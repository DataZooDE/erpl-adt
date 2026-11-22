"""`abapgit deploy` of update function modules (issue #97).

An update module (abapGit: <UPDATE_TASK>1</UPDATE_TASK>, V1 with immediate
start) runs only when the SAP LUW is committed: BAPI-style code registers its
writes `CALL FUNCTION ... IN UPDATE TASK` and nothing is written before
COMMIT WORK, a ROLLBACK drops it. erpl-sap-demo's BAPI mirrors need exactly
that. Before #97 the deploy refused such a function group (exit 8).

ADT keeps the update flag in the module's metadata (fmodule:processingType
"update", fmodule:updateTaskKind "startImmediate", as SAP's STATUS_UPDATE has
them) and ignores it on create; the deploy sets it under the lock, as for the
RFC flag (#87). Verified independently of the code under test: TFDIR-UTASK
('1' = V1 update module) read with a plain SELECT in HANA, plus the export,
byte for byte.

Scenario: a posting function group with one update module is deployed from a
git work tree; a redeploy changes nothing; the export equals the files;
dropping UPDATE_TASK makes it a normal module again. An unsupported update
kind (2, V2) and a module that is both remote-enabled and an update module
are refused before anything is written.
"""

import json
import subprocess

import pytest

from hana_oracle import oracle as hana_oracle

PACKAGE = "$TMP"
FUGR = "ZERPL_UPD_FG"
FM = "Z_ERPL_UPD_POST"

GROUP_XML = """\ufeff<?xml version="1.0" encoding="utf-8"?>
<abapGit version="v1.0.0" serializer="LCL_OBJECT_FUGR" serializer_version="v1.0.0">
 <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
   <AREAT>erpl-adt test: update modules</AREAT>
   <INCLUDES>
    <SOBJ_NAME>LZERPL_UPD_FGTOP</SOBJ_NAME>
    <SOBJ_NAME>SAPLZERPL_UPD_FG</SOBJ_NAME>
   </INCLUDES>
   <FUNCTIONS>
    <item>
     <FUNCNAME>Z_ERPL_UPD_POST</FUNCNAME>
{flags}     <SHORT_TEXT>erpl-adt test: post a document in the update task</SHORT_TEXT>
     <IMPORT>
      <RSIMP>
       <PARAMETER>IV_DOCUMENT</PARAMETER>
       <TYP>CHAR10</TYP>
      </RSIMP>
     </IMPORT>
     <DOCUMENTATION>
      <RSFDO>
       <PARAMETER>IV_DOCUMENT</PARAMETER>
       <KIND>P</KIND>
      </RSFDO>
     </DOCUMENTATION>
    </item>
   </FUNCTIONS>
  </asx:values>
 </asx:abap>
</abapGit>
"""

FM_SOURCE_UPDATE = """FUNCTION z_erpl_upd_post.
*"----------------------------------------------------------------------
*"*"Update Function Module:
*"
*"*"Local Interface:
*"  IMPORTING
*"     VALUE(IV_DOCUMENT) TYPE  CHAR10
*"----------------------------------------------------------------------
  " Runs when the LUW is committed; a failure aborts the whole update.
  IF iv_document IS INITIAL.
    MESSAGE 'No document to post' TYPE 'A'.
  ENDIF.
ENDFUNCTION.
"""

FM_SOURCE_NORMAL = FM_SOURCE_UPDATE.replace('*"*"Update Function Module:\n*"\n', "")

TOP_SOURCE = """FUNCTION-POOL ZERPL_UPD_FG.                 "MESSAGE-ID ..

* INCLUDE LZERPL_UPD_FGD...                  " Local class definition
"""

INCLUDE_XML = """﻿<?xml version="1.0" encoding="utf-8"?>
<abapGit version="v1.0.0">
 <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
   <PROGDIR>
    <NAME>{name}</NAME>
    <DBAPL>S</DBAPL>
    <DBNA>D$</DBNA>
    <SUBC>{subc}</SUBC>
    <APPL>S</APPL>
{extra}    <FIXPT>X</FIXPT>
    <LDBNAME>D$S</LDBNAME>
    <UCCHECK>X</UCCHECK>
   </PROGDIR>
  </asx:values>
 </asx:abap>
</abapGit>
"""

MAIN_SOURCE = """*******************************************************************
*   System-defined Include-files.                                 *
*******************************************************************
  INCLUDE LZERPL_UPD_FGTOP.                  " Global Declarations
  INCLUDE LZERPL_UPD_FGUXX.                  " Function Modules

*******************************************************************
*   User-defined Include-files (if necessary).                    *
*******************************************************************
* INCLUDE LZERPL_UPD_FGF...                  " Subroutines
* INCLUDE LZERPL_UPD_FGO...                  " PBO-Modules
* INCLUDE LZERPL_UPD_FGI...                  " PAI-Modules
* INCLUDE LZERPL_UPD_FGE...                  " Events
* INCLUDE LZERPL_UPD_FGP...                  " Local class implement.
* INCLUDE LZERPL_UPD_FGT99.                  " ABAP Unit tests
"""


def write_repo(root, flags="     <UPDATE_TASK>1</UPDATE_TASK>\n", source=FM_SOURCE_UPDATE):
    src = root / "src"
    src.mkdir(parents=True, exist_ok=True)
    (root / ".abapgit.xml").write_text(
        '\ufeff<?xml version="1.0" encoding="utf-8"?>\n<asx:abap xmlns:asx="http://www.sap.com/abapxml" '
        'version="1.0">\n <asx:values>\n  <DATA>\n   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n'
        '   <STARTING_FOLDER>/src/</STARTING_FOLDER>\n   <FOLDER_LOGIC>PREFIX</FOLDER_LOGIC>\n'
        '  </DATA>\n </asx:values>\n</asx:abap>\n', encoding="utf-8")
    stem = FUGR.lower()
    (src / f"{stem}.fugr.xml").write_text(GROUP_XML.replace("{flags}", flags), encoding="utf-8")
    (src / f"{stem}.fugr.{FM.lower()}.abap").write_text(source, encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_upd_fgtop.abap").write_text(TOP_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_upd_fgtop.xml").write_text(
        INCLUDE_XML.format(name="LZERPL_UPD_FGTOP", subc="I", extra=""), encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_upd_fg.abap").write_text(MAIN_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_upd_fg.xml").write_text(
        INCLUDE_XML.format(name="SAPLZERPL_UPD_FG", subc="F", extra="    <RLOAD>E</RLOAD>\n"),
        encoding="utf-8")
    for args in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "update module", "--allow-empty"]):
        subprocess.run(["git", "-C", str(root), "-c", "user.name=erpl-adt test",
                        "-c", "user.email=test@erpl.invalid", *args], check=True, capture_output=True)
    return src


def tfdir():
    o = hana_oracle()
    rows = o.query(f"SELECT FMODE, UTASK FROM {o.schema}.TFDIR WHERE FUNCNAME = '{FM}'")
    return [tuple(c.strip('"').strip() for c in r) for r in rows]


@pytest.fixture(scope="module")
def repo(tmp_path_factory, cli):
    root = tmp_path_factory.mktemp("upd")
    write_repo(root)
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")
    yield root
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
class TestUpdateFunctionModule:

    @pytest.mark.order(1)
    def test_unsupported_update_kinds_are_refused_before_anything_is_written(self, cli, tmp_path):
        for flags, words in (("     <UPDATE_TASK>2</UPDATE_TASK>\n", "UPDATE_TASK '2'"),
                             ("     <REMOTE_CALL>R</REMOTE_CALL>\n     <UPDATE_TASK>1</UPDATE_TASK>\n",
                              "exclude each other")):
            root = tmp_path / words.replace(" ", "_").replace("'", "")
            write_repo(root, flags=flags)
            result = cli.run("abapgit", "deploy", str(root), "--package", PACKAGE)
            assert result.returncode == 8, result.stdout + result.stderr
            group = {o["name"]: o for o in json.loads(result.stdout)["objects"]}[FUGR]
            assert group["action"] == "failed" and words in group["message"], group
            assert tfdir() == [], "nothing may be created"

    @pytest.mark.order(2)
    def test_deploy_creates_an_update_module(self, cli, repo):
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        actions = {(o["type"], o["name"]): o["action"] for o in result["objects"]}
        assert actions[("FUGR", FUGR)] == "created", result["objects"]
        assert tfdir() == [("", "1")]  # FMODE blank, UTASK 1: V1 update module

    @pytest.mark.order(3)
    def test_redeploy_unchanged_changes_nothing(self, cli, repo):
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert {o["action"] for o in result["objects"]} == {"unchanged"}, result["objects"]

    @pytest.mark.order(4)
    def test_export_matches_files_byte_for_byte(self, cli, repo, tmp_path):
        cli.run_ok("abapgit", "export", str(tmp_path), "--package", PACKAGE, "--object", f"FUGR:{FUGR}")
        for path in sorted((repo / "src").iterdir()):
            exported = tmp_path / "src" / path.name
            assert exported.exists(), path.name
            assert exported.read_text(encoding="utf-8") == path.read_text(encoding="utf-8"), path.name

    @pytest.mark.order(5)
    def test_dropping_update_task_makes_it_normal_again(self, cli, repo):
        write_repo(repo, flags="", source=FM_SOURCE_NORMAL)
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert {(o["type"], o["name"]): o["action"] for o in result["objects"]}[("FUGR", FUGR)] == "updated"
        assert tfdir() == [("", "")]
