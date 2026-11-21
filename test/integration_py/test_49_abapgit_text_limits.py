"""Short texts longer than their SAP field are refused up front (issue #92).

SAP stores a short text in a fixed-length field and cuts what does not fit:
a function group text (TLIBT-AREAT) holds 40 characters. Before #92 a
43-character AREAT deployed as `created` - SAP kept the first 40 - and every
later deploy of the same files compared the cut text with the file and
refused ("changing the short text ... is not supported"). The repository
could never be deployed again, and nothing had said so the first time.

Scenario: a developer's repository with a function group whose text is 43
characters and a data element whose short field label is 11 characters (the
field holds 10). The deploy refuses both before anything is created (exit 8,
naming the field and its limit). The developer shortens the texts to exactly
the limits; the deploy creates them, and a redeploy finds them unchanged.
"""

import json
import subprocess

import pytest

PACKAGE = "$TMP"
FUGR = "ZERPL_TXT_FG"
DTEL = "ZERPL_TXT_E_PRIO"
# Fits from the start: refusing the others must not hold it back.
DTEL_OK = "ZERPL_TXT_E_SHIFT"

AREAT_TOO_LONG = "Order priority rules for the planning board"  # 43
AREAT_FITS = AREAT_TOO_LONG[:40]
SCRTEXT_S_TOO_LONG = "Order prio."  # 11
SCRTEXT_S_FITS = "Order prio"  # 10

GROUP_XML = """﻿<?xml version="1.0" encoding="utf-8"?>
<abapGit version="v1.0.0" serializer="LCL_OBJECT_FUGR" serializer_version="v1.0.0">
 <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
   <AREAT>{areat}</AREAT>
   <INCLUDES>
    <SOBJ_NAME>LZERPL_TXT_FGTOP</SOBJ_NAME>
    <SOBJ_NAME>SAPLZERPL_TXT_FG</SOBJ_NAME>
   </INCLUDES>
   <FUNCTIONS/>
  </asx:values>
 </asx:abap>
</abapGit>
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

TOP_SOURCE = """FUNCTION-POOL ZERPL_TXT_FG.                 "MESSAGE-ID ..

* INCLUDE LZERPL_TXT_FGD...                  " Local class definition
"""

MAIN_SOURCE = """*******************************************************************
*   System-defined Include-files.                                 *
*******************************************************************
  INCLUDE LZERPL_TXT_FGTOP.                  " Global Declarations
  INCLUDE LZERPL_TXT_FGUXX.                  " Function Modules

*******************************************************************
*   User-defined Include-files (if necessary).                    *
*******************************************************************
* INCLUDE LZERPL_TXT_FGF...                  " Subroutines
* INCLUDE LZERPL_TXT_FGO...                  " PBO-Modules
* INCLUDE LZERPL_TXT_FGI...                  " PAI-Modules
* INCLUDE LZERPL_TXT_FGE...                  " Events
* INCLUDE LZERPL_TXT_FGP...                  " Local class implement.
* INCLUDE LZERPL_TXT_FGT99.                  " ABAP Unit tests
"""

DTEL_XML = """﻿<?xml version="1.0" encoding="utf-8"?>
<abapGit version="v1.0.0" serializer="LCL_OBJECT_DTEL" serializer_version="v1.0.0">
 <asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
   <DD04V>
    <ROLLNAME>ZERPL_TXT_E_PRIO</ROLLNAME>
    <DDLANGUAGE>E</DDLANGUAGE>
    <DOMNAME>CHAR10</DOMNAME>
    <HEADLEN>10</HEADLEN>
    <SCRLEN1>10</SCRLEN1>
    <SCRLEN2>20</SCRLEN2>
    <SCRLEN3>40</SCRLEN3>
    <DDTEXT>Order priority on the planning board</DDTEXT>
    <REPTEXT>Priority</REPTEXT>
    <SCRTEXT_S>{scrtext_s}</SCRTEXT_S>
    <SCRTEXT_M>Order priority</SCRTEXT_M>
    <SCRTEXT_L>Order priority</SCRTEXT_L>
    <DTELMASTER>E</DTELMASTER>
    <REFKIND>D</REFKIND>
   </DD04V>
  </asx:values>
 </asx:abap>
</abapGit>
"""


def write_repo(root, areat, scrtext_s):
    src = root / "src"
    src.mkdir(parents=True, exist_ok=True)
    (root / ".abapgit.xml").write_text(
        '﻿<?xml version="1.0" encoding="utf-8"?>\n<asx:abap xmlns:asx="http://www.sap.com/abapxml" '
        'version="1.0">\n <asx:values>\n  <DATA>\n   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n'
        '   <STARTING_FOLDER>/src/</STARTING_FOLDER>\n   <FOLDER_LOGIC>PREFIX</FOLDER_LOGIC>\n'
        '  </DATA>\n </asx:values>\n</asx:abap>\n', encoding="utf-8")
    stem = FUGR.lower()
    (src / f"{stem}.fugr.xml").write_text(GROUP_XML.format(areat=areat), encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_txt_fgtop.abap").write_text(TOP_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.lzerpl_txt_fgtop.xml").write_text(
        INCLUDE_XML.format(name="LZERPL_TXT_FGTOP", subc="I", extra=""), encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_txt_fg.abap").write_text(MAIN_SOURCE, encoding="utf-8")
    (src / f"{stem}.fugr.saplzerpl_txt_fg.xml").write_text(
        INCLUDE_XML.format(name="SAPLZERPL_TXT_FG", subc="F", extra="    <RLOAD>E</RLOAD>\n"),
        encoding="utf-8")
    (src / f"{DTEL.lower()}.dtel.xml").write_text(DTEL_XML.format(scrtext_s=scrtext_s), encoding="utf-8")
    (src / f"{DTEL_OK.lower()}.dtel.xml").write_text(
        DTEL_XML.format(scrtext_s="Shift").replace(DTEL, DTEL_OK)
        .replace("Order priority on the planning board", "Work shift on the planning board"),
        encoding="utf-8")
    for args in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "texts", "--allow-empty"]):
        subprocess.run(["git", "-C", str(root), "-c", "user.name=erpl-adt test",
                        "-c", "user.email=test@erpl.invalid", *args], check=True, capture_output=True)


def entry(data, kind, name):
    return {(o["type"], o["name"]): o for o in data["objects"]}[(kind, name)]


@pytest.fixture
def repo(tmp_path, cli):
    root = tmp_path / "repo"
    root.mkdir()
    write_repo(root, AREAT_TOO_LONG, SCRTEXT_S_TOO_LONG)
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")
    yield root
    write_repo(root, AREAT_FITS, SCRTEXT_S_FITS)
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
class TestShortTextLimits:

    def test_too_long_is_refused_then_fitting_texts_deploy_and_redeploy(self, cli, repo):
        result = cli.run("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert result.returncode == 8, result.stdout + result.stderr

        data = json.loads(result.stdout)
        group = entry(data, "FUGR", FUGR)
        assert group["action"] == "failed", group
        assert "AREAT" in group["message"] and "40" in group["message"], group
        dtel = entry(data, "DTEL", DTEL)
        assert dtel["action"] == "failed", dtel
        assert "SCRTEXT_S" in dtel["message"] and "10" in dtel["message"], dtel
        # Only the refused objects are held back; the one that fits deploys.
        assert entry(data, "DTEL", DTEL_OK)["action"] == "created", data["objects"]
        # The refused ones were not created.
        assert cli.run("object", "read", f"/sap/bc/adt/functions/groups/{FUGR.lower()}").returncode == 2
        assert cli.run("object", "read", f"/sap/bc/adt/ddic/dataelements/{DTEL.lower()}").returncode == 2

        write_repo(repo, AREAT_FITS, SCRTEXT_S_FITS)
        created = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert entry(created, "DTEL", DTEL_OK)["action"] == "unchanged", created["objects"]
        assert entry(created, "FUGR", FUGR)["action"] == "created", created["objects"]
        assert entry(created, "DTEL", DTEL)["action"] == "created", created["objects"]

        again = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE)
        assert {o["action"] for o in again["objects"]} == {"unchanged"}, again["objects"]
