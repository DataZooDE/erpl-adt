"""A CI job runs hundreds of erpl-adt commands as one user, then writes.

Every command logs on to SAP. If it does not log off again, SAP keeps the
logon until rdisp/plugin_auto_logout (30 min on the trial). Once the user
holds too many (rdisp/user_resource_limit/logon, 400 by default), SAP
refuses every new stateful session - the locks a write needs - with
"400 Service cannot be reached", and later even plain requests with
"429 Too Many Logons", for every tool of that user, for the next half
hour. Observed on A4H while running this suite.

Runs as the second user (SAP_COLLEAGUE_USER, default BWDEVELOPER) so that a
regression locks out that user, not the one the rest of the suite runs as.
"""

import json
import os
import tempfile

import pytest

pytestmark = [pytest.mark.session_logon, pytest.mark.slow]

# More than the default per-user logon limit.
COMMANDS = 450
CLASS = "ZCL_ERPL_LOGON_PROBE"
SOURCE = """CLASS zcl_erpl_logon_probe DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    METHODS run{n}.
ENDCLASS.

CLASS zcl_erpl_logon_probe IMPLEMENTATION.
  METHOD run{n}.
  ENDMETHOD.
ENDCLASS.
"""


def _write(cli, uri, n):
    with tempfile.NamedTemporaryFile("w", suffix=".abap", delete=False) as f:
        f.write(SOURCE.format(n=n))
        path = f.name
    try:
        return cli.run("source", "write", uri + "/source/main", "--file", path)
    finally:
        os.unlink(path)


@pytest.mark.timeout(1800)
def test_a_write_still_works_after_more_commands_than_the_logon_limit(colleague_cli):
    cli = colleague_cli
    cli.run("object", "delete", f"/sap/bc/adt/oo/classes/{CLASS.lower()}")
    created = cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", CLASS,
                         "--package", "$TMP", "--description", "erpl-adt logon probe")
    uri = created["uri"]
    try:
        first = _write(cli, uri, 0)
        assert first.returncode == 0, first.stderr
        for i in range(COMMANDS):
            result = cli.run("search", "ZERPL*", "--max", "1")
            assert result.returncode == 0, (
                f"command {i + 1} of {COMMANDS} failed (exit {result.returncode}) - "
                f"logons are left open on the server:\n{result.stderr}")
            json.loads(result.stdout)
        # A write needs a stateful session (lock): the first thing SAP refuses
        # when the user's logons are exhausted.
        last = _write(cli, uri, 1)
        assert last.returncode == 0, (
            f"write after {COMMANDS} commands failed - logons are left open on the "
            f"server:\n{last.stderr}")
    finally:
        cli.run("object", "delete", uri)
