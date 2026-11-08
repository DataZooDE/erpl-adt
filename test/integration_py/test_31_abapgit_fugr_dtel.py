"""Integration tests for `erpl-adt abapgit` with data elements and function groups.

Data element (DTEL): deployed through DDIF_DTEL_PUT / DDIF_DTEL_ACTIVATE like
SE11 and abapGit do. A data element refers to its domain, so the two are
deployed together from one repository folder and the domain must come first.

Function group (FUGR) with function modules: abapGit stores a function module
in SE37's "old" source style (FUNCTION statement + generated `*"` interface
comment block) plus the interface tables in <fg>.fugr.xml. The deploy writes
it the way Eclipse ADT does — the source in signature form
(`FUNCTION z_x IMPORTING VALUE(iv) TYPE t ...`) — and SAP regenerates exactly
the include abapGit serialized, which the export checks byte for byte.

Fixtures: fixtures/abapgit/erpl_demo/src (EPM demo model SNWD_SO / SNWD_SO_I /
SNWD_BPA, which exists on the A4H trial).
"""

import re
import shutil
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"

DOMA_FILE = SRC / "zerpl_d_so_status.doma.xml"
DTEL = "ZERPL_E_SO_STATUS"
DTEL_FILE = SRC / "zerpl_e_so_status.dtel.xml"

FUGR = "ZERPL_FG"
FUGR_FILE = SRC / "zerpl_fg.fugr.xml"
FUGR_FILES = sorted(SRC.glob("zerpl_fg.fugr.*"))
FM_ORDER_FILE = SRC / "zerpl_fg.fugr.z_erpl_read_order.abap"
CALL_CLASS = "ZCL_ERPL_FUGR_CALL"


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


class _AdtReader:
    """Read-only ADT REST access with the standard library, used to verify
    deployed objects independently of the erpl-adt code under test."""

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


def _data_element(adt):
    status, body = adt.get(
        f"/sap/bc/adt/ddic/dataelements/{DTEL.lower()}",
        headers={"Accept": "application/vnd.sap.adt.dataelements.v2+xml, application/xml"})
    return status, body


def _repo_with(tmp_path, *files):
    """A repository folder (.abapgit.xml + src/) holding just `files`."""
    repo = tmp_path / "repo"
    (repo / "src").mkdir(parents=True)
    shutil.copy(FIXTURES / ".abapgit.xml", repo / ".abapgit.xml")
    for f in files:
        shutil.copy(f, repo / "src" / f.name)
    return repo


@pytest.mark.abapgit_deploy
class TestDataElement:
    """Sales order status data element on the status domain."""

    def test_folder_deploy_creates_domain_before_data_element(self, cli, adt, tmp_path):
        cli.run("abapgit", "delete", str(DTEL_FILE), "--package", PACKAGE, "--force")
        repo = _repo_with(tmp_path, DOMA_FILE, DTEL_FILE)

        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)

        order = [(o["type"], o["name"]) for o in result["objects"]]
        assert order == [("DOMA", "ZERPL_D_SO_STATUS"), ("DTEL", DTEL)], order
        assert _entry(result, "DOMA", "ZERPL_D_SO_STATUS")["action"] in (
            "created", "updated", "unchanged")
        assert _entry(result, "DTEL", DTEL)["action"] == "created"
        status, body = _data_element(adt)
        assert status == 200, body[:800]
        assert "ZERPL_D_SO_STATUS" in body.upper()
        assert "Sales Order Status" in body
        assert "Order Status" in body

    def test_redeploy_unchanged_changes_nothing(self, cli, tmp_path):
        repo = _repo_with(tmp_path, DOMA_FILE, DTEL_FILE)
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert _entry(result, "DOMA", "ZERPL_D_SO_STATUS")["action"] == "unchanged"
        assert _entry(result, "DTEL", DTEL)["action"] == "unchanged"

    def test_changed_label_updates_in_place(self, cli, adt, tmp_path):
        changed = tmp_path / DTEL_FILE.name
        changed.write_bytes(DTEL_FILE.read_bytes().replace(
            b"<SCRTEXT_M>Order Status</SCRTEXT_M>", b"<SCRTEXT_M>Order State</SCRTEXT_M>"))

        result = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

        entry = _entry(result, "DTEL", DTEL)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["DD04V"]
        status, body = _data_element(adt)
        assert status == 200
        assert "Order State" in body

    def test_export_matches_file_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(DTEL_FILE), "--package", PACKAGE)

        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"DTEL:{DTEL}",
                   "--package", PACKAGE)

        assert (tmp_path / "src" / DTEL_FILE.name).read_bytes() == DTEL_FILE.read_bytes()

    def test_delete_removes_data_element(self, cli, adt):
        result = cli.run_ok("abapgit", "delete", str(DTEL_FILE), "--package", PACKAGE)
        assert _entry(result, "DTEL", DTEL)["action"] == "deleted"
        status, body = _data_element(adt)
        assert status == 404, body[:800]


def _fm_source(adt, fm):
    return adt.get(f"/sap/bc/adt/functions/groups/{FUGR.lower()}/fmodules/{fm.lower()}"
                   "/source/main", headers={"Accept": "text/plain"})


CALL_SOURCE = """CLASS @CLASS@ DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS @CLASS@ IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA order TYPE snwd_so.
    DATA items TYPE STANDARD TABLE OF snwd_so_i WITH DEFAULT KEY.
    SELECT so~so_id FROM snwd_so AS so
      INNER JOIN snwd_so_i AS i ON i~parent_key = so~node_key
      ORDER BY so~so_id
      INTO @DATA(so_id) UP TO 1 ROWS.
    ENDSELECT.
    CALL FUNCTION 'Z_ERPL_READ_ORDER'
      EXPORTING
        iv_so_id  = so_id
      IMPORTING
        es_order  = order
      TABLES
        et_items  = items
      EXCEPTIONS
        not_found = 1
        OTHERS    = 2.
    out->write( |RC { sy-subrc } ORDER { order-so_id } ITEMS { lines( items ) }| ).
  ENDMETHOD.
ENDCLASS.
"""


@pytest.mark.abapgit_deploy
class TestFunctionGroup:
    """Function group with two function modules on the EPM sales order model."""

    def test_deploy_creates_group_and_function_modules(self, cli, adt):
        cli.run("abapgit", "delete", str(FUGR_FILE), "--package", PACKAGE, "--force")

        result = cli.run_ok("abapgit", "deploy", str(FUGR_FILE), "--package", PACKAGE)

        entry = _entry(result, "FUGR", FUGR)
        assert entry["action"] == "created"
        assert "function module Z_ERPL_READ_ORDER" in entry["parts"]
        assert "function module Z_ERPL_READ_PARTNER" in entry["parts"]
        status, body = _fm_source(adt, "Z_ERPL_READ_ORDER")
        assert status == 200, body[:800]
        # ADT shows the interface in signature form, derived from the deploy.
        assert re.search(r"VALUE\(iv_so_id\)\s+TYPE\s+snwd_so_id", body, re.I), body[:800]
        assert re.search(r"et_items\s+LIKE\s+snwd_so_i", body, re.I), body[:800]
        status, body = _fm_source(adt, "Z_ERPL_READ_PARTNER")
        assert status == 200
        assert re.search(r"iv_with_contacts\s+TYPE\s+abap_bool\s+DEFAULT\s+abap_false", body, re.I)
        assert re.search(r"RAISING\s+cx_sy_itab_line_not_found", body, re.I)

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(FUGR_FILE), "--package", PACKAGE)
        assert _entry(result, "FUGR", FUGR)["action"] == "unchanged"

    def test_function_module_can_be_called(self, cli, tmp_path):
        cli.run("object", "delete", f"/sap/bc/adt/oo/classes/{CALL_CLASS.lower()}")
        try:
            cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", CALL_CLASS,
                       "--package", PACKAGE, "--description", "erpl demo: call Z_ERPL_READ_ORDER")
            source = tmp_path / "call.abap"
            source.write_text(CALL_SOURCE.replace("@CLASS@", CALL_CLASS.lower()))
            # `source write --activate` prints one JSON line per step.
            written = cli.run("source", "write",
                              f"/sap/bc/adt/oo/classes/{CALL_CLASS.lower()}/source/main",
                              "--file", str(source), "--activate")
            assert written.returncode == 0, written.stderr
            out = cli.run_ok("object", "run", CALL_CLASS)["output"]
            m = re.search(r"RC (\d+) ORDER (\S+) ITEMS (\d+)", out)
            assert m, out
            assert m.group(1) == "0", out
            assert int(m.group(3)) > 0, out
        finally:
            cli.run("object", "delete", f"/sap/bc/adt/oo/classes/{CALL_CLASS.lower()}")

    def test_changed_body_updates_only_that_module(self, cli, adt, tmp_path):
        for f in FUGR_FILES:
            shutil.copy(f, tmp_path / f.name)
        fm_file = tmp_path / FM_ORDER_FILE.name
        fm_file.write_bytes(fm_file.read_bytes().replace(
            b"ORDER BY so_item_pos INTO TABLE", b"ORDER BY so_item_pos DESCENDING INTO TABLE"))

        result = cli.run_ok("abapgit", "deploy", str(tmp_path / FUGR_FILE.name),
                            "--package", PACKAGE)

        entry = _entry(result, "FUGR", FUGR)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["function module Z_ERPL_READ_ORDER"]
        status, body = _fm_source(adt, "Z_ERPL_READ_ORDER")
        assert status == 200
        assert "so_item_pos DESCENDING" in body

    def test_export_matches_files_byte_for_byte(self, cli, tmp_path):
        result = cli.run_ok("abapgit", "deploy", str(FUGR_FILE), "--package", PACKAGE)
        assert _entry(result, "FUGR", FUGR)["action"] in ("updated", "unchanged")

        out = tmp_path / "out"
        cli.run_ok("abapgit", "export", str(out), "--object", f"FUGR:{FUGR}",
                   "--package", PACKAGE)

        exported = sorted(p.name for p in (out / "src").iterdir())
        assert exported == [f.name for f in FUGR_FILES]
        for f in FUGR_FILES:
            assert (out / "src" / f.name).read_bytes() == f.read_bytes(), f.name

    def test_delete_removes_function_group(self, cli, adt):
        result = cli.run_ok("abapgit", "delete", str(FUGR_FILE), "--package", PACKAGE)
        assert _entry(result, "FUGR", FUGR)["action"] == "deleted"
        status, body = adt.get(f"/sap/bc/adt/functions/groups/{FUGR.lower()}")
        assert status == 404, body[:800]
