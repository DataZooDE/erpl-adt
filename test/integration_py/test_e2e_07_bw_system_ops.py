"""E2E Scenario 7: BW System & Operations Info (read-only).

Simulates an agent discovering a BW system's capabilities and current state
before performing any modifications:
  discover -> sysinfo -> changeability -> adturi -> locks list -> job list -> search-md

All operations are read-only. Auto-skips on systems without BW capabilities.
"""

import json

import pytest


@pytest.mark.e2e
@pytest.mark.bw
class TestBwSystemOps:
    """Read-only BW system monitoring — agent pre-flight checks."""

    @pytest.mark.order(1)
    def test_step1_gate_bw_available(self, cli):
        """Step 1: Gate — verify BW Modeling API is available."""
        result = cli.run("bw", "discover")
        if result.returncode != 0:
            pytest.skip("BW Modeling API not available on this system")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not data:
            pytest.skip("BW discovery returned no services")
        self.__class__.bw_available = True
        self.__class__.bw_terms = {s.get("term", "") for s in data}

    @pytest.mark.order(2)
    def test_step2_sysinfo(self, cli):
        """Step 2: Retrieve BW system properties (version, hostname, etc.)."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        result = cli.run("bw", "sysinfo")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW sysinfo not available on this system")
            pytest.fail(f"bw sysinfo failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert isinstance(data, (dict, list))
        self.__class__.has_sysinfo = True

    @pytest.mark.order(3)
    def test_step3_changeability(self, cli):
        """Step 3: Retrieve BW changeability settings."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        result = cli.run("bw", "changeability")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW changeability not available on this system")
            pytest.fail(f"bw changeability failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert isinstance(data, (dict, list))

    @pytest.mark.order(4)
    def test_step4_adturi(self, cli):
        """Step 4: Retrieve BW-to-ADT URI mappings."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        result = cli.run("bw", "adturi")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW adturi not available on this system")
            pytest.fail(f"bw adturi failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert isinstance(data, (dict, list))

    @pytest.mark.order(5)
    def test_step5_locks_list(self, cli):
        """Step 5: List active BW locks (may be empty)."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        result = cli.run("bw", "locks", "list")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW locks service not available on this system")
            pytest.fail(f"bw locks list failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)

    @pytest.mark.order(6)
    def test_step6_job_list(self, cli):
        """Step 6: List BW background jobs (may be empty)."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        result = cli.run("bw", "job", "list")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW job service not available on this system")
            pytest.fail(f"bw job list failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)

    @pytest.mark.order(7)
    def test_step7_search_md(self, cli):
        """Step 7: Retrieve BW search metadata (facets/filters)."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        terms = getattr(self.__class__, "bw_terms", set())
        if "bwSearchMD" not in terms:
            pytest.skip("bwSearchMD service not in discovery")
        result = cli.run("bw", "search-md")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404", "\"http_status\":404")):
                pytest.skip("BW search-md not available on this system")
            pytest.fail(f"bw search-md failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)
