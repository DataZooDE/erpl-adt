"""The abapgit_* MCP tools over stdio JSON-RPC, the way an agent calls them.

`erpl-adt mcp --abapgit-root <fixtures>` serves abapgit_status / _deploy /
_export / _delete. The test deploys the DOMA fixture ZERPL_D_SO_STATUS through
the tools, checks the domain independently over ADT REST, checks that the
structured result is the CLI's `--json` report, and that paths outside the
root are refused. It deletes the domain again only if it did not exist before.
"""

import base64
import json
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

import pytest

ROOT = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
FILE = "src/zerpl_d_so_status.doma.xml"
DOMA = "ZERPL_D_SO_STATUS"
PACKAGE = "$TMP"

pytestmark = pytest.mark.abapgit_deploy


def mcp_call(cli, calls, root=ROOT, timeout=900):
    """Send tools/call messages to one stdio MCP server, return responses by call."""
    messages = [
        {"jsonrpc": "2.0", "id": i, "method": "tools/call",
         "params": {"name": name, "arguments": args}}
        for i, (name, args) in enumerate(calls)
    ]
    cmd = [cli.binary, "--host", cli.host, "--port", str(cli.port), "--user", cli.user,
           "--password", cli.password, "--client", cli.client, "--timeout", "300", "mcp",
           "--abapgit-root", str(root)]
    proc = subprocess.run(cmd, input="\n".join(json.dumps(m) for m in messages),
                          capture_output=True, text=True, timeout=timeout)
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            msg = json.loads(line)
            if "id" in msg:
                out[msg["id"]] = msg
    responses = [out.get(i) for i in range(len(calls))]
    assert all(r is not None for r in responses), proc.stderr[-2000:]
    return responses


def structured(response):
    result = response["result"]
    assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
    return result["structuredContent"]


def entry(report, obj_type, name):
    for obj in report["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in {report['objects']}")


def domain_status(cfg):
    token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
    req = urllib.request.Request(
        f"http://{cfg['host']}:{cfg['port']}/sap/bc/adt/ddic/domains/{DOMA.lower()}",
        headers={"Authorization": f"Basic {token}", "sap-client": cfg["client"],
                 "Accept": "application/vnd.sap.adt.domains.v2+xml, application/xml"})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as err:
        return err.code, err.read().decode("utf-8", "replace")


@pytest.fixture(scope="module")
def existed_before(sap_config):
    status, _ = domain_status(sap_config)
    assert status in (200, 404), status
    return status == 200


class TestAbapGitMcpTools:

    def test_tools_are_listed_with_their_classification(self, cli):
        cmd = [cli.binary, "--host", cli.host, "--port", str(cli.port), "--user", cli.user,
               "--password", cli.password, "--client", cli.client, "mcp",
               "--abapgit-root", str(ROOT)]
        proc = subprocess.run(
            cmd, input=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}),
            capture_output=True, text=True, timeout=120)
        tools = {t["name"]: t for t in
                 json.loads(proc.stdout.strip().splitlines()[0])["result"]["tools"]}
        assert tools["abapgit_status"]["annotations"]["readOnlyHint"] is True
        for name in ("abapgit_deploy", "abapgit_export", "abapgit_delete"):
            assert tools[name]["annotations"]["destructiveHint"] is True
            assert tools[name]["outputSchema"]["type"] == "object"

    def test_status_deploy_status_and_cleanup(self, cli, sap_config, existed_before, tmp_path):
        status, deploy, again, export = mcp_call(cli, [
            ("abapgit_status", {"path": FILE, "package": PACKAGE}),
            ("abapgit_deploy", {"path": FILE, "package": PACKAGE}),
            ("abapgit_status", {"path": FILE, "package": PACKAGE}),
            ("abapgit_export", {"path": "src", "package": PACKAGE, "dry_run": True,
                                "objects": [f"DOMA:{DOMA}"]}),
        ])
        before = structured(status)
        assert before["exit_code"] == 0, before
        assert entry(before, "DOMA", DOMA)["action"] in ("created", "unchanged", "updated")

        deployed = structured(deploy)
        assert deploy["result"].get("isError") is not True, deployed
        assert deployed["exit_code"] == 0
        assert entry(deployed, "DOMA", DOMA)["action"] in ("created", "unchanged", "updated")

        # Independent check over ADT REST: the domain exists with its values.
        http_status, body = domain_status(sap_config)
        assert http_status == 200, body[:500]
        assert "<doma:low>N</doma:low>" in body

        after = structured(again)
        assert entry(after, "DOMA", DOMA)["action"] == "unchanged"

        # The structured result is the CLI's --json report.
        cli_report = cli.run_ok("abapgit", "status", str(ROOT / FILE), "--package", PACKAGE)
        assert cli_report == after

        exported = structured(export)
        assert exported["dry_run"] is True
        assert entry(exported, "DOMA", DOMA)["action"] == "exported"

        if not existed_before:
            deleted = structured(mcp_call(cli, [
                ("abapgit_delete", {"path": FILE, "package": PACKAGE}),
            ])[0])
            assert entry(deleted, "DOMA", DOMA)["action"] == "deleted", deleted
            assert domain_status(sap_config)[0] == 404

    def test_paths_outside_the_root_are_refused(self, cli, tmp_path):
        outside = tmp_path / "outside"
        outside.mkdir()
        calls = [
            ("abapgit_status", {"path": "../../../../../../etc/passwd", "package": PACKAGE}),
            ("abapgit_deploy", {"path": str(outside), "package": PACKAGE}),
            ("abapgit_delete", {"path": FILE, "package": PACKAGE, "dryrun": True}),
        ]
        for (name, _), response in zip(calls, mcp_call(cli, calls)):
            result = response["result"]
            assert result.get("isError") is True, f"{name}: {result}"
        texts = [r["result"]["content"][0]["text"] for r in mcp_call(cli, calls[:2])]
        assert all("outside the abapGit root" in t for t in texts), texts
