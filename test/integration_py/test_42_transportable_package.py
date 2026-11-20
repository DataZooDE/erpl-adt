"""A transportable package, created and filled on a request.

`object create --type DEVC/K --software-component HOME --transport <req>`
is how a team starts transportable work: a package under a structure package
(ZCLASSIC_DEVELOPMENT on A4H), recorded on a request, then objects in it,
recorded on the same request. The package XML never said that changes are
to be recorded, so SAP refused it: "Change recording must be activated for
package ...". Reviewed with `transport show` (#76).
"""

import random

import pytest

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

            # Its task is still open, so SAP refuses to release the request -
            # with HTTP 200 and the reason in the release report. That must
            # not read as "Released" (#72).
            task = cli.run_ok("transport", "show", request)["tasks"][0]["number"]
            release = cli.run("transport", "release", request)
            assert release.returncode == 9, release.stdout + release.stderr
            assert task in release.stderr, release.stderr
            assert cli.run_ok("transport", "show", request)["status"] == "modifiable"
        finally:
            cli.run("object", "delete", f"/sap/bc/adt/oo/classes/{clas.lower()}",
                    "--transport", request)
            cli.run("object", "delete", f"/sap/bc/adt/packages/{package.lower()}",
                    "--transport", request)
