"""Integration tests for `erpl-adt abapgit deploy|export|status|delete`.

Classic SAP GUI objects (dynpros, GUI status/titles, transactions, domains,
text pools, function modules) have no ADT REST endpoint. `erpl-adt abapgit`
deploys them from abapGit-serialized files through SAP's own workbench
function modules (run by a temporary console helper class), and exports them
back into the same files.

The fixtures under fixtures/abapgit/erpl_demo model a small, realistic
order-display application on the EPM demo model (SNWD_SO / SNWD_SO_I /
SNWD_BPA) that exists on the A4H trial — the trial has no SD tables
(VBAK/VBAP), so a "Display Sales Order" clone is built on EPM.
"""

import json
import re
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"


def _entry(result, obj_type, name):
    """Return the deploy/export result entry for one object."""
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _fixed_values(adt, domain):
    """Read a domain's fixed values through ADT REST — independent of the
    helper-class engine under test."""
    status, body = adt.get(
        f"/sap/bc/adt/ddic/domains/{domain.lower()}",
        headers={"Accept": "application/vnd.sap.adt.domains.v2+xml, application/xml"},
    )
    assert status == 200, body[:500]
    lows = re.findall(r"<doma:low>([^<]*)</doma:low>", body)
    texts = re.findall(r"<doma:text>([^<]*)</doma:text>", body)
    return list(zip(lows, texts))


class _AdtReader:
    """Read-only ADT REST access with the standard library, used to verify
    deployed objects independently of the erpl-adt engine under test."""

    def __init__(self, cfg):
        import base64
        self.base = f"http://{cfg['host']}:{cfg['port']}"
        token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
        self.headers = {"Authorization": f"Basic {token}", "sap-client": cfg["client"],
                        "Accept": "*/*"}

    def get(self, path, headers=None):
        import urllib.error
        import urllib.request
        req = urllib.request.Request(self.base + path, headers={**self.headers, **(headers or {})})
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                return resp.status, resp.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as err:
            return err.code, err.read().decode("utf-8", "replace")


@pytest.fixture(scope="module")
def adt(sap_config):
    return _AdtReader(sap_config)


@pytest.mark.abapgit_deploy
class TestDomain:
    """Slice 1: a domain with fixed values (sales order lifecycle status)."""

    DOMA = "ZERPL_D_SO_STATUS"
    FILE = SRC / "zerpl_d_so_status.doma.xml"

    def test_deploy_creates_domain_with_fixed_values(self, cli, adt):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE)

        result = cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)

        assert _entry(result, "DOMA", self.DOMA)["action"] == "created"
        assert _fixed_values(adt, self.DOMA) == [
            ("N", "New"), ("P", "In process"), ("C", "Completed"), ("X", "Cancelled"),
        ]

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)
        assert _entry(result, "DOMA", self.DOMA)["action"] == "unchanged"

    def test_changed_fixed_value_updates_in_place(self, cli, adt, tmp_path):
        changed = tmp_path / self.FILE.name
        changed.write_bytes(
            self.FILE.read_bytes().replace(b"<DDTEXT>Cancelled</DDTEXT>",
                                           b"<DDTEXT>Rejected</DDTEXT>"))

        result = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

        assert _entry(result, "DOMA", self.DOMA)["action"] == "updated"
        assert ("X", "Rejected") in _fixed_values(adt, self.DOMA)

    def test_export_matches_file_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)

        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"DOMA:{self.DOMA}",
                   "--package", PACKAGE)

        exported = tmp_path / "src" / self.FILE.name
        assert exported.read_bytes() == self.FILE.read_bytes()

    def test_delete_removes_domain(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.FILE), "--package", PACKAGE)
        status, body = adt.get(f"/sap/bc/adt/ddic/domains/{self.DOMA.lower()}")
        assert status == 404, body[:800]


@pytest.mark.abapgit_deploy
class TestGuardRails:
    """Enterprise guard rails: nothing is written when a rule is violated."""

    FILE = SRC / "zerpl_d_so_status.doma.xml"

    def test_transportable_package_without_request_is_refused(self, cli, adt):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE)
        # ZCLASSIC_DEVELOPMENT is a transportable customer package on A4H.
        result = cli.run("abapgit", "deploy", str(self.FILE), "--package", "ZCLASSIC_DEVELOPMENT")

        assert result.returncode == 9, result.stderr
        data = json.loads(result.stdout)
        entry = _entry(data, "DOMA", "ZERPL_D_SO_STATUS")
        assert entry["action"] == "refused"
        assert "--transport" in entry["message"]
        status, _ = adt.get("/sap/bc/adt/ddic/domains/zerpl_d_so_status")
        assert status == 404  # nothing was written

    def test_sap_object_is_refused(self, cli, tmp_path):
        sap_file = tmp_path / "xfeld.doma.xml"
        sap_file.write_bytes(self.FILE.read_bytes().replace(
            b"ZERPL_D_SO_STATUS", b"XFELD"))

        result = cli.run("abapgit", "deploy", str(sap_file), "--package", PACKAGE)

        assert result.returncode == 1, result.stderr
        assert _entry(json.loads(result.stdout), "DOMA", "XFELD")["action"] == "refused"

    def test_status_reports_without_writing(self, cli, adt):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE)

        data = cli.run_ok("abapgit", "status", str(self.FILE), "--package", PACKAGE)

        assert data["dry_run"] is True
        assert _entry(data, "DOMA", "ZERPL_D_SO_STATUS")["action"] == "created"
        status, _ = adt.get("/sap/bc/adt/ddic/domains/zerpl_d_so_status")
        assert status == 404

    def test_no_helper_classes_are_left_behind(self, cli):
        data = cli.run_ok("search", "ZCL_ERPLHLP*")
        names = [r.get("name") for r in (data if isinstance(data, list) else data.get("results", []))]
        assert names == [], f"leftover helper classes: {names}"
