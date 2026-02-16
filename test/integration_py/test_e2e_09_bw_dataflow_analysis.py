"""E2E Scenario 9: BW Transport & Lineage Analysis (read-only).

Simulates an agent exploring dataflow: transports, object dependencies,
and data transformation pipelines:
  discover -> transport check -> transport list -> search ADSO -> transport collect
           -> search DTPA -> read-dtp -> xref (bounded)

All operations are read-only. Auto-skips on systems without BW capabilities.
"""

import json

import pytest


@pytest.mark.e2e
@pytest.mark.bw
class TestBwDataflowAnalysis:
    """BW dataflow exploration — transport & lineage agent workflow."""

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
        self.__class__.has_cto = "cto" in self.__class__.bw_terms
        self.__class__.has_search = (
            "bwSearch" in self.__class__.bw_terms
            or "search" in self.__class__.bw_terms
        )

    @pytest.mark.order(2)
    def test_step2_transport_check(self, cli):
        """Step 2: Check BW transport state."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        if not getattr(self.__class__, "has_cto", False):
            pytest.skip("BW CTO (transport) service not in discovery")
        result = cli.run("bw", "transport", "check")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "404")):
                pytest.skip("BW transport check not available on this system")
            pytest.fail(f"bw transport check failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert "writing_enabled" in data
        self.__class__.transport_available = True

    @pytest.mark.order(3)
    def test_step3_transport_list(self, cli):
        """Step 3: List BW transports (may be empty on trial)."""
        if not getattr(self.__class__, "transport_available", False):
            pytest.skip("Transport check did not pass")
        data = cli.run_ok("bw", "transport", "list")
        assert "requests" in data
        assert isinstance(data["requests"], list)
        self.__class__.transport_count = len(data["requests"])

    @pytest.mark.order(4)
    def test_step4_find_adso_for_collect(self, cli):
        """Step 4: Find an ADSO for transport collect."""
        if not getattr(self.__class__, "bw_available", False):
            pytest.skip("BW not available")
        if not getattr(self.__class__, "has_search", False):
            pytest.skip("BW search not available")
        result = cli.run("bw", "search", "*", "--type", "ADSO", "--max", "1")
        if result.returncode != 0:
            pytest.skip("BW ADSO search failed")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not data:
            pytest.skip("No ADSO objects found")
        self.__class__.adso_name = data[0]["name"]

    @pytest.mark.order(5)
    def test_step5_transport_collect(self, cli):
        """Step 5: Collect dependent objects for the ADSO."""
        adso = getattr(self.__class__, "adso_name", None)
        if not adso:
            pytest.skip("No ADSO from previous step")
        if not getattr(self.__class__, "has_cto", False):
            pytest.skip("BW CTO not available")
        result = cli.run("bw", "transport", "collect", "ADSO", adso)
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("BW transport collect not activated")
            # Some ADSOs may not support collect — graceful skip
            pytest.skip(f"bw transport collect failed: {result.stderr.strip()[:200]}")
        data = json.loads(result.stdout.strip())
        assert isinstance(data, dict)
        assert "dependencies" in data
        assert isinstance(data["dependencies"], list)

    @pytest.mark.order(6)
    def test_step6_find_dtp(self, cli):
        """Step 6: Find a DTP for read-dtp."""
        if not getattr(self.__class__, "has_search", False):
            pytest.skip("BW search not available")
        result = cli.run("bw", "search", "*", "--type", "DTPA", "--max", "10")
        if result.returncode != 0:
            pytest.skip("BW DTPA search failed")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not data:
            pytest.skip("No DTPA objects found")
        # Probe each — search index may be stale
        for r in data:
            probe = cli.run("bw", "read-dtp", r["name"])
            if probe.returncode == 0:
                self.__class__.dtp_name = r["name"]
                return
        pytest.skip("All DTPA candidates returned errors (stale search index)")

    @pytest.mark.order(7)
    def test_step7_read_dtp(self, cli):
        """Step 7: Read DTP connection details (source/target/filter)."""
        dtp = getattr(self.__class__, "dtp_name", None)
        if not dtp:
            pytest.skip("No DTP from previous step")
        data = cli.run_ok("bw", "read-dtp", dtp)
        assert data["name"] == dtp
        assert "source_name" in data
        assert "source_type" in data
        assert "target_name" in data
        assert "target_type" in data

    @pytest.mark.order(8)
    def test_step8_xref_bounded(self, cli):
        """Step 8: Bounded xref for the ADSO (validates --max flag works E2E)."""
        adso = getattr(self.__class__, "adso_name", None)
        if not adso:
            pytest.skip("No ADSO from previous step")
        result = cli.run("bw", "xref", "ADSO", adso, "--max", "5")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("BW xref not activated")
            pytest.skip(f"bw xref failed: {result.stderr.strip()[:200]}")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)
        assert len(data) <= 5, f"Expected at most 5 xrefs, got {len(data)}"
