"""Integration tests for `erpl-adt abapgit` with remote-enabled function modules.

abapGit marks a remote-enabled module with <REMOTE_CALL>R</REMOTE_CALL> in its
function group's XML. ADT keeps that flag in the module's metadata
(fmodule:processingType "rfc"), not in its source, and ignores it on create;
the deploy sets it by PUTting the metadata under the lock, as Eclipse's
properties view does (measured on A4H).

The function group is written by the test into a git work tree (one RFC module
doubling a number). Verified independently of the code under test: the
module's ADT metadata (processingType) and TFDIR-FMODE ('R' = remote-enabled)
read with a plain SELECT in HANA, plus the export, byte for byte.
"""

import base64
import re
import subprocess
import urllib.error
import urllib.request

import pytest

from hana_oracle import oracle as hana_oracle

PACKAGE = "$TMP"
FUGR = "ZERPL_RFC_FG"
FM = "Z_ERPL_RFC_DOUBLE"

GROUP_XML = """﻿<?xml version="1.0" encoding="utf-8"?>
<abapGit version="v1.0.0" serializer="LCL_OBJECT_FUGR" serializer_version="v1.0.0">
 <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
   <AREAT>erpl-adt test: remote-enabled modules</AREAT>
   <INCLUDES>
    <SOBJ_NAME>LZERPL_RFC_FGTOP</SOBJ_NAME>
    <SOBJ_NAME>SAPLZERPL_RFC_FG</SOBJ_NAME>
   </INCLUDES>
   <FUNCTIONS>
    <item>
     <FUNCNAME>Z_ERPL_RFC_DOUBLE</FUNCNAME>
{remote}     <SHORT_TEXT>erpl-adt test: double a number</SHORT_TEXT>
     <IMPORT>
      <RSIMP>
       <PARAMETER>IV_X</PARAMETER>
       <TYP>INT4</TYP>
      </RSIMP>
     </IMPORT>
     <EXPORT>
      <RSEXP>
       <PARAMETER>EV_Y</PARAMETER>
       <TYP>INT4</TYP>
      </RSEXP>
     </EXPORT>
     <DOCUMENTATION>
      <RSFDO>
       <PARAMETER>IV_X</PARAMETER>
       <KIND>P</KIND>
      </RSFDO>
      <RSFDO>
       <PARAMETER>EV_Y</PARAMETER>
       <KIND>P</KIND>
      </RSFDO>
     </DOCUMENTATION>
    </item>
   </FUNCTIONS>
  </asx:values>
 </asx:abap>
</abapGit>
"""

FM_SOURCE = """FUNCTION z_erpl_rfc_double.
*"----------------------------------------------------------------------
*"*"Local Interface:
*"  IMPORTING
*"     VALUE(IV_X) TYPE  INT4
*"  EXPORTING
*"     VALUE(EV_Y) TYPE  INT4
*"----------------------------------------------------------------------
  ev_y = iv_x * 2.
ENDFUNCTION.
"""

TOP_SOURCE = """FUNCTION-POOL ZERPL_RFC_FG.                 "MESSAGE-ID ..

* INCLUDE LZERPL_RFC_FGD...                  " Local class definition
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
  INCLUDE LZERPL_RFC_FGTOP.                  " Global Declarations
  INCLUDE LZERPL_RFC_FGUXX.                  " Function Modules

*******************************************************************
*   User-defined Include-files (if necessary).                    *
*******************************************************************
* INCLUDE LZERPL_RFC_FGF...                  " Subroutines
* INCLUDE LZERPL_RFC_FGO...                  " PBO-Modules
* INCLUDE LZERPL_RFC_FGI...                  " PAI-Modules
* INCLUDE LZERPL_RFC_FGE...                  " Events
* INCLUDE LZERPL_RFC_FGP...                  " Local class implement.
* INCLUDE LZERPL_RFC_FGT99.                  " ABAP Unit tests
"""


def _write_repo(root, remote=True):
    src = root / "src"
    src.mkdir(parents=True, exist_ok=True)
    (root / ".abapgit.xml").write_text(
        '﻿<?xml version="1.0" encoding="utf-8"?>\n<asx:abap xmlns:asx="http://www.sap.com/abapxml" '
        'version="1.0">\n <asx:values>\n  <DATA>\n   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n'
        '   <STARTING_FOLDER>/src/</STARTING_FOLDER>\n   <FOLDER_LOGIC>PREFIX</FOLDER_LOGIC>\n'
        '  </DATA>\n </asx:values>\n</asx:abap>\n', encoding="utf-8")
    stem = FUGR.lower()
    (src / f"{stem}.fugr.xml").write_text(
        GROUP_XML.replace("{remote}", "     <REMOTE_CALL>R</REMOTE_CALL>\n" if remote else ""), encoding="utf-8")
    (src / f"{stem}.fugr.{FM.lower()}.abap").write_text(FM_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_rfc_fgtop.abap").write_text(TOP_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_rfc_fgtop.xml").write_text(
        INCLUDE_XML.format(name="LZERPL_RFC_FGTOP", subc="I", extra=""), encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_rfc_fg.abap").write_text(MAIN_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_rfc_fg.xml").write_text(
        INCLUDE_XML.format(name="SAPLZERPL_RFC_FG", subc="F", extra="    <RLOAD>E</RLOAD>\n"),
        encoding="utf-8")
    for args in (["init", "-q"], ["add", "."], ["commit", "-qm", "rfc fixture", "--allow-empty"]):
        subprocess.run(["git", "-C", str(root), "-c", "user.name=erpl-adt test",
                        "-c", "user.email=test@erpl.invalid", *args], check=True, capture_output=True)
    return src


def _processing_type(cfg):
    token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
    req = urllib.request.Request(
        f"http://{cfg['host']}:{cfg['port']}/sap/bc/adt/functions/groups/{FUGR.lower()}/fmodules/{FM.lower()}",
        headers={"Authorization": f"Basic {token}", "sap-client": cfg["client"],
                 "Accept": "application/vnd.sap.adt.functions.fmodules.v3+xml"})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode()
    except urllib.error.HTTPError as err:
        return f"HTTP {err.code}"
    return re.search(r'fmodule:processingType="(\w+)"', body).group(1)


def _fmode():
    o = hana_oracle()
    rows = o.query(f"SELECT FMODE FROM {o.schema}.TFDIR WHERE FUNCNAME = '{FM}'")
    return [r[0].strip('"').strip() for r in rows]


@pytest.fixture(scope="module")
def repo(tmp_path_factory, cli):
    root = tmp_path_factory.mktemp("rfc")
    _write_repo(root)
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")
    yield root
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
class TestRemoteEnabledFunctionModule:

    def test_deploy_creates_a_remote_enabled_module(self, cli, sap_config, repo):
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        actions = {(o["type"], o["name"]): o["action"] for o in result["objects"]}
        assert actions[("FUGR", FUGR)] == "created", result["objects"]
        assert _processing_type(sap_config) == "rfc"
        assert _fmode() == ["R"]

    def test_redeploy_unchanged_changes_nothing(self, cli, repo):
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert {o["action"] for o in result["objects"]} == {"unchanged"}, result["objects"]

    def test_export_matches_files_byte_for_byte(self, cli, repo, tmp_path):
        cli.run_ok("abapgit", "export", str(tmp_path), "--package", PACKAGE, "--object", f"FUGR:{FUGR}")
        for path in sorted((repo / "src").iterdir()):
            exported = tmp_path / "src" / path.name
            assert exported.exists(), path.name
            assert exported.read_bytes() == path.read_bytes(), path.name

    def test_removing_remote_call_makes_it_normal_again(self, cli, sap_config, repo):
        _write_repo(repo, remote=False)
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert {(o["type"], o["name"]): o["action"] for o in result["objects"]}[("FUGR", FUGR)] == "updated"
        assert _processing_type(sap_config) == "normal"
        assert _fmode() == [""]
