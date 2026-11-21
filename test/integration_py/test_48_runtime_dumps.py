"""Diagnosing a short dump without SAP GUI (issue #89).

An agent runs a console class with `object run` and it short-dumps. Before
#89 the agent got exit 99 "Application Server Error" and no way to see which
runtime error it was, short of ST22 in SAP GUI or curl against ADT.

Scenario: a report class computes an average over zero orders (division by
zero). The run fails, and
- the error hint names the runtime error and the command to see the dump;
- `dump list --owner DEVELOPER --since <before the run>` lists exactly it;
- `dump show <id> --json` says runtime error, program, include and line;
- the MCP tools adt_list_dumps / adt_show_dump give the agent the same.
The agent fixes the line and the run succeeds.
"""

import datetime
import random

import pytest

from test_25_mcp_smoke import mcp_call, tool_error

# Line 9 of the class divides by the order count.
DUMP_LINE = 9


def report_source(name, orders):
    return (
        f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        "  PUBLIC SECTION.\n"
        "    INTERFACES if_oo_adt_classrun.\n"
        "ENDCLASS.\n\n"
        f"CLASS {name} IMPLEMENTATION.\n"
        "  METHOD if_oo_adt_classrun~main.\n"
        f"    DATA(lv_orders) = {orders}.\n"
        "    out->write( |Average order value: { 1000 / lv_orders }| ).\n"
        "  ENDMETHOD.\n"
        "ENDCLASS.\n"
    )


@pytest.fixture
def report_class(cli, tmp_path):
    name = f"ZCL_ERPL_AVG_{random.randint(10000, 99999)}"
    data = cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", name,
                      "--package", "$TMP", "--description", "#89 average report")
    uri = data.get("uri", f"/sap/bc/adt/oo/classes/{name.lower()}")
    yield name, uri
    cli.run("object", "delete", uri)


def write(cli, uri, source, tmp_path):
    path = tmp_path / "src.abap"
    path.write_text(source)
    cli.run_ok("source", "write", uri + "/source/main", "--file", str(path), "--activate")


@pytest.mark.dumps
@pytest.mark.classrun
class TestDiagnoseAShortDump:

    def test_agent_finds_and_reads_the_dump(self, cli, report_class, tmp_path):
        name, uri = report_class
        write(cli, uri, report_source(name, 0), tmp_path)
        since = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(seconds=5)
        since_arg = since.strftime("%Y-%m-%dT%H:%M:%SZ")

        run = cli.run("object", "run", name)
        assert run.returncode != 0, run.stdout
        assert "COMPUTE_INT_ZERODIVIDE" in run.stderr, run.stderr
        assert "erpl-adt dump show" in run.stderr, run.stderr

        listed = cli.run_ok("dump", "list", "--owner", "DEVELOPER", "--since", since_arg)
        ours = [d for d in listed if d["program"].startswith(name)]
        assert len(ours) == 1, listed
        dump = ours[0]
        assert dump["error"] == "COMPUTE_INT_ZERODIVIDE", dump
        assert dump["user"] == "DEVELOPER", dump
        assert dump["short_text"], dump
        assert dump["id"] in run.stderr, "the hint names this dump"

        shown = cli.run_ok("dump", "show", dump["id"])
        assert shown["error"] == "COMPUTE_INT_ZERODIVIDE", shown
        assert shown["exception"] == "CX_SY_ZERODIVIDE", shown
        assert shown["program"].startswith(name), shown
        assert shown["include"].startswith(name), shown  # the method include, ...CM001
        assert shown["line"] == DUMP_LINE, shown
        assert shown["include_line"] == DUMP_LINE - 6, shown  # METHOD is line 1 of ...CM001
        assert shown["uri"] == uri + "/source/main", shown
        assert shown["event"].upper() == "IF_OO_ADT_CLASSRUN~MAIN", shown
        assert "text" not in shown  # the full dump only on request
        full = cli.run_ok("dump", "show", dump["id"], "--full")
        assert "COMPUTE_INT_ZERODIVIDE" in full["text"]

        human = cli.run_no_json("dump", "show", dump["id"])
        assert human.returncode == 0, human.stderr
        assert "COMPUTE_INT_ZERODIVIDE" in human.stdout
        assert f"line {DUMP_LINE}" in human.stdout, human.stdout

        # The same through MCP.
        listed_mcp, shown_mcp = mcp_call(cli, [
            ("adt_list_dumps", {"owner": "DEVELOPER", "since": since_arg}),
            ("adt_show_dump", {"id": dump["id"]}),
        ])
        assert tool_error(listed_mcp) is None, tool_error(listed_mcp)
        assert tool_error(shown_mcp) is None, tool_error(shown_mcp)
        dumps = listed_mcp["result"]["structuredContent"]["result"]
        assert [d["id"] for d in dumps if d["program"].startswith(name)] == [dump["id"]]
        assert shown_mcp["result"]["structuredContent"]["line"] == DUMP_LINE

        # The agent fixes the cause; the run works and adds no dump.
        write(cli, uri, report_source(name, 4), tmp_path)
        assert "250" in cli.run_ok("object", "run", name)["output"]
        again = cli.run_ok("dump", "list", "--owner", "DEVELOPER", "--since", since_arg)
        assert len([d for d in again if d["program"].startswith(name)]) == 1

    def test_unknown_dump_is_not_found(self, cli):
        result = cli.run("dump", "show", "20000101000000nohost_X_00%20NOBODY%20001%201")
        assert result.returncode == 2, result.stdout + result.stderr
