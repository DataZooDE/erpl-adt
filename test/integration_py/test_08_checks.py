"""ATC quality check tests — validate check execution via CLI."""

import json
import os

import pytest


def run_check(cli, *args):
    """`check` on SAP standard code: the run must work and answer JSON; it may
    exit 8 if this release rates something there priority 1 (crew F9)."""
    result = cli.run("check", *args)
    assert result.returncode in (0, 8), result.stdout + result.stderr
    return json.loads(result.stdout)


@pytest.mark.checks
@pytest.mark.slow
class TestAtcChecks:

    def test_run_atc_check(self, cli):
        """check run on a standard class succeeds."""
        data = run_check(cli, "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "worklist_id" in data
        assert "error_count" in data
        assert "warning_count" in data

    def test_run_atc_check_by_name(self, cli):
        """check with object name resolves via search."""
        data = run_check(cli, "CL_ABAP_RANDOM")
        assert "worklist_id" in data
        assert "error_count" in data

    def test_check_has_findings_list(self, cli):
        """ATC check result has a findings array."""
        data = run_check(cli, "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "findings" in data
        assert isinstance(data["findings"], list)

    def test_check_with_variant(self, cli):
        """check run with --variant runs that variant and says so.

        It used to pass --variant DEFAULT, which does not exist on A4H: SAP
        ran other checks without a word, and the test passed on that.
        """
        variant = os.getenv("SAP_ATC_VARIANT", "ZABAP_CLOUD_DEVELOPMENT")
        data = run_check(cli, "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random",
                          "--variant", variant)
        assert "worklist_id" in data
        assert data["variant"] == variant

    def test_finding_has_priority(self, cli):
        """ATC findings (if any) have priority."""
        data = run_check(cli, "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        for f in data.get("findings", []):
            assert "priority" in f
            assert isinstance(f["priority"], int)
