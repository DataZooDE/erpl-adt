"""Integration tests for `erpl-adt abapgit deploy|export|status|delete`.

Classic SAP GUI objects (dynpros, GUI status/titles, transactions, domains,
text pools, function modules) have no ADT REST endpoint. `erpl-adt abapgit`
deploys them from abapGit-serialized files through SAP's own workbench
function modules (run by a temporary console helper class), and exports them
back into the same files.

The fixtures under fixtures/abapgit/erpl_demo model a small, realistic
order-display application on the EPM demo model (SNWD_SO / SNWD_SO_I /
SNWD_BPA) that exists on the A4H trial — the trial has no SD tables
(VBAK/VBAP), so a "Display Sales Order" clone is built on EPM.
"""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from cli_runner import CliRunner

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"


def _entry(result, obj_type, name):
    """Return the deploy/export result entry for one object."""
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _fixed_values(adt, domain):
    """Read a domain's fixed values through ADT REST — independent of the
    helper-class engine under test."""
    status, body = adt.get(
        f"/sap/bc/adt/ddic/domains/{domain.lower()}",
        headers={"Accept": "application/vnd.sap.adt.domains.v2+xml, application/xml"},
    )
    assert status == 200, body[:500]
    lows = re.findall(r"<doma:low>([^<]*)</doma:low>", body)
    texts = re.findall(r"<doma:text>([^<]*)</doma:text>", body)
    return list(zip(lows, texts))


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


@pytest.fixture(scope="module")
def adt(sap_config):
    return _AdtReader(sap_config)


def _engine_runs(result):
    """Helper-class runs of one CLI call, counted in its -v log (each run is
    one POST to ADT classrun; writing and activating the helper costs ~3 s)."""
    return result.stderr.count("POST /sap/bc/adt/oo/classrun/")


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _helper_classes(cli):
    data = cli.run_ok("search", "ZCL_ERPLHLP*", "--max", "500")
    rows = data if isinstance(data, list) else data.get("results", [])
    return {r.get("name") for r in rows}


@pytest.fixture(scope="module")
def helpers_before(cli):
    """Helper classes that already existed before this module ran (e.g. left
    by runs interrupted by an outage) — not this run's responsibility."""
    return _helper_classes(cli)


@pytest.mark.abapgit_deploy
class TestDomain:
    """Slice 1: a domain with fixed values (sales order lifecycle status)."""

    DOMA = "ZERPL_D_SO_STATUS"
    FILE = SRC / "zerpl_d_so_status.doma.xml"

    def test_deploy_creates_domain_with_fixed_values(self, cli, adt, helpers_before):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE, "--force")

        result = cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)

        assert _entry(result, "DOMA", self.DOMA)["action"] == "created"
        assert _fixed_values(adt, self.DOMA) == [
            ("N", "New"), ("P", "In process"), ("C", "Completed"), ("X", "Cancelled"),
        ]

    def test_redeploy_unchanged_changes_nothing(self, cli):
        result = cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)
        assert _entry(result, "DOMA", self.DOMA)["action"] == "unchanged"

    def test_changed_fixed_value_updates_in_place(self, cli, adt, tmp_path):
        changed = tmp_path / self.FILE.name
        changed.write_bytes(
            self.FILE.read_bytes().replace(b"<DDTEXT>Cancelled</DDTEXT>",
                                           b"<DDTEXT>Rejected</DDTEXT>"))

        result = cli.run_ok("abapgit", "deploy", str(changed), "--package", PACKAGE)

        assert _entry(result, "DOMA", self.DOMA)["action"] == "updated"
        assert ("X", "Rejected") in _fixed_values(adt, self.DOMA)

    def test_export_matches_file_byte_for_byte(self, cli, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)

        cli.run_ok("abapgit", "export", str(tmp_path), "--object", f"DOMA:{self.DOMA}",
                   "--package", PACKAGE)

        exported = tmp_path / "src" / self.FILE.name
        assert exported.read_bytes() == self.FILE.read_bytes()

    def test_delete_removes_domain(self, cli, adt):
        cli.run_ok("abapgit", "delete", str(self.FILE), "--package", PACKAGE)
        status, body = adt.get(f"/sap/bc/adt/ddic/domains/{self.DOMA.lower()}")
        assert status == 404, body[:800]


@pytest.mark.abapgit_deploy
class TestGuardRails:
    """Enterprise guard rails: nothing is written when a rule is violated."""

    FILE = SRC / "zerpl_d_so_status.doma.xml"

    def test_transportable_package_without_request_is_refused(self, cli, adt):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE, "--force")
        # ZCLASSIC_DEVELOPMENT is a transportable customer package on A4H.
        result = cli.run("abapgit", "deploy", str(self.FILE), "--package", "ZCLASSIC_DEVELOPMENT")

        assert result.returncode == 9, result.stderr
        data = json.loads(result.stdout)
        entry = _entry(data, "DOMA", "ZERPL_D_SO_STATUS")
        assert entry["action"] == "refused"
        assert "--transport" in entry["message"]
        status, _ = adt.get("/sap/bc/adt/ddic/domains/zerpl_d_so_status")
        assert status == 404  # nothing was written

    def test_status_without_request_annotates_instead_of_refusing(self, cli):
        # A pull-request check has no transport request: status reports what
        # a deploy would do and notes the missing --transport; --strict refuses.
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE, "--force")

        data = cli.run_ok("abapgit", "status", str(self.FILE), "--package",
                          "ZCLASSIC_DEVELOPMENT")

        entry = _entry(data, "DOMA", "ZERPL_D_SO_STATUS")
        assert entry["action"] == "created"
        assert any("--transport" in n for n in entry["notes"])
        strict = cli.run("abapgit", "status", str(self.FILE), "--package",
                         "ZCLASSIC_DEVELOPMENT", "--strict")
        assert strict.returncode == 9, strict.stdout + strict.stderr
        assert _entry(json.loads(strict.stdout), "DOMA", "ZERPL_D_SO_STATUS")["action"] == \
            "refused"

    def test_sap_object_is_refused(self, cli, tmp_path):
        sap_file = tmp_path / "xfeld.doma.xml"
        sap_file.write_bytes(self.FILE.read_bytes().replace(
            b"ZERPL_D_SO_STATUS", b"XFELD"))

        result = cli.run("abapgit", "deploy", str(sap_file), "--package", PACKAGE)

        assert result.returncode == 1, result.stderr
        assert _entry(json.loads(result.stdout), "DOMA", "XFELD")["action"] == "refused"

    def test_status_reports_drift_but_exits_0_unless_strict(self, cli, colleague_cli, tmp_path):
        # A colleague changed the domain in the system; it is in no git
        # history. status is a report: it shows the refusal and exits 0 (it
        # could run); --strict turns any refusal into its exit code (6).
        cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE, "--force")
        theirs = tmp_path / self.FILE.name
        theirs.write_bytes(self.FILE.read_bytes().replace(b"<DDTEXT>Cancelled</DDTEXT>",
                                                          b"<DDTEXT>Withdrawn</DDTEXT>"))
        colleague_cli.run_ok("abapgit", "deploy", str(theirs), "--package", PACKAGE, "--force")
        try:
            status = cli.run("abapgit", "status", str(self.FILE), "--package", PACKAGE)
            assert status.returncode == 0, status.stdout + status.stderr
            data = json.loads(status.stdout)
            entry = _entry(data, "DOMA", "ZERPL_D_SO_STATUS")
            assert entry["action"] == "refused"
            assert "BWDEVELOPER" in entry["message"]
            assert data["exit_code"] == 0
            text = cli.run_no_json("abapgit", "status", str(self.FILE), "--package", PACKAGE)
            assert text.returncode == 0
            assert "refused  DOMA ZERPL_D_SO_STATUS" in text.stdout

            strict = cli.run("abapgit", "status", str(self.FILE), "--package", PACKAGE,
                             "--strict")
            assert strict.returncode == 6, strict.stdout + strict.stderr
            assert json.loads(strict.stdout)["exit_code"] == 6
        finally:
            cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE, "--force")

    def test_status_fails_on_a_broken_file(self, cli, tmp_path):
        # A pull request that breaks an XML file must not pass `status`
        # (without --strict), while the other objects are still reported.
        repo = tmp_path / "repo"
        (repo / "src").mkdir(parents=True)
        (repo / ".abapgit.xml").write_bytes((SRC.parent / ".abapgit.xml").read_bytes())
        (repo / "src" / self.FILE.name).write_bytes(self.FILE.read_bytes())
        (repo / "src" / "zerpl_d_broken.doma.xml").write_bytes(
            self.FILE.read_bytes()[:400])  # cut off mid-element

        status = cli.run("abapgit", "status", str(repo), "--package", PACKAGE)

        assert status.returncode != 0, status.stdout + status.stderr
        data = json.loads(status.stdout)
        assert data["command"] == "status"
        assert data["dry_run"] is True
        assert data["exit_code"] == status.returncode
        assert _entry(data, "DOMA", "ZERPL_D_BROKEN")["action"] == "failed"
        assert _entry(data, "DOMA", "ZERPL_D_SO_STATUS")["action"] in ("created", "updated",
                                                                       "unchanged")

    def test_status_reports_without_writing(self, cli, adt):
        cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE, "--force")

        data = cli.run_ok("abapgit", "status", str(self.FILE), "--package", PACKAGE)

        assert data["dry_run"] is True
        assert _entry(data, "DOMA", "ZERPL_D_SO_STATUS")["action"] == "created"
        status, _ = adt.get("/sap/bc/adt/ddic/domains/zerpl_d_so_status")
        assert status == 404

    def test_no_helper_classes_are_left_behind(self, cli, helpers_before):
        leftovers = _helper_classes(cli) - helpers_before
        assert leftovers == set(), f"this run left helper classes behind: {leftovers}"


@pytest.fixture(scope="module")
def cli2(sap_config, cli):
    """A second SAP user (a colleague). SAP_USER_2 (default BWDEVELOPER) /
    SAP_PASSWORD_2 (default SAP_PASSWORD). Logon is probed once; the tests
    that need a colleague skip when it fails."""
    user = os.getenv("SAP_USER_2", "BWDEVELOPER")
    runner = CliRunner(
        binary_path=cli.binary, host=sap_config["host"], port=sap_config["port"],
        user=user, password=os.getenv("SAP_PASSWORD_2", sap_config["password"]),
        client=sap_config["client"],
    )
    probe = runner.run("search", "ZERPL_D*", "--max", "1")
    if probe.returncode != 0:
        pytest.skip(f"second SAP user {user} is not available: {probe.stderr[:300]}")
    return runner


@pytest.mark.abapgit_deploy
class TestColleagueChanges:
    """Two users: a deploy never silently overwrites (or deletes) what a
    colleague changed in the system, and respects a colleague's edit lock."""

    DOMA = "ZERPL_D_SO_STATUS"
    FILE = SRC / "zerpl_d_so_status.doma.xml"

    def _colleague_edits_domain(self, cli, cli2, tmp_path):
        cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE, "--force")
        edited = tmp_path / self.FILE.name
        edited.write_bytes(self.FILE.read_bytes().replace(
            b"<DDTEXT>Cancelled</DDTEXT>", b"<DDTEXT>Storniert</DDTEXT>"))
        # The colleague overwrites on purpose (as if editing in SE11).
        cli2.run_ok("abapgit", "deploy", str(edited), "--package", PACKAGE, "--force")

    def test_deploy_refuses_a_colleagues_change_and_force_overwrites(
            self, cli, cli2, adt, tmp_path):
        self._colleague_edits_domain(cli, cli2, tmp_path)

        result = cli.run("abapgit", "deploy", str(self.FILE), "--package", PACKAGE)

        assert result.returncode == 6, result.stderr
        data = json.loads(result.stdout)
        entry = _entry(data, "DOMA", self.DOMA)
        assert entry["action"] == "refused"
        assert cli2.user in entry["message"]
        assert entry["category"] == "lock_conflict" and entry["exit_code"] == 6
        assert data["exit_code"] == 6
        assert ("X", "Storniert") in _fixed_values(adt, self.DOMA)  # untouched

        forced = cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE,
                            "--force")
        assert _entry(forced, "DOMA", self.DOMA)["action"] == "updated"
        assert ("X", "Cancelled") in _fixed_values(adt, self.DOMA)

    def test_delete_refuses_a_colleagues_change(self, cli, cli2, adt, tmp_path):
        self._colleague_edits_domain(cli, cli2, tmp_path)

        result = cli.run("abapgit", "delete", str(self.FILE), "--package", PACKAGE)

        assert result.returncode == 6, result.stderr
        entry = _entry(json.loads(result.stdout), "DOMA", self.DOMA)
        assert entry["action"] == "refused"
        assert cli2.user in entry["message"]
        status, _ = adt.get(f"/sap/bc/adt/ddic/domains/{self.DOMA.lower()}")
        assert status == 200  # still there
        cli.run_ok("abapgit", "deploy", str(self.FILE), "--package", PACKAGE, "--force")

    def test_deploy_fails_on_a_colleagues_edit_lock(self, cli, cli2, adt, tmp_path):
        xml = SRC / "zerpl_po_report.prog.xml"
        abap = SRC / "zerpl_po_report.prog.abap"
        cli.run_ok("abapgit", "deploy", str(xml), "--package", PACKAGE, "--force",
                   timeout=900)
        work = tmp_path / "work"
        work.mkdir()
        shutil.copy(xml, work / xml.name)
        (work / abap.name).write_bytes(abap.read_bytes() + b"* changed while locked\n")
        uri = "/sap/bc/adt/programs/programs/zerpl_po_report"
        session = tmp_path / "colleague_session.json"
        lock = cli2.run_ok("object", "lock", uri, session_file=session)
        try:
            result = cli.run("abapgit", "deploy", str(work / xml.name), "--package", PACKAGE,
                             timeout=900)
            assert result.returncode == 6, result.stdout + result.stderr
            entry = _entry(json.loads(result.stdout), "PROG", "ZERPL_PO_REPORT")
            assert entry["action"] == "failed"
            status, source = adt.get(uri + "/source/main")
            assert status == 200
            assert "changed while locked" not in source  # content unchanged
        finally:
            cli2.run("object", "unlock", uri, "--handle", lock["handle"],
                     session_file=session)


@pytest.mark.abapgit_deploy
class TestSubPackageRepository:
    """A repository with a sub-package folder (PREFIX: src/sub/ -> <root>_SUB).

    Uses the local packages $ZERPL_GIT and $ZERPL_GIT_SUB (created once on
    A4H for these tests) and a test-only copy of the status domain."""

    ROOT = "$ZERPL_GIT"
    SUB = "$ZERPL_GIT_SUB"
    DOMA = "ZERPL_D_SUB_STATUS"

    def _repo(self, tmp_path, cli):
        if cli.run("object", "read", "/sap/bc/adt/packages/%24zerpl_git_sub").returncode != 0:
            pytest.skip("local test packages $ZERPL_GIT / $ZERPL_GIT_SUB do not exist")
        repo = tmp_path / "repo"
        (repo / "src" / "sub").mkdir(parents=True)
        shutil.copy(FIXTURES / ".abapgit.xml", repo / ".abapgit.xml")
        file = repo / "src" / "sub" / "zerpl_d_sub_status.doma.xml"
        file.write_bytes((SRC / "zerpl_d_so_status.doma.xml").read_bytes().replace(
            b"ZERPL_D_SO_STATUS", self.DOMA.encode()))
        return repo, file

    def test_deploy_export_refresh_and_delete_keep_the_folder(self, cli, adt, tmp_path):
        repo, file = self._repo(tmp_path, cli)
        try:
            deployed = cli.run_ok("abapgit", "deploy", str(repo), "--package", self.ROOT)
            assert _entry(deployed, "DOMA", self.DOMA)["package"] == self.SUB

            # --object: lands in the folder of its TADIR package.
            fresh = tmp_path / "fresh"
            exported = cli.run_ok("abapgit", "export", str(fresh), "--package", self.ROOT,
                                  "--object", f"DOMA:{self.DOMA}")
            assert _entry(exported, "DOMA", self.DOMA)["package"] == self.SUB
            assert (fresh / "src" / "sub" / file.name).read_bytes() == file.read_bytes()
            assert not (fresh / "src" / file.name).exists()

            # Refresh in place: written back into src/sub/, an obsolete part
            # file of the object is removed (and git would show it).
            obsolete = file.parent / "zerpl_d_sub_status.doma.screen_0200.abap"
            obsolete.write_text("obsolete\n")
            refreshed = cli.run_ok("abapgit", "export", str(repo), "--package", self.ROOT)
            entry = _entry(refreshed, "DOMA", self.DOMA)
            assert "removed " + obsolete.name in entry["parts"]
            assert not obsolete.exists()
            assert sorted(p.name for p in (repo / "src").rglob("*.xml")) == [file.name]
            again = cli.run_ok("abapgit", "status", str(repo), "--package", self.ROOT)
            assert _entry(again, "DOMA", self.DOMA)["action"] == "unchanged"
        finally:
            cli.run("abapgit", "delete", str(repo), "--package", self.ROOT, "--force")
        status, _ = adt.get(f"/sap/bc/adt/ddic/domains/{self.DOMA.lower()}")
        assert status == 404

    def test_single_file_inside_the_repo_uses_the_folder_package(self, cli, tmp_path):
        repo, file = self._repo(tmp_path, cli)
        data = cli.run_ok("abapgit", "status", str(file), "--package", self.ROOT)
        assert _entry(data, "DOMA", self.DOMA)["package"] == self.SUB


@pytest.mark.abapgit_deploy
class TestRepositoryRoundTrip:
    """The whole demo repository: domain, data element, function group with
    function modules, a module pool with five screens and GUI status, a report
    with text pool, and three transactions — deployed in one call."""

    def _keys(self):
        keys = []
        for f in sorted(SRC.iterdir()):
            parts = f.name.split(".")
            if len(parts) == 3 and parts[2] == "xml" and parts[1] != "devc":
                keys.append(f"{parts[1].upper()}:{parts[0].upper()}")
        return keys

    def test_deploy_whole_repository_in_dependency_order(self, cli):
        # From a clean state: earlier suites may have left (or interrupted
        # runs may have left) versions of these objects that are in no git
        # history, which a deploy rightly refuses to overwrite.
        cli.run("abapgit", "delete", str(FIXTURES), "--package", PACKAGE, "--force",
                timeout=1800)
        result = cli.run("abapgit", "deploy", str(FIXTURES), "--package", PACKAGE,
                         timeout=1800, extra_flags=["-v"])
        assert result.returncode == 0, result.stdout + result.stderr[-3000:]
        data = json.loads(result.stdout)

        order = [o["type"] for o in data["objects"]]
        rank = {"DOMA": 0, "DTEL": 1, "FUGR": 2, "PROG": 3, "TRAN": 4}
        assert order == sorted(order, key=rank.get), order
        assert not [o for o in data["objects"] if o["action"] in ("failed", "refused")]
        # Batched per phase, whatever the object count: one read run, one
        # DDIC run (domain + data element, activated before anything uses
        # them), one run that writes screens / GUI status / text pools /
        # transactions and activates them. The function group goes over ADT.
        assert _engine_runs(result) <= 3, _engine_runs(result)

    def test_one_changed_screen_costs_two_engine_runs(self, cli, tmp_path):
        # A redeploy in a real work tree: the fixture is committed, one
        # screen's flow logic changes. Read run + one write-and-activate run.
        repo = tmp_path / "repo"
        shutil.copytree(FIXTURES, repo)
        _git(repo, "init", "-q")
        _git(repo, "add", ".")
        _git(repo, "commit", "-qm", "fixture")
        flow = repo / "src" / "zerpl_so_display.prog.screen_0100.abap"
        original = flow.read_bytes()
        flow.write_bytes(original.replace(b"PROCESS BEFORE OUTPUT.\n",
                                          b"PROCESS BEFORE OUTPUT.\n* order number entry\n", 1))

        result = cli.run("abapgit", "deploy", str(repo), "--package", PACKAGE,
                         timeout=1800, extra_flags=["-v"])

        assert result.returncode == 0, result.stdout + result.stderr[-3000:]
        data = json.loads(result.stdout)
        changed = [o for o in data["objects"] if o["action"] != "unchanged"]
        assert [(o["name"], o["action"], o["parts"]) for o in changed] == \
               [("ZERPL_SO_DISPLAY", "updated", ["screen 0100"])]
        assert _engine_runs(result) <= 2, _engine_runs(result)
        # Back to the fixture.
        _git(repo, "commit", "-qam", "comment")
        flow.write_bytes(original)
        cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE, timeout=1800)

    def test_redeploy_whole_repository_changes_nothing(self, cli):
        data = cli.run_ok("abapgit", "deploy", str(FIXTURES), "--package", PACKAGE,
                          timeout=1800)
        assert {o["action"] for o in data["objects"]} == {"unchanged"}, data["objects"]

    def test_crlf_working_copy_reads_as_unchanged(self, cli, tmp_path):
        # A Windows checkout with core.autocrlf=true: every LF is CRLF, in the
        # object files and in .abapgit.xml. It reads as the LF files abapGit
        # writes, so nothing is reported changed and nothing is rewritten.
        repo = tmp_path / "crlf"
        shutil.copytree(FIXTURES, repo)
        converted = 0
        for f in repo.rglob("*"):
            if f.is_file():
                f.write_bytes(f.read_bytes().replace(b"\n", b"\r\n"))
                converted += 1
        assert converted > 10 and b"\r\n" in (repo / ".abapgit.xml").read_bytes()

        for action in ("status", "deploy"):
            data = cli.run_ok("abapgit", action, str(repo), "--package", PACKAGE,
                              timeout=1800)
            actions = {(o["type"], o["name"]): o["action"] for o in data["objects"]}
            assert len(actions) == 8, (action, actions)
            assert set(actions.values()) == {"unchanged"}, (action, actions)

    def test_export_deploy_export_is_byte_identical(self, cli, tmp_path):
        first = tmp_path / "first"
        cli.run_ok("abapgit", "export", str(first), "--package", PACKAGE,
                   "--object", ",".join(self._keys()), timeout=1800)
        # Deploy what was exported, then export again.
        cli.run_ok("abapgit", "deploy", str(first), "--package", PACKAGE, timeout=1800)
        second = tmp_path / "second"
        cli.run_ok("abapgit", "export", str(second), "--package", PACKAGE,
                   "--object", ",".join(self._keys()), timeout=1800)

        def files(root):
            return {p.relative_to(root).as_posix(): p.read_bytes()
                    for p in root.rglob("*") if p.is_file()}

        a, b = files(first), files(second)
        assert a.keys() == b.keys()
        assert [k for k in a if a[k] != b[k]] == []
        # ... and both equal the files in git.
        fixtures = {k: v for k, v in files(FIXTURES).items() if not k.endswith("package.devc.xml")}
        assert {k: v for k, v in a.items() if k != ".abapgit.xml"} == \
               {k: v for k, v in fixtures.items() if k != ".abapgit.xml"}
