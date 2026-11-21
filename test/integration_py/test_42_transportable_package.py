"""A transportable package, created and filled on a request.

`object create --type DEVC/K --software-component HOME --transport <req>`
is how a team starts transportable work: a package under a structure package
(ZCLASSIC_DEVELOPMENT on A4H), recorded on a request, then objects in it,
recorded on the same request. The package XML never said that changes are
to be recorded, so SAP refused it: "Change recording must be activated for
package ...". Reviewed with `transport show` (#76).
"""

import json
import random

import pytest

from cli_runner import release_with_tasks

STRUCTURE_PACKAGE = "ZCLASSIC_DEVELOPMENT"


def _entries(cli, number):
    data = cli.run_ok("transport", "show", number)
    return {(o["pgmid"], o["type"], o["name"])
            for o in data["objects"] + [o for t in data["tasks"] for o in t["objects"]]}


@pytest.mark.transport
class TestTransportablePackage:

    def test_package_and_class_are_recorded_on_the_request(self, cli):
        suffix = random.randint(10000, 99999)
        package = f"ZERPL_TP_{suffix}"
        clas = f"ZCL_ERPL_TP_{suffix}"
        request = cli.run_ok("transport", "create", "--desc",
                             "pytest transportable package (test_42)",
                             "--package", STRUCTURE_PACKAGE)["transport_number"]
        try:
            created = cli.run("object", "create", "--type", "DEVC/K", "--name", package,
                              "--package", STRUCTURE_PACKAGE, "--software-component", "HOME",
                              "--description", "erpl-adt transportable package test",
                              "--transport", request)
            assert created.returncode == 0, created.stderr
            assert ("R3TR", "DEVC", package) in _entries(cli, request)

            cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", clas,
                       "--package", package, "--description", "erpl-adt test",
                       "--transport", request)
            assert ("R3TR", "CLAS", clas) in _entries(cli, request)

            # Its task is still open. SAP releases a request only after its
            # tasks, so `transport release` releases my open task first, then
            # the request - and says so.
            task = cli.run_ok("transport", "show", request)["tasks"][0]["number"]
            release = cli.run("transport", "release", request)
            if release.returncode == 0:
                assert task in json.loads(release.stdout)["released_tasks"], release.stdout
            else:
                # A4H: the trial has no export, so the request's own release
                # report ends with "Error in pre-export methods" (exit 9) while
                # SAP still marks it released; the task went first either way.
                assert release.returncode == 9, release.stdout + release.stderr
                assert "pre-export" in release.stderr, release.stderr
                assert task in release.stderr, release.stderr
            shown = cli.run_ok("transport", "show", request)
            assert shown["tasks"][0]["status"] == "released", shown
            assert shown["status"] == "released", shown
        finally:
            # A released request records nothing more: delete on a new one.
            cleanup = request
            if cli.run_ok("transport", "show", request)["status"] != "modifiable":
                cleanup = cli.run_ok("transport", "create", "--desc",
                                     "pytest transportable package cleanup (test_42)",
                                     "--package", STRUCTURE_PACKAGE)["transport_number"]
            cli.run("object", "delete", f"/sap/bc/adt/oo/classes/{clas.lower()}",
                    "--transport", cleanup)
            cli.run("object", "delete", f"/sap/bc/adt/packages/{package.lower()}",
                    "--transport", cleanup)
            release_with_tasks(cli, cleanup)
