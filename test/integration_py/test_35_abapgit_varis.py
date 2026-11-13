"""Integration tests for `erpl-adt abapgit` with report variants (VARIS).

abapGit stores the system variants of a report (names SAP&... / CUS&..., kept
in client 000 and transported with the program) in the VARIS section of
<name>.prog.xml: header flags (VARID), the screens the variant covers, the
attributes of each selection (VANZ), the values (RSPARAMSL_255) and the
variant text in the master language. erpl-adt writes them like abapGit's
ZCL_ABAPGIT_OBJECTS_PROGRAM: RS_VARIANT_DELETE + RS_CREATE_VARIANT_255 +
RS_CHANGE_CREATED_VARIANT_255, after the program is active (a variant is
checked against the generated selection screen). User variants (names
without SAP&/CUS&) are not part of abapGit's format and are refused.

Realistic case: the ME2L look-alike ZERPL_PO_REPORT ships a variant
CUS&OPEN_POS "Purchase orders in progress" (lifecycle status P, at most 200
items) - the kind of default selection a team keeps in git next to the report.

The fixture fixtures/abapgit/erpl_varis/zerpl_po_report.prog.xml is the
erpl_demo program plus that VARIS section; the erpl_demo files stay without
variants, and every test leaves the system at the erpl_demo version.

Every result is checked independently of the engine under test: the variant
tables in client 000 (HANA, read-only) and the variant's values as SAP's
variant API returns them to a separate console class.
"""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit"
DEMO = FIXTURES / "erpl_demo" / "src"
VARIS_XML = FIXTURES / "erpl_varis" / "zerpl_po_report.prog.xml"
PACKAGE = "$TMP"
PROG = "ZERPL_PO_REPORT"
STEM = "zerpl_po_report"
VARIANT = "CUS&OPEN_POS"
CHECK_CLASS = "ZERPL_VARIS_CHECK"

CHECK_SOURCE = """CLASS zerpl_varis_check DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zerpl_varis_check IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    " The values of variant CUS&OPEN_POS as SAP's variant API returns them.
    DATA values TYPE STANDARD TABLE OF rsparamsl_255 WITH DEFAULT KEY.
    DATA objects TYPE STANDARD TABLE OF vanz WITH DEFAULT KEY.
    CALL FUNCTION 'RS_VARIANT_CONTENTS_255'
      EXPORTING report = 'ZERPL_PO_REPORT' variant = 'CUS&OPEN_POS' execute_direct = abap_true
      TABLES valutab = values objects = objects
      EXCEPTIONS OTHERS = 1.
    out->write( |rc={ sy-subrc }| ).
    LOOP AT values INTO DATA(value).
      out->write( |{ value-selname }={ condense( val = value-low ) }| ).
    ENDLOOP.
  ENDMETHOD.
ENDCLASS.
"""


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _hana(sql):
    """Read-only SELECT in the A4H container's HANA (independent of erpl-adt)."""
    if shutil.which("docker") is None:
        pytest.skip("docker not available for the HANA cross-check")
    proc = subprocess.run(
        ["docker", "exec", "-u", "a4hadm", "-e", "HOME=/home/a4hadm", "a4h",
         "/usr/sap/A4H/hdbclient/hdbsql", "-U", "DEFAULT", "-a", "-x", sql],
        capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        pytest.skip(f"HANA not reachable: {proc.stderr[:200]}")
    return [line.split(",") for line in proc.stdout.strip().splitlines() if line]


def _variants():
    """System variants of the report in client 000: {variant: (environment, text)}."""
    rows = _hana(
        "SELECT V.VARIANT, V.ENVIRONMNT, T.VTEXT FROM SAPA4H.VARID V "
        "LEFT JOIN SAPA4H.VARIT T ON T.MANDT = V.MANDT AND T.REPORT = V.REPORT "
        "AND T.VARIANT = V.VARIANT AND T.LANGU = 'E' "
        f"WHERE V.MANDT = '000' AND V.REPORT = '{PROG}'")
    return {r[0].strip('"'): (r[1].strip('"'), r[2].strip('"')) for r in rows}


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _work(target, xml=VARIS_XML):
    """The report (erpl_demo source) with `xml`, committed in a fresh git work tree."""
    shutil.copy(DEMO / f"{STEM}.prog.abap", target / f"{STEM}.prog.abap")
    shutil.copy(xml, target / f"{STEM}.prog.xml")
    _git(target, "init", "-q")
    _git(target, "add", ".")
    _git(target, "commit", "-qm", "fixture")
    return target / f"{STEM}.prog.xml"


def _variant_values(cli, tmp_path):
    uri = f"/sap/bc/adt/oo/classes/{CHECK_CLASS.lower()}"
    cli.run("object", "delete", uri)
    cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", CHECK_CLASS,
               "--package", PACKAGE, "--description", "erpl-adt test: variant values")
    try:
        src = tmp_path / "check.abap"
        src.write_text(CHECK_SOURCE)
        written = cli.run("source", "write", CHECK_CLASS, "--file", str(src), "--activate")
        assert written.returncode == 0, written.stdout + written.stderr
        output = cli.run_ok("object", "run", CHECK_CLASS)["output"]
    finally:
        cli.run("object", "delete", uri)
    return dict(line.split("=", 1) for line in output.splitlines() if "=" in line)


@pytest.fixture(scope="module", autouse=True)
def demo_version(cli):
    """Start from - and always end at - the erpl_demo version (no variants)."""
    xml = DEMO / f"{STEM}.prog.xml"
    cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE, "--force")
    yield
    cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
class TestReportVariants:

    def test_deploy_adds_the_variant(self, cli, tmp_path):
        assert VARIANT not in _variants()
        xml = _work(tmp_path)

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == [f"variant {VARIANT}"]
        assert _variants()[VARIANT] == ("A", "Purchase orders in progress")
        values = _variant_values(cli, tmp_path)
        assert values["rc"] == "0"
        assert values["P_STATUS"] == "P"
        assert values["P_MAX"] == "200"

    def test_redeploy_unchanged_changes_nothing(self, cli, tmp_path):
        xml = _work(tmp_path)
        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)
        assert _entry(result, "PROG", PROG)["action"] == "unchanged"

    def test_changed_value_rewrites_only_the_variant(self, cli, tmp_path):
        xml = _work(tmp_path)
        content = xml.read_text(encoding="utf-8")
        assert content.count("<LOW>       200</LOW>") == 1
        xml.write_text(content.replace("<LOW>       200</LOW>", "<LOW>       100</LOW>"),
                       encoding="utf-8")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == [f"variant {VARIANT}"]
        assert _variant_values(cli, tmp_path)["P_MAX"] == "100"
        status = cli.run_ok("abapgit", "status", str(xml), "--package", PACKAGE)
        assert _entry(status, "PROG", PROG)["action"] == "unchanged"

    def test_export_matches_the_fixture_byte_for_byte(self, cli, tmp_path):
        work = tmp_path / "work"
        work.mkdir()
        cli.run_ok("abapgit", "deploy", str(_work(work)), "--package", PACKAGE)

        out = tmp_path / "out"
        cli.run_ok("abapgit", "export", str(out), "--object", f"PROG:{PROG}",
                   "--package", PACKAGE)

        assert (out / "src" / f"{STEM}.prog.xml").read_bytes() == VARIS_XML.read_bytes()
        assert (out / "src" / f"{STEM}.prog.abap").read_bytes() == \
            (DEMO / f"{STEM}.prog.abap").read_bytes()

    def test_removed_variant_is_deleted(self, cli, tmp_path):
        assert VARIANT in _variants()
        xml = _work(tmp_path, DEMO / f"{STEM}.prog.xml")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == [f"variant {VARIANT} (deleted)"]
        assert VARIANT not in _variants()

    def test_user_variant_is_refused(self, cli, tmp_path):
        xml = _work(tmp_path)
        xml.write_text(xml.read_text(encoding="utf-8").replace(
            "<VARIANT>CUS&amp;OPEN_POS</VARIANT>", "<VARIANT>OPEN_POS</VARIANT>"),
            encoding="utf-8")

        result = cli.run("abapgit", "deploy", str(xml), "--package", PACKAGE)

        assert result.returncode == 8, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "PROG", PROG)
        assert entry["action"] == "failed"
        assert "OPEN_POS" in entry["message"] and "SAP&" in entry["message"]
        assert "OPEN_POS" not in _variants()

    def test_delete_removes_program_and_variant(self, cli, tmp_path):
        xml = _work(tmp_path)
        cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)
        assert VARIANT in _variants()

        result = cli.run_ok("abapgit", "delete", str(xml), "--package", PACKAGE)

        assert _entry(result, "PROG", PROG)["action"] == "deleted"
        assert _variants() == {}
        assert _hana(f"SELECT NAME FROM SAPA4H.PROGDIR WHERE NAME = '{PROG}'") == []

    def test_new_program_is_created_with_its_variant(self, cli, tmp_path):
        xml = _work(tmp_path)

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "created"
        parts = entry["parts"]
        # The fixture also carries the German texts and the documentation.
        assert set(parts) == {"source", "PROGDIR", f"variant {VARIANT}", "TPOOL",
                              "I18N_TPOOL", "LONGTEXTS"}, parts
        assert _variants()[VARIANT] == ("A", "Purchase orders in progress")
