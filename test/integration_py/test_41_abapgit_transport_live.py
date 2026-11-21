"""abapgit deploy on a system with transports: recording (#72) and the
transaction refusal (#73).

Neither can be verified on the A4H trial, so every test here skips unless
the system it needs is named:

  #72  SAP_TRANSPORT_PACKAGE   a transportable Z package with change
                               recording on (A4H answers "Change recording
                               must be activated for package ...").
                               The colleague test also needs a second
                               developer: SAP_COLLEAGUE_USER /
                               SAP_COLLEAGUE_PASSWORD (see conftest).
  #73  SAP_TRAN_AUTH_OBJ       a Z transaction with an authorization object
                               (set up in SE93 - no workbench API creates one)
       SAP_TRAN_AUTH_PACKAGE   its package (default: SAP_TRANSPORT_PACKAGE)

    SAP_PASSWORD=... SAP_TRANSPORT_PACKAGE=ZERPL_TR uv run pytest -v \\
        test_41_abapgit_transport_live.py

What is recorded is read back with `erpl-adt transport show` (#76), i.e. the
request's E071 entries as the Transport Organizer shows them. The requests
the tests create are released at the end (tasks first), and their objects
deleted, so a second run starts clean: an object locked in a modifiable
request cannot be recorded on another one.
"""

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from cli_runner import release_with_tasks

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_fixture"
PACKAGE = os.getenv("SAP_TRANSPORT_PACKAGE", "")
TRAN_AUTH = os.getenv("SAP_TRAN_AUTH_OBJ", "")
TRAN_AUTH_PACKAGE = os.getenv("SAP_TRAN_AUTH_PACKAGE", PACKAGE)

EXIT_CHECK = 8
# docs/abapgit-deploy.md: "9 | transport problem (missing --transport, object
# locked in another request)" - a CTS conflict, unlike an edit lock (6).
EXIT_TRANSPORT = 9

# The tests deploy their own copy of the fixture repository, renamed ZERPL_ ->
# ZERPT_ (a second team's repository): the fixture itself may be deployed in
# $TMP on the same system, and an object cannot live in two packages.
def N(name):
    """The test copy's name for a fixture object name."""
    return name.replace("ZERPL_", "ZERPT_").replace("Z_ERPL_", "Z_ERPT_")


PROG = N("ZERPL_FX_DIALOG")


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result}")


def _recorded(cli, number):
    """Every E071 entry of the request and its tasks, as (pgmid, type, name)
    with the name's padding collapsed ("ZERPL_FX_DIALOG 0200")."""
    data = cli.run_ok("transport", "show", number)
    entries = data["objects"] + [o for t in data["tasks"] for o in t["objects"]]
    return {(o["pgmid"], o["type"], " ".join(o["name"].split())) for o in entries}


def _new_request(cli, package, purpose):
    number = cli.run_ok("transport", "create", "--desc",
                        f"pytest abapgit {purpose} (test_41)",
                        "--package", package)["transport_number"]
    print(f"\n  request for {purpose}: {number}")
    return number


def _release(cli, number):
    """Release a request the way SE09 does - its open tasks first - and
    return its status afterwards. On A4H the request's release answers
    "Error in pre-export methods" (the trial has no export) yet leaves the
    request released; what counts is the status, read back."""
    release_with_tasks(cli, number)
    return cli.run_ok("transport", "show", number)["status"]


def _repo_copy(target):
    """The demo repository, renamed (see N), as a git working copy (the drift
    check reads git)."""
    shutil.copytree(FIXTURES, target)
    for path in sorted(target.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if path.is_file():
            text = path.read_bytes().decode("utf-8")
            renamed = N(text).replace("zerpl_", "zerpt_").replace("z_erpl_", "z_erpt_")
            path.write_bytes(renamed.encode("utf-8"))
        new_name = N(path.name).replace("zerpl_", "zerpt_").replace("z_erpl_", "z_erpt_")
        if new_name != path.name:
            path.rename(path.with_name(new_name))
    for args in (["init", "-q"], ["add", "-A"],
                 ["-c", "user.name=pytest", "-c", "user.email=pytest@localhost",
                  "commit", "-q", "-m", "demo"]):
        subprocess.run(["git", *args], cwd=target, check=True)
    return target


def _replace(path, old, new):
    text = path.read_text(encoding="utf-8")
    assert old in text, f"{old!r} not in {path.name}"
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


# ===========================================================================
# #72 - transport recording
# ===========================================================================

@pytest.mark.abapgit_deploy
@pytest.mark.skipif(not PACKAGE, reason="SAP_TRANSPORT_PACKAGE not set: needs a "
                    "transportable package with change recording (not A4H)")
class TestTransportRecording:
    """Deploy the demo repository into a transportable package the way a team
    would: first deploy on one request, a change on the next."""

    @pytest.fixture(scope="class")
    def ctx(self, cli, tmp_path_factory):
        repo = _repo_copy(tmp_path_factory.mktemp("tr") / "erpl_fixture")
        ctx = {"repo": repo, "requests": []}
        yield ctx
        if ctx.get("last_request"):
            cli.run("abapgit", "delete", str(repo), "--package", PACKAGE,
                    "--transport", ctx["last_request"], "--force")
        for number in ctx["requests"]:
            _release(cli, number)

    @pytest.mark.order(1)
    def test_first_deploy_records_every_object(self, cli, ctx):
        first = _new_request(cli, PACKAGE, "first deploy")
        ctx["first"] = ctx["last_request"] = first

        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", first)

        assert result.returncode == 0, result.stdout + result.stderr
        recorded = _recorded(cli, first)
        for expected in [("R3TR", "DOMA", N("ZERPL_FX_D_STATUS")),
                         ("R3TR", "DTEL", N("ZERPL_FX_E_STATUS")),
                         ("R3TR", "PROG", PROG),
                         ("R3TR", "PROG", N("ZERPL_FX_REPORT")),
                         ("R3TR", "FUGR", N("ZERPL_FX_FG")),
                         ("R3TR", "TRAN", N("ZERPL_FX_DYN")),
                         ("R3TR", "TRAN", N("ZERPL_FX_REP")),
                         ("R3TR", "TRAN", N("ZERPL_FX_PARAM"))]:
            assert expected in recorded, f"{expected} not recorded on {first}: {sorted(recorded)}"

    @pytest.mark.order(2)
    def test_release_of_the_first_request(self, cli, ctx):
        # Until it is released, its objects are locked in it: a change could
        # not be recorded on another request. Releasing it also proves the
        # deploy left nothing inactive (SAP refuses: "Object ... is inactive").
        assert _release(cli, ctx["first"]) == "released"

    @pytest.mark.order(3)
    def test_a_change_records_exactly_the_changed_parts(self, cli, ctx):
        repo = ctx["repo"] / "src"
        _replace(repo / "zerpt_so_display.prog.screen_0200.abap",
                 "PROCESS AFTER INPUT.", "PROCESS AFTER INPUT.\n* changed (test_41)")
        prog_xml = repo / "zerpt_so_display.prog.xml"
        _replace(prog_xml, "<TEXT>Sales Order</TEXT>",          # a GUI title (CUA)
                 "<TEXT>Sales Order (test_41)</TEXT>")
        _replace(prog_xml, "<ENTRY>Customer</ENTRY>",            # a text symbol (TPOOL)
                 "<ENTRY>Customer (test_41)</ENTRY>")
        _replace(repo / "zerpt_fg.fugr.z_erpt_read_order.abap",
                 "FUNCTION z_erpt_read_order.", "FUNCTION z_erpt_read_order.\n* changed (test_41)")
        _replace(repo / "zerpt_po_report.prog.abap",
                 "START-OF-SELECTION.", "* changed (test_41)\nSTART-OF-SELECTION.")
        _replace(repo / "zerpt_d_so_status.doma.xml",
                 "</DDTEXT>", " (test_41)</DDTEXT>")
        _replace(repo / "zerpt_fx_dyn.tran.xml",
                 "<TTEXT>Display Sales Order</TTEXT>",
                 "<TTEXT>Display Sales Order (test_41)</TTEXT>")
        second = _new_request(cli, PACKAGE, "change")
        ctx["second"] = ctx["last_request"] = second
        ctx["requests"].append(second)

        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", second)

        assert result.returncode == 0, result.stdout + result.stderr
        recorded = _recorded(cli, second)
        # A changed program goes on the request whole (R3TR PROG: source,
        # screens, GUI status, text pool), as abapGit records it - the engine
        # calls RS_CORR_INSERT for R3TR PROG before writing any part. A
        # changed function module is recorded alone (LIMU FUNC, via ADT
        # corrNr), DDIC objects and transactions as R3TR.
        expected = {
            ("R3TR", "PROG", PROG),
            ("R3TR", "PROG", N("ZERPL_FX_REPORT")),
            ("LIMU", "FUNC", N("Z_ERPL_FX_READ_ORDER")),   # one module, not the group
            ("R3TR", "DOMA", N("ZERPL_FX_D_STATUS")),
            ("R3TR", "TRAN", N("ZERPL_FX_DYN")),
        }
        assert expected <= recorded, f"missing: {sorted(expected - recorded)}; got {sorted(recorded)}"
        unchanged = {("R3TR", "DTEL", N("ZERPL_FX_E_STATUS")),
                     ("R3TR", "TRAN", N("ZERPL_FX_REP")),
                     ("R3TR", "TRAN", N("ZERPL_FX_PARAM")),
                     ("R3TR", "FUGR", N("ZERPL_FX_FG"))}
        assert not unchanged & recorded, f"unchanged parts recorded: {sorted(unchanged & recorded)}"
        ctx["after_change"] = recorded

    @pytest.mark.order(4)
    def test_redeploy_unchanged_records_nothing_new(self, cli, ctx):
        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", ctx["second"])
        assert result.returncode == 0, result.stdout + result.stderr
        assert _recorded(cli, ctx["second"]) == ctx["after_change"]

    @pytest.mark.order(5)
    def test_a_deleted_screen_is_recorded(self, cli, ctx):
        repo = ctx["repo"] / "src"
        xml = repo / "zerpt_so_display.prog.xml"
        content = xml.read_text(encoding="utf-8")
        start = content.index("<item>\n     <HEADER>\n      <PROGRAM>ZERPT_SO_DISPLAY</PROGRAM>\n"
                              "      <SCREEN>0300</SCREEN>")
        end = content.index("    </item>\n", start) + len("    </item>\n")
        xml.write_text(content[:start] + content[end:].lstrip(" "), encoding="utf-8")
        (repo / "zerpt_so_display.prog.screen_0300.abap").unlink()

        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", ctx["second"], "--force")

        assert result.returncode == 0, result.stdout + result.stderr
        recorded = _recorded(cli, ctx["second"])
        # docs/abapgit-deploy.md: RS_SCRP_DELETE takes no request; the deletion
        # travels with the R3TR PROG entry RS_CORR_INSERT makes first, as in
        # abapGit. A LIMU DYNP entry for 0300 would record it as well.
        assert ("R3TR", "PROG", PROG) in recorded or ("LIMU", "DYNP", f"{PROG} 0300") in recorded, \
            f"screen 0300 deletion not recorded: {sorted(recorded)}"

    @pytest.mark.order(6)
    def test_a_released_request_is_refused(self, cli, ctx):
        _replace(ctx["repo"] / "src" / "zerpt_d_so_status.doma.xml",
                 " (test_41)</DDTEXT>", " (test_41 b)</DDTEXT>")
        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", ctx["first"])
        assert result.returncode == EXIT_TRANSPORT, result.stdout + result.stderr

    @pytest.mark.order(7)
    def test_a_request_that_does_not_exist_is_refused(self, cli, ctx):
        missing = ctx["first"][:4] + "999999"
        result = cli.run("abapgit", "deploy", str(ctx["repo"]), "--package", PACKAGE,
                         "--transport", missing)
        assert result.returncode == EXIT_TRANSPORT, result.stdout + result.stderr

    @pytest.mark.order(8)
    def test_an_object_in_my_request_is_refused_to_a_colleague(
            self, cli, colleague_cli, ctx, tmp_path):
        """A colleague deploys a change to the domain while it is still on my
        open request: SAP refuses to record it on theirs, nothing is written,
        and the refusal names my request (a CTS conflict: exit 9)."""
        if colleague_cli.run("discover", "services").returncode != 0:
            pytest.skip("no colleague user on this system (SAP_COLLEAGUE_USER)")
        theirs_request = _new_request(colleague_cli, PACKAGE, "colleague")
        try:
            theirs = _repo_copy(tmp_path / "theirs")
            domain = theirs / "src" / "zerpt_d_so_status.doma.xml"
            _replace(domain, "</DDTEXT>", " (colleague)</DDTEXT>")

            result = colleague_cli.run("abapgit", "deploy", str(domain), "--package", PACKAGE,
                                       "--transport", theirs_request, "--force")

            assert result.returncode == EXIT_TRANSPORT, result.stdout + result.stderr
            entry = _entry(json.loads(result.stdout), "DOMA", N("ZERPL_FX_D_STATUS"))
            # SAP names the task the object is locked in (TK 247), part of my request.
            mine = {ctx["second"]} | {t["number"] for t in
                                      cli.run_ok("transport", "show", ctx["second"])["tasks"]}
            assert any(n in entry["message"] for n in mine), (mine, entry)
            assert _recorded(colleague_cli, theirs_request) == set()
        finally:
            _release(colleague_cli, theirs_request)
# ===========================================================================
# #73 - a transaction update must not drop its authorization check
# ===========================================================================

@pytest.mark.abapgit_deploy
@pytest.mark.skipif(not (TRAN_AUTH and TRAN_AUTH_PACKAGE),
                    reason="SAP_TRAN_AUTH_OBJ / SAP_TRAN_AUTH_PACKAGE not set: needs a "
                    "transaction with an authorization object, set up in SE93")
class TestTransactionWithAuthorizationObject:
    """An update is delete + RPY_TRANSACTION_INSERT, which cannot write TSTCA
    or the CINFO flags: the deploy must refuse instead of dropping them."""

    def _export(self, cli, target):
        cli.run_ok("abapgit", "export", str(target), "--package", TRAN_AUTH_PACKAGE,
                   "--object", f"TRAN:{TRAN_AUTH}")
        return (target / "src" / f"{TRAN_AUTH.lower()}.tran.xml").read_bytes()

    @pytest.mark.parametrize("force", [False, True], ids=["plain", "force"])
    def test_update_is_refused_and_the_transaction_stays(self, cli, tmp_path, force):
        before = self._export(cli, tmp_path / "before")
        repo = tmp_path / "before"
        tran = repo / "src" / f"{TRAN_AUTH.lower()}.tran.xml"
        _replace(tran, "</TTEXT>", " (test_41)</TTEXT>")
        flags = []
        if not TRAN_AUTH_PACKAGE.startswith("$"):
            flags += ["--transport", _new_request(cli, TRAN_AUTH_PACKAGE, "TRAN refusal")]
        if force:
            flags.append("--force")

        result = cli.run("abapgit", "deploy", str(tran), "--package", TRAN_AUTH_PACKAGE,
                         *flags)

        assert result.returncode == EXIT_CHECK, result.stdout + result.stderr
        entry = _entry(json.loads(result.stdout), "TRAN", TRAN_AUTH)
        # A part the handler cannot write is rejected before anything is
        # written; the deploy reports that as "failed" with category "check"
        # for every type (`refused` is its word for drift and guard rails).
        assert entry["action"] == "failed", entry
        assert entry["category"] == "check", entry
        assert "authorization-object check" in entry["message"], entry
        assert "nothing was written" in entry["message"], entry
        # Unchanged in the system: TSTC, TSTCA and the CINFO flags all come
        # back byte for byte in a fresh export.
        assert self._export(cli, tmp_path / "after") == before
