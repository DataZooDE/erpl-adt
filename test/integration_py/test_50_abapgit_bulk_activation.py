"""Many programs changed in one deploy (issue #95).

A team keeps a reporting suite of twenty small reports in git and changes
them all in one commit (a new header comment, say). Before #95 the deploy
reported every one of them `failed` (part source, no message, exit 5),
although all were written and active - the next deploy said `unchanged`.
And when one of them really had an error, the first deploy lost SAP's text:
`failed` without a reason, the text appeared only on the second run.

The trigger, measured on A4H with the demo: one program of the run with a
syntax warning. RS_WORKING_OBJECTS_ACTIVATE's checklist lists warnings with
the errors; the helper counted the warning as an error, and as it named no
object, every program of the run failed.

Scenario: deploy the suite (twenty dialog programs, one with a warning), take SAP's own export as the git baseline,
change all twenty programs, deploy: all `updated`, exit 0, a redeploy is
`unchanged`. Then break one report with a real type error and change the
others again: only the broken one fails, on the first run, with SAP's text;
the other nineteen are updated.
"""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

PACKAGE = "$TMP"
COUNT = 20
# Dialog programs like the ones the bug was found with (#95: module pools
# with screens and a GUI status): copies of the fixture's sales order dialog.
NAMES = [f"ZERPL_BULK_D{i:02d}" for i in range(1, COUNT + 1)]
EXIT_ACTIVATION = 5
FIXTURE = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_fixture"
TEMPLATE = "zerpl_fx_dialog"


# One program of the suite has a syntax *warning* - a sort of a table with
# an empty key, as in the erpl-sap-demo unit test where #95 was found. SAP
# activates it; its checklist reports the warning.
WARNING_PROGRAM = NAMES[11]
WARNING_FORM = (
    "\nFORM sorted_documents.\n"
    "  TYPES ty_docs TYPE STANDARD TABLE OF char10 WITH EMPTY KEY.\n"
    "  DATA lt_open TYPE ty_docs.\n"
    "  APPEND '5100000103' TO lt_open.\n"
    "  SORT lt_open.\n"
    "ENDFORM.\n")


def write_suite(root):
    src = root / "src"
    src.mkdir(parents=True, exist_ok=True)
    shutil.copy(FIXTURE / ".abapgit.xml", root / ".abapgit.xml")
    for name in NAMES:
        for template in sorted((FIXTURE / "src").glob(TEMPLATE + ".prog*")):
            text = template.read_text(encoding="utf-8")
            text = text.replace(TEMPLATE.upper(), name).replace(TEMPLATE, name.lower())
            if name == WARNING_PROGRAM and template.name == TEMPLATE + ".prog.abap":
                text += WARNING_FORM
            (src / template.name.replace(TEMPLATE, name.lower())).write_text(text, encoding="utf-8")


def git(root, *args):
    subprocess.run(["git", "-C", str(root), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args], check=True, capture_output=True)


def actions(result):
    data = json.loads(result.stdout)
    return data, {o["name"]: o for o in data["objects"] if o["type"] == "PROG"}


@pytest.fixture
def suite(cli, tmp_path):
    root = tmp_path / "suite"
    write_suite(root)
    git(root, "init", "-q")
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")
    yield root
    cli.run("abapgit", "delete", str(root), "--package", PACKAGE, "--force")


@pytest.mark.abapgit_deploy
@pytest.mark.slow
# Deploying, changing and deleting twenty dialog programs takes several minutes.
@pytest.mark.timeout(1800)
class TestTwentyProgramsInOneDeploy:

    def test_all_changed_programs_are_updated_and_a_real_error_is_named(self, cli, suite):
        created = cli.run("abapgit", "deploy", str(suite), "--package", PACKAGE)
        assert created.returncode == 0, created.stdout + created.stderr
        # SAP's own rendering is the baseline in git, as after `abapgit export`.
        cli.run_ok("abapgit", "export", str(suite), "--package", PACKAGE)
        git(suite, "add", "-A")
        git(suite, "commit", "-qm", "baseline")

        # One commit changes all twenty programs.
        for path in sorted((suite / "src").glob("*.prog.abap")):
            path.write_text("* Reviewed for the 2026 audit\n" + path.read_text(encoding="utf-8"),
                            encoding="utf-8")
        git(suite, "commit", "-qam", "audit header")
        result = cli.run("abapgit", "deploy", str(suite), "--package", PACKAGE)
        data, progs = actions(result)
        assert result.returncode == 0, json.dumps(data, indent=1)[:3000]
        assert {o["action"] for o in progs.values()} == {"updated"}, \
            {n: (o["action"], o.get("message", "")) for n, o in progs.items()}
        assert len(progs) == COUNT

        again = cli.run_ok("abapgit", "deploy", str(suite), "--package", PACKAGE)
        assert {o["action"] for o in again["objects"]} == {"unchanged"}, again["objects"]

        # One report gets a real type error; the others another harmless change.
        broken = NAMES[6]
        for path in sorted((suite / "src").glob("*.prog.abap")):
            text = path.read_text(encoding="utf-8").replace("2026 audit", "2027 audit")
            if path.name.startswith(broken.lower()):
                # A type error: a date where a number is computed.
                text = text.replace("START-OF-SELECTION.", "START-OF-SELECTION.\n"
                                    "  DATA lv_broken TYPE i.\n  lv_broken = sy-datum + snwd_so.", 1) \
                    if "START-OF-SELECTION." in text else text + "\nFORM broken.\n  DATA lv_broken TYPE i.\n  lv_broken = snwd_so.\nENDFORM.\n"
            path.write_text(text, encoding="utf-8")
        git(suite, "commit", "-qam", "2027 audit, one broken")
        result = cli.run("abapgit", "deploy", str(suite), "--package", PACKAGE)
        data, progs = actions(result)
        assert result.returncode == EXIT_ACTIVATION, json.dumps(data, indent=1)[:3000]
        failed = {n for n, o in progs.items() if o["action"] == "failed"}
        assert failed == {broken}, {n: (o["action"], o.get("message", "")) for n, o in progs.items()}
        assert progs[broken].get("message"), f"no reason on the first run: {progs[broken]}"
        assert {o["action"] for n, o in progs.items() if n != broken} == {"updated"}
