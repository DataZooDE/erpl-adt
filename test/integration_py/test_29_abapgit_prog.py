"""Integration tests for `erpl-adt abapgit` with programs (PROG).

A PROG object is a set of abapGit files:
  <name>.prog.xml               PROGDIR, DYNPROS, CUA (GUI status / titles), TPOOL
  <name>.prog.abap              source
  <name>.prog.screen_NNNN.abap  flow logic of screen NNNN

The fixtures model two classic EPM applications (the A4H trial has no SD/MM
tables, so the VA03 / ME2L look-alikes run on SNWD_SO / SNWD_PO):
  ZERPL_SO_DISPLAY  module pool "Display Sales Order": initial screen with F4,
                    overview with tabstrip, table control subscreen, custom
                    control subscreen, dialog box, GUI status MAIN / DIALOG
  ZERPL_PO_REPORT   executable report "Purchase orders by supplier" with a
                    selection screen and ALV output

Every result is verified independently of the engine under test: ADT REST
(source, text elements, inactive objects) and `erpl-adt screen read`.
The files are canonical: deploy -> export reproduces them byte for byte.
"""

import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _files(stem):
    return sorted(p for p in SRC.iterdir() if p.name.startswith(stem + "."))


def _copy(stem, target):
    """Copy all files of an object into `target` (single-file mode reads siblings)."""
    for path in _files(stem):
        shutil.copy(path, target / path.name)
    return target / f"{stem}.prog.xml"


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _git_copy(stem, target):
    """Copy the object into a fresh git work tree with the fixture committed.

    Dynpros record no last changer in SAP, so a deploy that changes a screen
    is only allowed when the version in the system is in the files' git
    history - as in a real repository, where the deployed version was
    committed. A plain temp copy has no history and would be refused."""
    xml = _copy(stem, target)
    _git(target, "init", "-q")
    _git(target, "add", ".")
    _git(target, "commit", "-qm", "fixture")
    return xml


def _commit(repo, message="change"):
    _git(repo, "add", ".")
    _git(repo, "commit", "-qm", message)


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

    def source(self, program):
        status, body = self.get(f"/sap/bc/adt/programs/programs/{program.lower()}/source/main",
                                {"Accept": "text/plain"})
        return status, body.replace("\r\n", "\n")

    def text_elements(self, program, kind):
        """kind: symbols | selections | headings"""
        status, body = self.get(
            f"/sap/bc/adt/textelements/programs/{program.lower()}/source/{kind}",
            {"Accept": f"application/vnd.sap.adt.textelements.{kind}.v1"})
        assert status == 200, body[:500]
        return body.replace("\r\n", "\n")

    def inactive(self, program):
        """Names of this user's inactive objects that belong to `program`."""
        status, body = self.get("/sap/bc/adt/activation/inactiveobjects")
        assert status == 200, body[:500]
        names = re.findall(r'adtcore:name="([^"]*)"', body)
        return [n for n in names if n.split()[0].upper() == program]


@pytest.fixture(scope="module")
def adt(sap_config):
    return _AdtReader(sap_config)


def _screen_fields(cli, program, screen):
    """Read a screen through `erpl-adt screen read` (its own helper class)."""
    result = cli.run_ok("screen", "read", "--program", program, "--screen", screen,
                        "--package", PACKAGE)
    return result


@pytest.mark.abapgit_deploy
class TestModulePool:
    """ZERPL_SO_DISPLAY: module pool with five screens, GUI status and titles."""

    PROG = "ZERPL_SO_DISPLAY"
    STEM = "zerpl_so_display"
    XML = SRC / "zerpl_so_display.prog.xml"

    def test_deploy_creates_program_with_all_parts(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.XML), "--package", PACKAGE, "--force")

        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "created"
        assert entry["parts"] == ["source", "PROGDIR", "screen 0100", "screen 0200",
                                  "screen 0210", "screen 0220", "screen 0300", "CUA", "TPOOL"]
        status, source = adt.source(self.PROG)
        assert status == 200, source[:500]
        assert source.rstrip("\n") == (SRC / f"{self.STEM}.prog.abap").read_text().rstrip("\n")
        assert adt.inactive(self.PROG) == []

    def test_screen_read_shows_the_overview_fields(self, cli):
        screen = _screen_fields(cli, self.PROG, "0200")
        fields = {f["name"]: f for f in screen["fields"]}
        for name in ("BOX_HEADER", "SNWD_SO-SO_ID", "GS_BUYER-COMPANY_NAME",
                     "SNWD_SO-GROSS_AMOUNT", "SNWD_SO-CURRENCY_CODE",
                     "SNWD_SO-LIFECYCLE_STATUS", "GV_BILLED", "GV_NET", "GV_GROSS",
                     "BTN_REFRESH", "TAB_ITEMS", "TAB_NOTES", "OK_CODE"):
            assert name in fields, f"{name} missing on screen 0200: {sorted(fields)}"
        company = fields["GS_BUYER-COMPANY_NAME"]
        assert (company["line"], company["column"]) == (3, 20)
        assert company["output"] and not company["input"]
        assert (screen["lines"], screen["columns"]) == (27, 120)

    CHECK_CLASS = "ZCL_ERPL_SO_DISPLAY_CHECK"
    CHECK_SOURCE = """CLASS zcl_erpl_so_display_check DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_so_display_check IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    " What screen 0200 of ZERPL_SO_DISPLAY shows for one order: the module
    " pool's own FORM read_order fills the fields the screen displays.
    DATA(program) = `ZERPL_SO_DISPLAY`.
    DATA(form) = `READ_ORDER`.
    FIELD-SYMBOLS <value> TYPE any.
    PERFORM (form) IN PROGRAM (program).
    ASSIGN ('(ZERPL_SO_DISPLAY)SNWD_SO-SO_ID') TO <value>.
    <value> = '500000728'.
    PERFORM (form) IN PROGRAM (program).
    LOOP AT VALUE string_table( ( `SNWD_SO-SO_ID` ) ( `GS_BUYER-COMPANY_NAME` )
                                ( `SNWD_SO-GROSS_AMOUNT` ) ( `SNWD_SO-CURRENCY_CODE` )
                                ( `SNWD_SO-LIFECYCLE_STATUS` ) ) INTO DATA(field).
      DATA(global) = `(` && program && `)` && field.
      ASSIGN (global) TO <value>.
      IF sy-subrc = 0.
        out->write( |{ field }={ <value> }| ).
      ELSE.
        out->write( |{ field }=?| ).
      ENDIF.
    ENDLOOP.
  ENDMETHOD.
ENDCLASS.
"""

    def test_overview_header_fields_are_filled_for_an_order(self, cli, tmp_path):
        # Regression: the buyer's company stayed empty on screen 0200 in the
        # GUI. A screen field with a dictionary reference (SNWD_BPA-...) is only
        # filled from a TABLES work area, and SNWD_BPA (not flat) cannot be
        # one - so the screen shows the program field GS_BUYER-COMPANY_NAME.
        # 1. Every dictionary field of screen 0200 is backed by TABLES.
        xml = (SRC / f"{self.STEM}.prog.xml").read_text(encoding="utf-8")
        screen = xml[xml.index("<SCREEN>0200</SCREEN>"):]
        screen = screen[:screen.index("</FIELDS>")]
        dict_tables = set()
        for field in re.findall(r"<RPY_DYFATC>(.*?)</RPY_DYFATC>", screen, re.S):
            name = re.search(r"<NAME>([^<]*)</NAME>", field).group(1)
            if "<FROM_DICT>X</FROM_DICT>" in field and "-" in name:
                dict_tables.add(name.split("-")[0])
        source = (SRC / f"{self.STEM}.prog.abap").read_text()
        tables = re.search(r"^TABLES:?([^.]*)\.", source, re.M).group(1)
        declared = {t.strip().upper() for t in tables.split(",")}
        assert dict_tables and dict_tables <= declared, (dict_tables, declared)
        # 2. At runtime, the fields hold the order's data. Order 500000728's
        # buyer is BP 0100000000 "OffiPOR" (SNWD_SO-BUYER_GUID -> SNWD_BPA,
        # read on A4H with hdbsql).
        uri = f"/sap/bc/adt/oo/classes/{self.CHECK_CLASS.lower()}"
        cli.run("object", "delete", uri)
        cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", self.CHECK_CLASS,
                   "--package", PACKAGE, "--description", "erpl-adt test: ZERPL_SO_DISPLAY data")
        try:
            src = tmp_path / "check.abap"
            src.write_text(self.CHECK_SOURCE)
            written = cli.run("source", "write", self.CHECK_CLASS, "--file", str(src),
                              "--activate")
            assert written.returncode == 0, written.stdout + written.stderr
            output = cli.run_ok("object", "run", self.CHECK_CLASS)["output"]
        finally:
            cli.run("object", "delete", uri)
        values = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
        assert values["SNWD_SO-SO_ID"] == "0500000728", output
        assert values["GS_BUYER-COMPANY_NAME"] == "OffiPOR", output
        assert values["SNWD_SO-GROSS_AMOUNT"] == "785.47", output
        assert values["SNWD_SO-CURRENCY_CODE"] == "USD", output
        assert values["SNWD_SO-LIFECYCLE_STATUS"] == "C", output

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)
        assert _entry(result, "PROG", self.PROG)["action"] == "unchanged"

    def test_changed_visible_length_rewrites_only_that_screen(self, cli, adt, tmp_path):
        xml = _git_copy(self.STEM, tmp_path)
        content = xml.read_text(encoding="utf-8")
        old = ("<NAME>GS_BUYER-COMPANY_NAME</NAME>\n       <TEXT>" + "_" * 80 + "</TEXT>\n"
               "       <ROLLING>X</ROLLING>\n"
               "       <LINE>003</LINE>\n       <COLUMN>020</COLUMN>\n"
               "       <LENGTH>080</LENGTH>\n       <VISLENGTH>040</VISLENGTH>")
        assert old in content
        xml.write_text(content.replace(old, old.replace("<VISLENGTH>040", "<VISLENGTH>030")),
                       encoding="utf-8")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["screen 0200"]
        assert adt.inactive(self.PROG) == []
        status = cli.run_ok("abapgit", "status", str(xml), "--package", PACKAGE)
        assert _entry(status, "PROG", self.PROG)["action"] == "unchanged"
        # Back to the fixture (the change is committed first, so the version
        # in the system is in git): again only that screen.
        _commit(tmp_path)
        shutil.copy(self.XML, xml)
        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)
        assert _entry(result, "PROG", self.PROG)["parts"] == ["screen 0200"]

    def test_uncommitted_screen_change_by_unknown_changer_is_refused(self, cli, adt, tmp_path):
        # The system holds a screen version that is in no git history (as if
        # edited in SE51, which records no last changer): refused, exit 6.
        xml = _copy(self.STEM, tmp_path)
        content = xml.read_text(encoding="utf-8")
        xml.write_text(content.replace("<VISLENGTH>040", "<VISLENGTH>035", 1), encoding="utf-8")
        cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE, "--force")

        result = cli.run("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        assert result.returncode == 6, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "PROG", self.PROG)
        assert entry["action"] == "refused"
        assert "unknown user" in entry["message"]
        restored = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE,
                              "--force")
        assert _entry(restored, "PROG", self.PROG)["action"] == "updated"

    def test_changed_title_rewrites_only_the_cua(self, cli, adt, tmp_path):
        xml = _copy(self.STEM, tmp_path)
        content = xml.read_text(encoding="utf-8")
        assert "<TEXT>Display Sales Order &amp;1</TEXT>" in content
        xml.write_text(content.replace("<TEXT>Display Sales Order &amp;1</TEXT>",
                                       "<TEXT>Sales Order &amp;1: Overview</TEXT>"),
                       encoding="utf-8")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["CUA"]
        assert adt.inactive(self.PROG) == []
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)
        assert _entry(result, "PROG", self.PROG)["parts"] == ["CUA"]

    def test_export_matches_files_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        result = cli.run_ok("abapgit", "export", str(tmp_path), "--object",
                            f"PROG:{self.PROG}", "--package", PACKAGE)

        assert _entry(result, "PROG", self.PROG)["action"] == "exported"
        exported = sorted(p.name for p in (tmp_path / "src").iterdir())
        assert exported == [p.name for p in _files(self.STEM)]
        for name in exported:
            assert (tmp_path / "src" / name).read_bytes() == (SRC / name).read_bytes(), name

    def test_broken_flow_logic_fails_with_sap_error(self, cli, adt, tmp_path):
        # RPY_DYNPRO_INSERT generates the screen: a field that is not on the
        # screen is rejected when it is saved. (An unknown MODULE is not: SAP
        # activates such a screen and only the runtime / extended check sees it.)
        xml = _git_copy(self.STEM, tmp_path)
        flow = tmp_path / f"{self.STEM}.prog.screen_0100.abap"
        flow.write_text(flow.read_text().replace(
            "  MODULE status_0100.\n", "  MODULE status_0100.\n  FIELD gv_unknown MODULE check_so_id.\n"))

        result = cli.run("abapgit", "deploy", str(xml), "--package", PACKAGE)

        output = result.stdout + result.stderr
        assert result.returncode == 5, output
        message = _entry(json.loads(result.stdout), "PROG", self.PROG)["message"]
        assert 'Input/output field "GV_UNKNOWN" not defined' in message
        assert "Screen 0100" in message
        assert adt.inactive(self.PROG) == []
        # RPY_DYNPRO_INSERT saves the screen before it generates it, so the
        # rejected flow logic is in the system (saved, not generated) - as
        # after a failed save in SE51. A plain redeploy of the fixed file is
        # therefore refused as drift; once the attempt is committed, the git
        # history knows that version and the fix goes through.
        refused = cli.run("abapgit", "deploy", str(self.XML), "--package", PACKAGE)
        assert refused.returncode == 6, refused.stdout + refused.stderr
        _commit(tmp_path, "broken attempt")
        shutil.copy(SRC / flow.name, flow)
        fixed = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)
        entry = _entry(fixed, "PROG", self.PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["screen 0100"]

    def test_broken_source_fails_activation_with_sap_error(self, cli, adt, tmp_path):
        xml = _copy(self.STEM, tmp_path)
        abap = tmp_path / f"{self.STEM}.prog.abap"
        abap.write_text(abap.read_text().replace("  SET TITLEBAR 'T0100'.\n",
                                                 "  SET TITLEBAR 'T0100'.\n  gv_missing = 1.\n"))

        result = cli.run("abapgit", "deploy", str(xml), "--package", PACKAGE)

        output = result.stdout + result.stderr
        assert result.returncode == 5, output
        assert "GV_MISSING" in output.upper()
        # Saved but not activated, exactly like SE38 after a failed activation.
        assert adt.inactive(self.PROG) != []

    def test_redeploy_after_failure_repairs_inactive_version(self, cli, adt):
        # The files equal the active version, yet the deploy must overwrite
        # and activate the broken inactive source left by the failed deploy.
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "updated"
        assert "source" in entry["parts"]
        assert adt.inactive(self.PROG) == []
        status, source = adt.source(self.PROG)
        assert "gv_missing" not in source

    def test_delete_removes_program(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.XML), "--package", PACKAGE)

        status, body = adt.source(self.PROG)
        assert status == 404, body[:500]
        result = cli.run_ok("abapgit", "status", str(self.XML), "--package", PACKAGE)
        assert _entry(result, "PROG", self.PROG)["action"] == "created"


@pytest.mark.abapgit_deploy
class TestReport:
    """ZERPL_PO_REPORT: executable report with selection screen and text pool."""

    PROG = "ZERPL_PO_REPORT"
    STEM = "zerpl_po_report"
    XML = SRC / "zerpl_po_report.prog.xml"

    def test_deploy_creates_report_with_text_pool(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.XML), "--package", PACKAGE, "--force")

        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "created"
        assert entry["parts"] == ["source", "PROGDIR", "TPOOL"]
        selections = adt.text_elements(self.PROG, "selections")
        assert "S_BP_ID =Supplier" in selections
        # P_STATUS refers to the data element: its text comes from the DDIC.
        assert "@DDICReference\nP_STATUS=Lifecycle Status" in selections
        symbols = adt.text_elements(self.PROG, "symbols")
        assert "B01=Selection" in symbols
        assert adt.inactive(self.PROG) == []

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)
        assert _entry(result, "PROG", self.PROG)["action"] == "unchanged"

    def test_changed_text_symbol_rewrites_only_the_text_pool(self, cli, adt, tmp_path):
        xml = _copy(self.STEM, tmp_path)
        content = xml.read_text(encoding="utf-8")
        xml.write_text(content.replace("<ENTRY>Selection</ENTRY>",
                                       "<ENTRY>Purchase order selection</ENTRY>"),
                       encoding="utf-8")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["TPOOL"]
        assert "B01=Purchase order selection" in adt.text_elements(self.PROG, "symbols")
        assert adt.inactive(self.PROG) == []

    def test_changed_source_goes_through_adt(self, cli, adt, tmp_path):
        xml = _copy(self.STEM, tmp_path)
        abap = tmp_path / f"{self.STEM}.prog.abap"
        abap.write_text(abap.read_text().replace("DEFAULT 1000", "DEFAULT 500"))

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", self.PROG)
        assert entry["parts"] == ["source", "TPOOL"]  # TPOOL: back from the previous test
        status, source = adt.source(self.PROG)
        assert "DEFAULT 500" in source
        assert adt.inactive(self.PROG) == []

    def test_export_matches_files_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"PROG:{self.PROG}",
                   "--package", PACKAGE)

        for path in _files(self.STEM):
            assert (tmp_path / "src" / path.name).read_bytes() == path.read_bytes(), path.name

    def test_delete_removes_report(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.XML), "--package", PACKAGE)
        status, body = adt.source(self.PROG)
        assert status == 404, body[:500]
