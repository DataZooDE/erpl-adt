"""ATC quality check tests — validate check execution via CLI."""

import pytest


@pytest.mark.checks
@pytest.mark.slow
class TestAtcChecks:

    def test_run_atc_check(self, cli):
        """check run on a standard class succeeds."""
        data = cli.run_ok("check", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "worklist_id" in data
        assert "error_count" in data
        assert "warning_count" in data

    def test_run_atc_check_by_name(self, cli):
        """check with object name resolves via search."""
        data = cli.run_ok("check", "CL_ABAP_RANDOM")
        assert "worklist_id" in data
        assert "error_count" in data

    def test_check_has_findings_list(self, cli):
        """ATC check result has a findings array."""
        data = cli.run_ok("check", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "findings" in data
        assert isinstance(data["findings"], list)

    def test_check_with_variant(self, cli):
        """check run with --variant parameter works."""
        data = cli.run_ok("check", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random",
                          "--variant", "DEFAULT")
        assert "worklist_id" in data

    def test_finding_has_priority(self, cli):
        """ATC findings (if any) have priority."""
        data = cli.run_ok("check", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        for f in data.get("findings", []):
            assert "priority" in f
            assert isinstance(f["priority"], int)
