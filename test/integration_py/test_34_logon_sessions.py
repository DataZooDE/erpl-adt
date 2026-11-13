"""A CI job runs many erpl-adt commands as one user - none may stay open.

Every stateful step (lock, write, unlock) opens an ABAP user context on the
server. Left behind, it stays until rdisp/plugin_auto_logout (30 min on the
trial). At 400 per user SAP refuses new stateful sessions ("400 Service
cannot be reached") and then every request ("429 Too Many Logons"), for
every tool of that user, for half an hour - observed on A4H while two
agents ran this suite. `sap-sessioncmd=close` is answered 200 but ends
nothing; ICF's /sap/public/bc/icf/logoff does.

The oracle is SAP's own session list (TH_USER_LIST, what SM04 shows), read
by a console class the test creates: after a series of writes the user
holds no more sessions than before. Runs as the second user
(SAP_COLLEAGUE_USER, default BWDEVELOPER), so the count is not disturbed by
the rest of the suite.
"""

import os
import tempfile

import pytest

pytestmark = [pytest.mark.session_logon]

CLASS = "ZCL_ERPL_LOGON_PROBE"
URI = f"/sap/bc/adt/oo/classes/{CLASS.lower()}"
WRITES = 20
# Sessions of the user itself; the classrun that counts runs in its own
# session, which is gone again by the time the next count runs.
SOURCE = """CLASS zcl_erpl_logon_probe DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_logon_probe IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA list TYPE STANDARD TABLE OF uinfo.
    CALL FUNCTION 'TH_USER_LIST'
      TABLES
        list = list.
    DELETE list WHERE bname <> sy-uname.
    out->write( |SESSIONS={ lines( list ) }| ).
  ENDMETHOD.
ENDCLASS.
"""


def _sessions(cli):
    result = cli.run_no_json("object", "run", CLASS)
    assert result.returncode == 0, result.stderr
    for line in result.stdout.splitlines():
        if line.strip().startswith("SESSIONS="):
            return int(line.strip().split("=", 1)[1])
    raise AssertionError(f"no session count in:\n{result.stdout}")


@pytest.fixture
def probe(colleague_cli):
    cli = colleague_cli
    cli.run("object", "delete", URI)
    cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", CLASS,
               "--package", "$TMP", "--description", "erpl-adt logon probe")
    with tempfile.NamedTemporaryFile("w", suffix=".abap", delete=False) as f:
        f.write(SOURCE)
        path = f.name
    try:
        written = cli.run("source", "write", URI + "/source/main", "--file", path, "--activate")
        assert written.returncode == 0, written.stderr
        yield path
    finally:
        os.unlink(path)
        cli.run("object", "delete", URI)


def test_writes_leave_no_session_open(colleague_cli, probe):
    cli = colleague_cli
    before = _sessions(cli)
    for i in range(WRITES):
        result = cli.run("source", "write", URI + "/source/main", "--file", probe,
                         "--activate")
        assert result.returncode == 0, f"write {i + 1} failed:\n{result.stderr}"
    after = _sessions(cli)
    # One of slack for an unrelated session of the same user.
    assert after - before <= 1, (
        f"{WRITES} writes left {after - before} sessions open on the server "
        f"({before} before, {after} after)")


def test_plain_commands_leave_no_session_open(colleague_cli, probe):
    cli = colleague_cli
    before = _sessions(cli)
    for _ in range(WRITES):
        assert cli.run("search", "ZERPL*", "--max", "1").returncode == 0
    after = _sessions(cli)
    assert after - before <= 1, (before, after)
