"""Integration tests for `erpl-adt abapgit` with transactions (TRAN).

Transactions have no ADT REST endpoint for writing. erpl-adt deploys them the
way SE93 and abapGit's ZCL_ABAPGIT_OBJECT_TRAN do: RPY_TRANSACTION_INSERT
(which records the object on the transport itself through RS_CORR_INSERT and
locks it while writing) and RPY_TRANSACTION_DELETE. An update is a delete +
insert -- that is what abapGit does too, since there is no update API.

Three realistic transactions of the demo order application:
- ZERPL_VA03     dialog transaction  -> ZERPL_SO_DISPLAY screen 0100
- ZERPL_ME2L     report transaction  -> ZERPL_PO_REPORT (selection screen 1000)
- ZERPL_SO_LIST  parameter transaction: SE16 on SNWD_SO, first screen skipped

The programs are owned by the PROG slice; when they are not there yet, this
module creates minimal stand-ins (and removes only the ones it created).

Independent checks: the ADT repository search (`erpl-adt search --type TRAN`)
and a read-only SELECT on TSTC/TSTCC/TSTCP in HANA.
"""

import json
import shutil
import subprocess
import warnings
from pathlib import Path

import pytest

from hana_oracle import oracle as hana_oracle

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"

VA03 = SRC / "zerpl_va03.tran.xml"
ME2L = SRC / "zerpl_me2l.tran.xml"
SO_LIST = SRC / "zerpl_so_list.tran.xml"

STAND_INS = {
    "ZERPL_SO_DISPLAY": (
        "REPORT zerpl_so_display.\n"
        "\n"
        "DATA ok_code TYPE sy-ucomm.\n"
        "\n"
        "START-OF-SELECTION.\n"
        "  CALL SCREEN 100.\n"
    ),
    "ZERPL_PO_REPORT": (
        "REPORT zerpl_po_report.\n"
        "\n"
        "PARAMETERS p_supp TYPE snwd_bpa-bp_id.\n"
        "\n"
        "START-OF-SELECTION.\n"
        "  WRITE / p_supp.\n"
    ),
}


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _hana(sql):
    """Read-only SELECT in the SAP system's HANA (independent of erpl-adt)."""
    return hana_oracle().query(sql.replace("SAPA4H.", hana_oracle().schema + "."))


def _tstc(tcode):
    rows = _hana(
        "SELECT C.PGMNA, C.DYPNO, C.CINFO, G.S_WIN32, G.S_WEBGUI, G.S_PLATIN "
        "FROM SAPA4H.TSTC C LEFT JOIN SAPA4H.TSTCC G ON G.TCODE = C.TCODE "
        f"WHERE C.TCODE = '{tcode}'")
    if not rows:
        return None
    pgmna, dypno, cinfo, win32, webgui, platin = (v.strip('"') for v in rows[0])
    return {"PGMNA": pgmna, "DYPNO": dypno, "CINFO": cinfo, "S_WIN32": win32,
            "S_WEBGUI": webgui, "S_PLATIN": platin}


def _tstcp(tcode):
    rows = _hana(f"SELECT PARAM FROM SAPA4H.TSTCP WHERE TCODE = '{tcode}'")
    return rows[0][0].strip('"').rstrip() if rows else None


def _search(cli, tcode):
    hits = cli.run_ok("search", tcode, "--type", "TRAN")
    return [h for h in hits if h["name"] == tcode]


@pytest.fixture(scope="module")
def programs(cli):
    """Make sure the called programs exist; create stand-ins only if needed."""
    created = []
    for name, source in STAND_INS.items():
        uri = f"/sap/bc/adt/programs/programs/{name.lower()}"
        if cli.run("object", "read", uri).returncode != 0:
            cli.run_ok("object", "create", "--type", "PROG/P", "--name", name,
                       "--package", PACKAGE, "--description", f"erpl demo stand-in {name}")
            created.append(uri)
            src_file = Path(__file__).parent / f".{name.lower()}.abap"
            src_file.write_text(source)
            try:
                written = cli.run("source", "write", f"{uri}/source/main", "--file",
                                  str(src_file), "--activate")
                assert written.returncode == 0, written.stderr
            finally:
                src_file.unlink()
        # The dialog transaction calls screen 0100. RPY_TRANSACTION_INSERT
        # does not check that the screen exists (SE93 only warns), so a
        # failure to create the stand-in screen does not block the TRAN tests.
        if name == "ZERPL_SO_DISPLAY" and cli.run(
                "screen", "read", "--program", name, "--screen", "0100",
                "--package", PACKAGE).returncode != 0:
            made = cli.run("screen", "create", "--program", name, "--screen", "0100",
                           "--package", PACKAGE, "--fields", "SO_ID:Sales Order:2:2:10")
            if made.returncode != 0:
                warnings.warn(f"stand-in screen {name} 0100 not created: {made.stdout[-300:]}")
    yield
    for uri in created:
        cli.run("object", "delete", uri)


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _git_copy(path, target):
    """Copy a transaction file into a fresh git work tree, committed.

    SAP records no last changer for a transaction, so a deploy that changes
    one is allowed only when the version in the system is in the file's git
    history (as in a real repository); a plain temp copy has none."""
    copy = target / path.name
    shutil.copy(path, copy)
    _git(target, "init", "-q")
    _git(target, "add", ".")
    _git(target, "commit", "-qm", "fixture")
    return copy


def _commit(repo):
    _git(repo, "add", ".")
    _git(repo, "commit", "-qm", "change")


@pytest.mark.abapgit_deploy
class TestTransactions:

    @pytest.mark.parametrize("path", [VA03, ME2L, SO_LIST], ids=lambda p: p.stem)
    def test_deploy_creates_transaction(self, cli, programs, path):
        tcode = path.name.split(".")[0].upper()
        cli.run("abapgit", "delete", str(path), "--package", PACKAGE, "--force")
        assert _tstc(tcode) is None

        result = cli.run_ok("abapgit", "deploy", str(path), "--package", PACKAGE)

        assert _entry(result, "TRAN", tcode)["action"] == "created"
        hits = _search(cli, tcode)
        assert hits and hits[0]["type"].startswith("TRAN")

    def test_dialog_transaction_in_tstc(self, cli):
        row = _tstc("ZERPL_VA03")
        assert row == {"PGMNA": "ZERPL_SO_DISPLAY", "DYPNO": "0100", "CINFO": "0x00",
                       "S_WIN32": "X", "S_WEBGUI": "1", "S_PLATIN": "X"}
        assert _search(cli, "ZERPL_VA03")[0]["description"] == "Display Sales Order"

    def test_report_transaction_in_tstc(self, cli):
        row = _tstc("ZERPL_ME2L")
        assert row["PGMNA"] == "ZERPL_PO_REPORT"
        assert row["DYPNO"] == "1000"
        assert row["CINFO"] == "0x80"
        assert _search(cli, "ZERPL_ME2L")[0]["description"] == "Purchase Orders by Supplier"

    def test_parameter_transaction_calls_se16(self, cli):
        row = _tstc("ZERPL_SO_LIST")
        assert row["CINFO"] == "0x02"
        assert _tstcp("ZERPL_SO_LIST") == "/*SE16 DATABROWSE-TABLENAME=SNWD_SO;"

    @pytest.mark.parametrize("path", [VA03, ME2L, SO_LIST], ids=lambda p: p.stem)
    def test_redeploy_unchanged_changes_nothing(self, cli, path):
        tcode = path.name.split(".")[0].upper()
        result = cli.run_ok("abapgit", "deploy", str(path), "--package", PACKAGE)
        assert _entry(result, "TRAN", tcode)["action"] == "unchanged"

    @pytest.mark.parametrize("path", [VA03, ME2L, SO_LIST], ids=lambda p: p.stem)
    def test_export_matches_file_byte_for_byte(self, cli, tmp_path, path):
        tcode = path.name.split(".")[0].upper()
        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"TRAN:{tcode}",
                   "--package", PACKAGE)
        exported = tmp_path / "src" / path.name
        assert exported.read_bytes() == path.read_bytes()

    def test_changed_text_updates_only_the_text(self, cli, tmp_path):
        changed = _git_copy(VA03, tmp_path)
        changed.write_bytes(VA03.read_bytes().replace(
            b"<TTEXT>Display Sales Order</TTEXT>", b"<TTEXT>Display Sales Order (EPM)</TTEXT>"))

        result = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

        entry = _entry(result, "TRAN", "ZERPL_VA03")
        assert entry["action"] == "updated"
        assert entry["parts"] == ["TSTCT"]
        assert _search(cli, "ZERPL_VA03")[0]["description"] == "Display Sales Order (EPM)"
        # the rest of the transaction survived the delete + insert
        assert _tstc("ZERPL_VA03")["PGMNA"] == "ZERPL_SO_DISPLAY"

        _commit(tmp_path)
        shutil.copy(VA03, changed)
        back = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)
        assert _entry(back, "TRAN", "ZERPL_VA03")["action"] == "updated"

    def test_change_not_in_git_is_refused(self, cli, tmp_path):
        # A version in the system that no git history knows (as if edited in
        # SE93, which records no last changer) is not overwritten: exit 6.
        edited = tmp_path / VA03.name
        edited.write_bytes(VA03.read_bytes().replace(
            b"<TTEXT>Display Sales Order</TTEXT>", b"<TTEXT>Edited in SE93</TTEXT>"))
        cli.run_ok("abapgit", "deploy", str(edited), "--package", PACKAGE, "--force")

        result = cli.run("abapgit", "deploy", str(VA03), "--package", PACKAGE)

        assert result.returncode == 6, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "TRAN", "ZERPL_VA03")
        assert entry["action"] == "refused"
        assert "unknown user" in entry["message"]
        assert _search(cli, "ZERPL_VA03")[0]["description"] == "Edited in SE93"
        cli.run_ok("abapgit", "deploy", str(VA03), "--package", PACKAGE, "--force")

    def test_changed_gui_flags_update(self, cli, tmp_path):
        changed = _git_copy(ME2L, tmp_path)
        changed.write_bytes(ME2L.read_bytes().replace(b"    <S_PLATIN>X</S_PLATIN>\n", b""))

        result = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

        entry = _entry(result, "TRAN", "ZERPL_ME2L")
        assert entry["action"] == "updated"
        assert entry["parts"] == ["TSTCC"]
        assert _tstc("ZERPL_ME2L")["S_PLATIN"] in ("", "?")
        _commit(tmp_path)
        shutil.copy(ME2L, changed)
        cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

    def test_sap_transaction_is_refused_before_anything_is_written(self, cli, tmp_path):
        se38 = tmp_path / "se38.tran.xml"
        se38.write_bytes(VA03.read_bytes().replace(b"ZERPL_VA03", b"SE38"))
        before = _tstc("SE38")

        result = cli.run("abapgit", "deploy", str(se38), "--package", PACKAGE)

        assert result.returncode != 0
        assert "SE38" in (result.stdout + result.stderr)
        assert _tstc("SE38") == before

    def test_unsupported_parts_are_refused_not_dropped(self, cli, tmp_path):
        # An authorization-object check (TSTCA, CINFO x'04') can only be written
        # to the tables directly -- erpl-adt refuses instead of dropping it.
        with_auth = tmp_path / VA03.name
        with_auth.write_bytes(VA03.read_bytes().replace(
            b"   </TSTCT>\n",
            b"   </TSTCT>\n   <AUTHORIZATIONS>\n    <TSTCA>\n     <TCODE>ZERPL_VA03</TCODE>\n"
            b"     <OBJCT>S_TCODE</OBJCT>\n     <FIELD>TCD</FIELD>\n     <VALUE>ZERPL_VA03</VALUE>\n"
            b"    </TSTCA>\n   </AUTHORIZATIONS>\n"))

        result = cli.run("abapgit", "deploy", str(with_auth), "--package", PACKAGE)

        assert result.returncode != 0
        assert "AUTHORIZATIONS" in (result.stdout + result.stderr)
        assert _tstc("ZERPL_VA03")["PGMNA"] == "ZERPL_SO_DISPLAY"  # untouched

    @pytest.mark.parametrize("path", [VA03, ME2L, SO_LIST], ids=lambda p: p.stem)
    def test_delete_removes_transaction(self, cli, path):
        tcode = path.name.split(".")[0].upper()
        result = cli.run_ok("abapgit", "delete", str(path), "--package", PACKAGE)
        assert _entry(result, "TRAN", tcode)["action"] == "deleted"
        assert _tstc(tcode) is None
        assert _search(cli, tcode) == []
