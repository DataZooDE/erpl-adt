"""E2E Scenario 6: Explore a BW System (read-only).

Simulates an agent discovering and navigating a SAP BW/4HANA system:
  discover -> search -> read object -> read-adso -> xref -> nodes -> read-trfn

Auto-skips on systems without BW capabilities. All operations are read-only.
"""

import json

import pytest


@pytest.mark.e2e
@pytest.mark.bw
class TestBwExplore:
    """Read-only BW system exploration — agent first-contact workflow."""

    @pytest.mark.order(1)
    def test_step1_discover_bw_services(self, cli):
        """Step 1: Discover BW modeling services."""
        result = cli.run("bw", "discover")
        if result.returncode != 0:
            pytest.skip("BW Modeling API not available on this system")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not data:
            pytest.skip("BW discovery returned no services")
        assert len(data) > 0
        # Store discovered services for later steps
        self.__class__.bw_services = {s.get("term", "") for s in data}
        self.__class__.bw_has_search = (
            "bwSearch" in self.__class__.bw_services
            or "search" in self.__class__.bw_services
        )

    @pytest.mark.order(2)
    def test_step2_search_bw_objects(self, cli):
        """Step 2: Search for BW objects, store first ADSO."""
        if not getattr(self.__class__, "bw_has_search", False):
            pytest.skip("BW search service not available")
        data = cli.run_ok("bw", "search", "*", "--max", "5")
        assert isinstance(data, list)
        if not data:
            pytest.skip("No BW objects found")
        self.__class__.search_results = data
        # Find an ADSO specifically
        adso = None
        for r in data:
            if r.get("type") == "ADSO":
                adso = r
                break
        # If no ADSO in first 5 results, search specifically
        if not adso:
            adso_results = cli.run_ok("bw", "search", "*", "--max", "1",
                                       "--type", "ADSO")
            if adso_results:
                adso = adso_results[0]
        self.__class__.known_adso = adso

    @pytest.mark.order(3)
    def test_step3_read_object_metadata(self, cli):
        """Step 3: Read metadata for the known ADSO."""
        adso = getattr(self.__class__, "known_adso", None)
        if not adso:
            pytest.skip("No ADSO found in previous step")
        name = adso["name"]
        data = cli.run_ok("bw", "read", "ADSO", name)
        assert data["name"] == name
        assert data["type"] == "ADSO"
        assert "version" in data
        self.__class__.adso_name = name

    @pytest.mark.order(4)
    def test_step4_read_adso_fields(self, cli):
        """Step 4: Read structured ADSO field list via read-adso."""
        name = getattr(self.__class__, "adso_name", None)
        if not name:
            pytest.skip("No ADSO available from previous step")
        data = cli.run_ok("bw", "read-adso", name)
        assert data["name"] == name
        assert "fields" in data
        assert isinstance(data["fields"], list)
        self.__class__.adso_fields = data["fields"]

    @pytest.mark.order(5)
    def test_step5_xref_cross_references(self, cli):
        """Step 5: Discover cross-references for the ADSO."""
        name = getattr(self.__class__, "adso_name", None)
        if not name:
            pytest.skip("No ADSO available from previous step")
        result = cli.run("bw", "xref", "ADSO", name)
        if result.returncode != 0:
            # xref may not be activated — store empty and continue
            self.__class__.xrefs = []
            pytest.skip("BW xref not supported on this system")
        xrefs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(xrefs, list)
        self.__class__.xrefs = xrefs

    @pytest.mark.order(6)
    def test_step6_nodes_child_components(self, cli):
        """Step 6: Enumerate child components of the ADSO."""
        name = getattr(self.__class__, "adso_name", None)
        if not name:
            pytest.skip("No ADSO available from previous step")
        result = cli.run("bw", "nodes", "ADSO", name)
        if result.returncode != 0:
            self.__class__.nodes = []
            pytest.skip("BW nodes not supported on this system")
        nodes = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(nodes, list)
        self.__class__.nodes = nodes
        # Look for a TRFN child to explore in the next step
        trfn = None
        for n in nodes:
            if n.get("type") == "TRFN":
                trfn = n
                break
        self.__class__.found_trfn = trfn

    @pytest.mark.order(7)
    def test_step7_read_transformation(self, cli):
        """Step 7: Read transformation detail if a TRFN was found."""
        trfn = getattr(self.__class__, "found_trfn", None)
        candidates = []
        if trfn:
            candidates.append(trfn["name"])

        # Fallback: search for TRFNs and probe each
        if not candidates:
            result = cli.run("bw", "search", "*", "--max", "10",
                             "--type", "TRFN")
            if result.returncode != 0:
                pytest.skip("No TRFN available for lineage read")
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            if not data:
                pytest.skip("No TRFN found via search")
            candidates = [r["name"] for r in data]

        # Probe each candidate — search index may be stale (404 on read)
        for name in candidates:
            probe = cli.run("bw", "read-trfn", name)
            if probe.returncode == 0:
                data = json.loads(probe.stdout.strip())
                assert data["name"] == name
                assert "source_name" in data
                assert "target_name" in data
                assert "rules" in data
                return
        pytest.skip("All TRFN candidates returned 404 (stale search index)")
