"""Call the MCP tools over the protocol, the way an agent does.

Every other test in this suite drives the CLI. That left a blind spot: a fix
applied in the CLI handler can leave the same operation broken as a tool, and
nothing noticed. It happened three times — `bw applog` and `bw validate` both
worked from the command line while their tools returned HTTP 400 and 500, and
the defaults that made the CLI work lived only in the CLI.

So these tests speak JSON-RPC to `erpl-adt mcp` on stdin, and assert that the
read-only tools answer without `isError`. They are deliberately shallow: the
point is coverage of the *path*, not of each tool's semantics, which the
CLI tests already carry.
"""

import json
import subprocess


def mcp_call(cli, calls, timeout=600):
    """Send tools/call messages to the stdio MCP server, return results by id."""
    messages = [
        {"jsonrpc": "2.0", "id": i, "method": "tools/call",
         "params": {"name": name, "arguments": args}}
        for i, (name, args) in enumerate(calls)
    ]
    cmd = [cli.binary, "--host", cli.host, "--port", str(cli.port),
           "--user", cli.user, "--password", cli.password,
           "--client", cli.client, "mcp"]
    proc = subprocess.run(cmd, input="\n".join(json.dumps(m) for m in messages),
                          capture_output=True, text=True, timeout=timeout)
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        msg = json.loads(line)
        if "id" in msg:
            out[msg["id"]] = msg
    return [out.get(i) for i in range(len(calls))]


def tool_error(response):
    """Return the error text when a tool call failed, else None."""
    if response is None:
        return "no response"
    if "error" in response:
        return f"JSON-RPC {response['error'].get('code')}: {response['error'].get('message')}"
    result = response.get("result", {})
    if result.get("isError"):
        return result["content"][0]["text"][:200]
    return None


class TestMcpToolsAnswer:
    """Read-only tools must work over the protocol, not only via the CLI."""

    def test_tools_list_is_served(self, cli):
        cmd = [cli.binary, "--host", cli.host, "--port", str(cli.port),
               "--user", cli.user, "--password", cli.password,
               "--client", cli.client, "mcp"]
        proc = subprocess.run(
            cmd, input=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}),
            capture_output=True, text=True, timeout=120)
        tools = json.loads(proc.stdout.strip().splitlines()[0])["result"]["tools"]
        assert len(tools) > 50
        # Every tool carries the metadata a host needs to present it.
        for tool in tools:
            assert tool["name"]
            assert tool["description"]
            assert tool["inputSchema"]["type"] == "object"
            assert "annotations" in tool, f"{tool['name']} has no annotations"
            assert tool.get("title"), f"{tool['name']} has no title"

    def test_core_adt_tools(self, cli):
        calls = [
            ("adt_discover", {}),
            ("adt_search", {"query": "CL_ABAP_RANDOM", "max_results": 2}),
            ("adt_read_object", {"uri": "/sap/bc/adt/oo/classes/cl_abap_random"}),
            ("adt_read_source", {"uri": "/sap/bc/adt/oo/classes/cl_abap_random/source/main"}),
            ("adt_read_table", {"table_name": "SFLIGHT"}),
            ("adt_list_package", {"package_name": "$TMP"}),
            ("adt_package_exists", {"package_name": "$TMP"}),
            ("adt_list_transports", {}),
        ]
        failures = [f"{name}: {err}"
                    for (name, _), resp in zip(calls, mcp_call(cli, calls))
                    if (err := tool_error(resp))]
        assert not failures, "MCP tools failed:\n  " + "\n  ".join(failures)

    def test_tools_return_structured_content(self, cli):
        """Results carry the payload as data, not only as a JSON string."""
        [resp] = mcp_call(cli, [("adt_search", {"query": "CL_ABAP_RANDOM",
                                                "max_results": 1})])
        assert tool_error(resp) is None
        assert resp is not None
        result = resp["result"]
        assert "content" in result
        assert "structuredContent" in result, "structuredContent missing"
        assert not isinstance(result["structuredContent"], str)

    def test_bw_tools_that_only_ever_worked_from_the_cli(self, cli, bw_available):
        """The two that were broken as tools while the command line was fine.

        bw_application_log answered HTTP 400 "Parameter username could not be
        found" and bw_validate answered HTTP 500 "Action 'validate' is not
        valid", because the defaults that fixed the CLI lived in its handler.
        """
        objects = mcp_call(cli, [("bw_search", {"query": "*", "object_type": "ADSO",
                                                "max_results": 1})])[0]
        assert tool_error(objects) is None
        assert objects is not None
        found = json.loads(objects["result"]["content"][0]["text"])
        adso = found[0]["name"] if found else None

        calls = [("bw_application_log", {}), ("bw_discover", {}), ("bw_sysinfo", {})]
        if adso:
            calls.append(("bw_validate", {"object_type": "ADSO", "object_name": adso}))

        failures = [f"{name}: {err}"
                    for (name, _), resp in zip(calls, mcp_call(cli, calls))
                    if (err := tool_error(resp))]
        assert not failures, "MCP tools failed:\n  " + "\n  ".join(failures)

    def test_a_missing_object_is_an_error_not_an_empty_result(self, cli):
        """The silent-success contract holds for tools too, not just the CLI."""
        calls = [
            ("adt_run_atc", {"uri": "/sap/bc/adt/oo/classes/zzz_erpl_missing_99"}),
            ("adt_run_tests", {"uri": "/sap/bc/adt/oo/classes/zzz_erpl_missing_99"}),
            ("adt_check_syntax", {"uri": "/sap/bc/adt/oo/classes/zzz_erpl_missing_99"}),
        ]
        for (name, _), resp in zip(calls, mcp_call(cli, calls)):
            assert tool_error(resp) is not None, (
                f"{name} reported success for an object that does not exist")
