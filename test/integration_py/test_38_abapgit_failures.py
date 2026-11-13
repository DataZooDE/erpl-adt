"""Integration tests for `erpl-adt abapgit` when part of a deploy fails.

A deploy writes all programs of a repository in one engine run and activates
them together (RS_WORKING_OBJECTS_ACTIVATE); what activation leaves inactive
fails the object it belongs to, and objects that depend on a failed one are
skipped (their operations are guarded by their prerequisites' owners). A
transaction update is delete + insert (RPY_TRANSACTION_*, as in abapGit and
SE93); when the insert fails, the previous version is inserted again.

Objects: ZERPL_T4_* in $TMP, created and removed by this module.
Independent checks: PROGDIR / TSTC in HANA (hana_oracle.py) and the ADT
repository search.
"""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from hana_oracle import oracle as hana_oracle

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
PACKAGE = "$TMP"

OK = "ZERPL_T4_OK"
BROKEN = "ZERPL_T4_BROKEN"
TRAN = "ZERPL_T4_TRAN"        # report transaction -> BROKEN
OK_TRAN = "ZERPL_T4_OKTRAN"   # report transaction -> OK

BOM = "﻿"


def _prog_xml(name, title):
    return (f'{BOM}<?xml version="1.0" encoding="utf-8"?>\n'
            '<abapGit version="v1.0.0" serializer="LCL_OBJECT_PROG" serializer_version="v1.0.0">\n'
            ' <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">\n'
            '  <asx:values>\n'
            '   <PROGDIR>\n'
            f'    <NAME>{name}</NAME>\n'
            '    <SUBC>1</SUBC>\n'
            '    <RLOAD>E</RLOAD>\n'
            '    <FIXPT>X</FIXPT>\n'
            '    <UCCHECK>X</UCCHECK>\n'
            '   </PROGDIR>\n'
            '   <TPOOL>\n'
            '    <item>\n'
            '     <ID>R</ID>\n'
            f'     <ENTRY>{title}</ENTRY>\n'
            f'     <LENGTH>{len(title)}</LENGTH>\n'
            '    </item>\n'
            '   </TPOOL>\n'
            '  </asx:values>\n'
            ' </asx:abap>\n'
            '</abapGit>\n')


def _tran_xml(tcode, program, text):
    return (f'{BOM}<?xml version="1.0" encoding="utf-8"?>\n'
            '<abapGit version="v1.0.0" serializer="LCL_OBJECT_TRAN" serializer_version="v1.0.0">\n'
            ' <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">\n'
            '  <asx:values>\n'
            '   <TSTC>\n'
            f'    <TCODE>{tcode}</TCODE>\n'
            f'    <PGMNA>{program}</PGMNA>\n'
            '    <DYPNO>1000</DYPNO>\n'
            '    <CINFO>gA==</CINFO>\n'
            '   </TSTC>\n'
            '   <TSTCC>\n'
            f'    <TCODE>{tcode}</TCODE>\n'
            '    <S_WEBGUI>1</S_WEBGUI>\n'
            '    <S_WIN32>X</S_WIN32>\n'
            '    <S_PLATIN>X</S_PLATIN>\n'
            '   </TSTCC>\n'
            '   <TSTCT>\n'
            '    <SPRSL>E</SPRSL>\n'
            f'    <TCODE>{tcode}</TCODE>\n'
            f'    <TTEXT>{text}</TTEXT>\n'
            '   </TSTCT>\n'
            '  </asx:values>\n'
            ' </asx:abap>\n'
            '</abapGit>\n')


OK_SOURCE = ("REPORT zerpl_t4_ok.\n"
             "\n"
             "PARAMETERS p_max TYPE i DEFAULT 10.\n"
             "\n"
             "START-OF-SELECTION.\n"
             "  WRITE / p_max.\n")
BROKEN_SOURCE = ("REPORT zerpl_t4_broken.\n"
                 "\n"
                 "PARAMETERS p_max TYPE i DEFAULT 10.\n"
                 "\n"
                 "START-OF-SELECTION.\n"
                 "  WRITE / gv_missing.\n")


def _write(path, text):
    path.write_bytes(text.encode("utf-8"))


def _repo(root, objects):
    """An abapGit repository (git work tree) with `objects`: name -> files."""
    repo = root
    (repo / "src").mkdir(parents=True)
    shutil.copy(FIXTURES / ".abapgit.xml", repo / ".abapgit.xml")
    for files in objects.values():
        for name, text in files.items():
            _write(repo / "src" / name, text)
    for args in (["init", "-q"], ["add", "."], ["commit", "-qm", "fixture"]):
        subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                        "-c", "user.email=test@erpl.invalid", *args],
                       check=True, capture_output=True)
    return repo


def _prog(name, title, source):
    return {f"{name.lower()}.prog.xml": _prog_xml(name, title),
            f"{name.lower()}.prog.abap": source}


def _tran(tcode, program, text):
    return {f"{tcode.lower()}.tran.xml": _tran_xml(tcode, program, text)}


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _hana(sql):
    return hana_oracle().query(sql)


def _pending(name):
    """Whether the program has an inactive version waiting (DWINACTIV)."""
    rows = _hana(f"SELECT OBJECT FROM {hana_oracle().table('DWINACTIV')} "
                 f"WHERE OBJ_NAME = '{name}'")
    return bool(rows)


def _source_states(name):
    """States of the program's source in REPOSRC (A active, I inactive).
    PROGDIR is no oracle: RPY_PROGRAM_INSERT writes an active directory entry
    even for a program saved inactive."""
    rows = _hana(f"SELECT R3STATE FROM {hana_oracle().table('REPOSRC')} "
                 f"WHERE PROGNAME = '{name}'")
    return {r[0] for r in rows}


def _tstc_program(tcode):
    rows = _hana(f"SELECT PGMNA FROM {hana_oracle().table('TSTC')} WHERE TCODE = '{tcode}'")
    return rows[0][0].strip() if rows else None


def _cleanup(cli, tmp_path_factory):
    """Removes every ZERPL_T4_* object this module may have left."""
    files = {**_prog(OK, "x", OK_SOURCE), **_prog(BROKEN, "x", BROKEN_SOURCE),
             **_tran(TRAN, BROKEN, "x"), **_tran(OK_TRAN, OK, "x")}
    repo = _repo(tmp_path_factory.mktemp("t4_cleanup"), {"all": files})
    cli.run("abapgit", "delete", str(repo), "--package", PACKAGE, "--force")


@pytest.fixture(scope="module")
def clean(cli, tmp_path_factory):
    _cleanup(cli, tmp_path_factory)
    yield
    _cleanup(cli, tmp_path_factory)


@pytest.mark.abapgit_deploy
class TestPartialFailure:
    """One deploy: a valid program, a program with a syntax error, and a
    transaction that calls the broken one."""

    def test_one_failure_fails_its_object_and_skips_its_dependent(self, cli, clean,
                                                                  tmp_path):
        repo = _repo(tmp_path / "repo", {
            OK: _prog(OK, "Valid report", OK_SOURCE),
            BROKEN: _prog(BROKEN, "Broken report", BROKEN_SOURCE),
            TRAN: _tran(TRAN, BROKEN, "Calls the broken report"),
        })

        result = cli.run("abapgit", "deploy", str(repo), "--package", PACKAGE)

        assert result.returncode == 5, result.stdout + result.stderr
        data = json.loads(result.stdout)
        assert data["exit_code"] == 5
        assert _entry(data, "PROG", OK)["action"] == "created", data
        broken = _entry(data, "PROG", BROKEN)
        assert broken["action"] == "failed", broken
        assert broken["category"] == "activation", broken
        assert broken["exit_code"] == 5
        assert "GV_MISSING" in broken["message"].upper(), broken  # SAP's own message
        tran = _entry(data, "TRAN", TRAN)
        assert tran["action"] == "skipped", tran
        assert tran["message"] == f"dependency PROG {BROKEN} failed", tran

        # Independently: A is active, B has only its inactive version, the
        # transaction was never written.
        assert _source_states(OK) == {"A"}, _source_states(OK)
        assert not _pending(OK)
        assert "I" in _source_states(BROKEN), _source_states(BROKEN)
        assert _pending(BROKEN)
        assert _tstc_program(TRAN) is None
        found = cli.run_ok("search", TRAN, "--type", "TRAN")
        assert all(o.get("name") != TRAN for o in found), found


@pytest.mark.abapgit_deploy
class TestTransactionRollback:
    """A transaction update that SAP rejects restores the previous version."""

    def test_failed_update_restores_the_previous_version(self, cli, clean, tmp_path):
        """RPY_TRANSACTION_INSERT checks little (a missing program is accepted -
        measured on A4H), but its RS_CORR_INSERT refuses a package that does
        not exist. Before, the update's delete had already removed the
        transaction by then."""
        repo = _repo(tmp_path / "repo", {
            OK: _prog(OK, "Valid report", OK_SOURCE),
            OK_TRAN: _tran(OK_TRAN, OK, "Valid report transaction"),
        })
        cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert _tstc_program(OK_TRAN) == OK

        bad = tmp_path / "bad" / f"{OK_TRAN.lower()}.tran.xml"
        bad.parent.mkdir()
        _write(bad, _tran_xml(OK_TRAN, OK, "Changed text, wrong package"))
        result = cli.run("abapgit", "deploy", str(bad), "--package", "$ZERPL_T4_NOPKG",
                         "--force")

        assert result.returncode != 0, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "TRAN", OK_TRAN)
        assert entry["action"] == "failed", entry
        assert "previous version was restored" in entry["message"], entry
        # Still there, unchanged, in its package.
        assert _tstc_program(OK_TRAN) == OK
        rows = _hana(f"SELECT DEVCLASS FROM {hana_oracle().table('TADIR')} WHERE "
                     f"PGMID = 'R3TR' AND OBJECT = 'TRAN' AND OBJ_NAME = '{OK_TRAN}'")
        assert rows == [["$TMP"]], rows
        status = cli.run_ok("abapgit", "status", str(repo), "--package", PACKAGE)
        assert _entry(status, "TRAN", OK_TRAN)["action"] == "unchanged", status
