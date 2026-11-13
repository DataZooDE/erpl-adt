"""Integration tests for `erpl-adt abapgit` with native dynpros (NAT_HEADER).

Some screens cannot be described in the external dynpro format that
RPY_DYNPRO_READ / RPY_DYNPRO_INSERT speak - in particular a dynpro splitter
control (CL_DYNPRO_SPLITTER): the external format shows it as a LOOP
container with a SPLITTER field, and cannot write it back. abapGit therefore
stores such a screen in SAP's internal format: next to HEADER it writes
NAT_HEADER (D020S), NAT_FIELDS (D021S) and NAT_TEXTS (D021T), and no
CONTAINERS / FIELDS. It reads them with RPY_DYNPRO_READ_NATIVE and writes them
with RPY_DYNPRO_INSERT_NATIVE (which locks the screen and records it on the
transport itself) - except NAT_TEXTS, which abapGit inserts into D021T
directly. erpl-adt writes NAT_HEADER / NAT_FIELDS with the same function
module and refuses NAT_TEXTS (no workbench API writes them).

Realistic case: ZERPL_SO_SPLIT, a sales order cockpit - splitter screen 0100
with the order list (subscreen 0110) left and the items (0120) right.
Fixture: fixtures/abapgit/erpl_native/ (a separate folder: erpl_demo is
unchanged).

Independent checks: the dynpro tables (HANA, read-only: D020S generation
date, D020T description), `erpl-adt screen read` (its own helper class) and
ADT's inactive objects.
"""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

SRC = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_native"
PACKAGE = "$TMP"
PROG = "ZERPL_SO_SPLIT"
STEM = "zerpl_so_split"


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _files():
    return sorted(p for p in SRC.iterdir() if p.name.startswith(STEM + "."))


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
    return [[c.strip('"') for c in line.split(",")]
            for line in proc.stdout.strip().splitlines() if line]


def _screens():
    """{screen: description} of the program's dynpros (D020S + D020T)."""
    rows = _hana("SELECT S.DNUM, T.DTXT FROM SAPA4H.D020S S LEFT JOIN SAPA4H.D020T T "
                 "ON T.PROG = S.PROG AND T.DYNR = S.DNUM AND T.LANG = 'E' "
                 f"WHERE S.PROG = '{PROG}'")
    return {r[0]: r[1] for r in rows}


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _git_copy(target):
    """The fixture in a fresh git work tree (dynpros record no last changer:
    a change is only deployed when the system version is in git history)."""
    for path in _files():
        shutil.copy(path, target / path.name)
    _git(target, "init", "-q")
    _git(target, "add", ".")
    _git(target, "commit", "-qm", "fixture")
    return target / f"{STEM}.prog.xml"


class _Adt:
    def __init__(self, cfg):
        import base64
        self.base = f"http://{cfg['host']}:{cfg['port']}"
        token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
        self.headers = {"Authorization": f"Basic {token}", "sap-client": cfg["client"],
                        "Accept": "*/*"}

    def inactive(self, program):
        import re
        import urllib.request
        req = urllib.request.Request(self.base + "/sap/bc/adt/activation/inactiveobjects",
                                     headers=self.headers)
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode("utf-8", "replace")
        names = re.findall(r'adtcore:name="([^"]*)"', body)
        return [n for n in names if n.split()[0].upper() == program]


@pytest.fixture(scope="module")
def adt(sap_config):
    return _Adt(sap_config)


@pytest.fixture(scope="module", autouse=True)
def cleanup(cli):
    xml = SRC / f"{STEM}.prog.xml"
    cli.run("abapgit", "delete", str(xml), "--package", PACKAGE, "--force")
    yield
    cli.run("abapgit", "delete", str(xml), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
class TestNativeDynpro:

    XML = SRC / f"{STEM}.prog.xml"

    def test_deploy_creates_the_splitter_screen(self, cli, adt):
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "created", entry
        assert entry["parts"] == ["source", "PROGDIR", "screen 0100", "screen 0110",
                                  "screen 0120", "CUA", "TPOOL"]
        assert _screens() == {"0100": "Sales order cockpit", "0110": "Sales orders",
                              "0120": "Items"}
        # Generated (activated): the generation date is set, nothing inactive.
        generated = _hana(f"SELECT DNUM, DGEN FROM SAPA4H.D020S WHERE PROG = '{PROG}'")
        assert all(dgen.strip() not in ("", "00000000") for _, dgen in generated), generated
        assert adt.inactive(PROG) == []

    def test_screen_read_shows_the_splitter(self, cli):
        screen = cli.run_ok("screen", "read", "--program", PROG, "--screen", "0100",
                            "--package", PACKAGE)
        names = {f["name"] for f in screen["fields"]}
        assert "SPLITTER" in names, screen["fields"]
        assert (screen["lines"], screen["columns"]) == (27, 120)

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE)
        assert _entry(result, "PROG", PROG)["action"] == "unchanged"

    def test_changed_description_rewrites_only_the_native_screen(self, cli, adt, tmp_path):
        xml = _git_copy(tmp_path)
        content = xml.read_text(encoding="utf-8")
        assert content.count("<DESCRIPT>Sales order cockpit</DESCRIPT>") == 1
        xml.write_text(content.replace("<DESCRIPT>Sales order cockpit</DESCRIPT>",
                                       "<DESCRIPT>Sales order cockpit: orders | items</DESCRIPT>"),
                       encoding="utf-8")

        result = cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE)

        entry = _entry(result, "PROG", PROG)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["screen 0100"]
        assert _screens()["0100"] == "Sales order cockpit: orders | items"
        assert adt.inactive(PROG) == []
        status = cli.run_ok("abapgit", "status", str(xml), "--package", PACKAGE)
        assert _entry(status, "PROG", PROG)["action"] == "unchanged"

    def test_export_matches_files_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.XML), "--package", PACKAGE, "--force")

        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"PROG:{PROG}",
                   "--package", PACKAGE)

        exported = sorted(p.name for p in (tmp_path / "src").iterdir())
        assert exported == [p.name for p in _files()]
        for path in _files():
            assert (tmp_path / "src" / path.name).read_bytes() == path.read_bytes(), path.name

    def test_field_texts_of_a_native_screen_are_refused(self, cli, tmp_path):
        xml = _git_copy(tmp_path)
        content = xml.read_text(encoding="utf-8")
        texts = ("     <NAT_TEXTS>\n      <D021T>\n       <PROG>ZERPL_SO_SPLIT</PROG>\n"
                 "       <DYNR>0100</DYNR>\n       <LANG>E</LANG>\n"
                 "       <FLDN>%#AUTOTEXT001</FLDN>\n       <DTXT>Orders</DTXT>\n"
                 "      </D021T>\n     </NAT_TEXTS>\n")
        xml.write_text(content.replace("     </NAT_FIELDS>\n", "     </NAT_FIELDS>\n" + texts, 1),
                       encoding="utf-8")

        result = cli.run("abapgit", "deploy", str(xml), "--package", PACKAGE)

        assert result.returncode == 8, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "PROG", PROG)
        assert entry["action"] == "failed"
        assert "NAT_TEXTS" in entry["message"] and "D021T" in entry["message"]

    def test_delete_removes_program_and_screens(self, cli):
        result = cli.run_ok("abapgit", "delete", str(self.XML), "--package", PACKAGE)

        assert _entry(result, "PROG", PROG)["action"] == "deleted"
        assert _screens() == {}
        assert _hana(f"SELECT NAME FROM SAPA4H.PROGDIR WHERE NAME = '{PROG}'") == []
