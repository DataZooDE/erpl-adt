"""Commands must not report success for a target that does not exist.

Several ADT endpoints answer HTTP 200 for something that is not there and put
the real story in prose: `classrun` returns "Object X of type CLAS does not
exist." as its console output, an ATC or syntax run returns an empty finding
list, a test run returns all_passed with "no test methods", and a transport
release accepts the job without checking the number.

Read literally, each of those says the work succeeded. "No findings" is
indistinguishable from "clean", which is the reading that matters when an
agent runs a check after every edit and mistypes a URI once.

These tests pin the contract against a live system, because that is where the
behaviour lives — the unit tests can only pin what we believe about it.
"""

import pytest


MISSING_CLASS = "/sap/bc/adt/oo/classes/zzz_erpl_missing_99"
MISSING_TRANSPORT = "ZZZK900099"


def _says_missing(result):
    """The failure must name the cause, not just fail."""
    combined = (result.stdout + result.stderr).lower()
    return "does not exist" in combined or "not found" in combined


class TestMissingTargetIsAnError:

    def test_check_run_does_not_report_a_clean_object(self, cli):
        """ATC on a missing object must not answer with an empty finding list."""
        result = cli.run("check", "run", MISSING_CLASS)
        assert result.returncode != 0, (
            "ATC reported success for an object that does not exist: " + result.stdout)
        assert _says_missing(result)
        # The dangerous shape specifically: a clean-looking result.
        assert '"findings":[]' not in result.stdout

    def test_test_run_does_not_report_all_passed(self, cli):
        """A test run on a missing object must not answer all_passed."""
        result = cli.run("test", "run", MISSING_CLASS)
        assert result.returncode != 0, (
            "test run reported success for an object that does not exist: "
            + result.stdout)
        assert _says_missing(result)
        assert '"all_passed":true' not in result.stdout.replace(" ", "")

    def test_source_check_does_not_report_clean_syntax(self, cli):
        """A syntax check on a missing object must not answer with no messages."""
        result = cli.run("source", "check", MISSING_CLASS)
        assert result.returncode != 0, (
            "source check reported success for an object that does not exist: "
            + result.stdout)
        assert _says_missing(result)

    def test_object_run_exits_non_zero(self, cli):
        """classrun prints SAP's message; the exit code must agree with it."""
        result = cli.run("object", "run", "ZZZ_ERPL_MISSING_99")
        assert result.returncode != 0, (
            "object run exited 0 for a class that does not exist")
        assert _says_missing(result)

    def test_transport_release_does_not_claim_a_release(self, cli):
        """Releasing is one-way: never claim it happened when it did not."""
        result = cli.run("transport", "release", MISSING_TRANSPORT)
        assert result.returncode != 0, (
            "transport release reported success for a transport that does not exist")
        assert _says_missing(result)
        assert "released" not in result.stdout.lower()


class TestRealTargetsStillWork:
    """The existence probe must not break the commands it guards."""

    @pytest.fixture(scope="class")
    def known_class(self, cli):
        found = cli.run_ok("search", "CL_ABAP_RANDOM", "--max", "1")
        if not found:
            pytest.skip("No reference class available on this system")
        return "/sap/bc/adt/oo/classes/cl_abap_random"

    def test_check_run_still_runs(self, cli, known_class):
        data = cli.run_ok("check", "run", known_class)
        assert "worklist_id" in data

    def test_source_check_still_runs(self, cli, known_class):
        result = cli.run("source", "check", known_class)
        assert result.returncode == 0, result.stderr

    def test_test_run_still_runs(self, cli, known_class):
        data = cli.run_ok("test", "run", known_class)
        assert "all_passed" in data


class TestTransportOwnerFilter:
    """--user selects the logon user; the owner filter is --owner.

    `transport list --user=ADMIN` re-authenticated as ADMIN with the current
    password and answered HTTP 401 — the example in our own help could never
    have worked.
    """

    def test_default_lists_the_logon_users_transports(self, cli):
        result = cli.run("transport", "list")
        assert result.returncode == 0, result.stderr

    def test_owner_filter_is_accepted(self, cli):
        result = cli.run("transport", "list", "--owner", "DEVELOPER")
        assert result.returncode == 0, result.stderr
        # Whatever comes back belongs to the owner asked for.
        import json
        rows = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        for row in rows:
            assert row.get("owner", "DEVELOPER") == "DEVELOPER"

    def test_owner_filter_does_not_reauthenticate(self, cli):
        """Naming another owner must not turn into an authentication failure."""
        result = cli.run("transport", "list", "--owner", "ZZZNOSUCHUSER")
        # Either an empty list or a real error — but never 401, which is what
        # happened when the filter and the connection user were the same flag.
        assert '"http_status":401' not in result.stderr
        assert result.returncode in (0, 1, 2)
