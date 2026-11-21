"""`activate` must not report success when SAP did not activate (#82).

Asked to activate one object whose companions are inactive too, SAP answers
HTTP 200 with a pre-audit list (root ioc:inactiveObjects) instead of
activating - the list Eclipse shows in its "activate these too?" dialog. It
happens for objects of a transportable package recorded on a request; in
$TMP SAP just activates. erpl-adt read the list as "activated 0, failed 0"
and exited 0 while the object stayed inactive.

The scenario: a function group on a request whose main program was written
without activating, then `activate` of that main program alone.

Needs SAP_TRANSPORT_PACKAGE (a transportable package with change recording;
ZERPL_TR on A4H), like test_41.
"""

import json
import os
import random

import pytest

from cli_runner import release_with_tasks

PACKAGE = os.getenv("SAP_TRANSPORT_PACKAGE", "")
EXIT_ACTIVATION = 5


@pytest.mark.activation
@pytest.mark.skipif(not PACKAGE, reason="SAP_TRANSPORT_PACKAGE not set: needs a "
                    "transportable package with change recording")
def test_activate_reports_what_sap_wants_activated_with_it(cli, tmp_path):
    group = f"ZERPL_PA{random.randint(100, 999)}"
    group_uri = f"/sap/bc/adt/functions/groups/{group.lower()}"
    request = cli.run_ok("transport", "create", "--desc",
                         "pytest activation pre-audit (test_43)",
                         "--package", PACKAGE)["transport_number"]
    try:
        cli.run_ok("object", "create", "--type", "FUGR/F", "--name", group,
                   "--package", PACKAGE, "--description", "erpl-adt test_43",
                   "--transport", request)
        main = cli.run_ok("source", "read", group_uri + "/source/main")["source"]
        source = tmp_path / "main.abap"
        source.write_text(main.rstrip("\n") + "\n* changed (test_43)\n")
        cli.run_ok("source", "write", group_uri + "/source/main",
                   "--file", str(source), "--transport", request)

        result = cli.run("activate", group_uri + "/source/main")

        data = json.loads(result.stdout)
        assert result.returncode == EXIT_ACTIVATION, result.stdout + result.stderr
        assert data["failed"] > 0, data
        assert any(group in m for m in data["error_messages"]), data
        assert not any(request in m for m in data["error_messages"]), data

        # The message says how: everything in one activation request.
        assert any(group_uri in m for m in data["error_messages"]), data
        fixed = cli.run("activate", group_uri + "/source/main", group_uri)
        assert fixed.returncode == 0, fixed.stdout + fixed.stderr
        assert json.loads(fixed.stdout)["failed"] == 0, fixed.stdout
        # Nothing of the group is left inactive: activating the main program
        # again finds nothing to activate.
        again = cli.run("activate", group_uri + "/source/main")
        assert again.returncode == 0, again.stdout + again.stderr
        assert json.loads(again.stdout)["failed"] == 0, again.stdout
    finally:
        cli.run("object", "delete", group_uri, "--transport", request)
        release_with_tasks(cli, request)
