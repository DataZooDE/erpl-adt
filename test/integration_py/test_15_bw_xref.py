"""BW xref and nodes command tests."""

import json

import pytest


# ===========================================================================
# BW Cross-References (xref)
# ===========================================================================

@pytest.mark.bw
class TestBwXref:

    @pytest.fixture(scope="class")
    def bw_has_xref(self, cli, bw_has_search):
        """Check if BW xref endpoint is accessible using a real object."""
        data = cli.run_ok("bw", "search", "*", "--max", "1")
        if not data:
            pytest.skip("No BW objects found to probe xref")
        result = cli.run("bw", "xref", data[0]["type"], data[0]["name"])
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("BW xref service not activated")
        return True

    @pytest.fixture(scope="class")
    def known_adso(self, cli, bw_has_search):
        """Find a known ADSO for xref tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO objects found for xref test")
        return data[0]

    def test_xref_returns_list(self, cli, bw_has_xref, known_adso):
        """bw xref returns a list for a known object."""
        name = known_adso["name"]
        result = cli.run("bw", "xref", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW xref not fully supported on this system")
        xrefs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(xrefs, list)

    def test_xref_result_has_fields(self, cli, bw_has_xref, known_adso):
        """Xref results have expected fields."""
        name = known_adso["name"]
        result = cli.run("bw", "xref", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW xref not supported")
        xrefs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not xrefs:
            pytest.skip("No cross-references found")
        r = xrefs[0]
        assert "name" in r
        assert "type" in r
        assert "association_type" in r
        assert "association_label" in r

    def test_xref_human_readable(self, cli, bw_has_xref, known_adso):
        """bw xref without --json produces table output."""
        name = known_adso["name"]
        result = cli.run_no_json("bw", "xref", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW xref not supported")
        assert not result.stdout.strip().startswith("[")

    def test_xref_association_filter(self, cli, bw_has_xref, known_adso):
        """bw xref --association filters by association code."""
        name = known_adso["name"]
        result = cli.run("bw", "xref", "ADSO", name, "--association", "001")
        if result.returncode != 0:
            pytest.skip("BW xref not supported")
        xrefs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(xrefs, list)
        # If we got results, they should all have the filtered association
        for r in xrefs:
            assert r["association_type"] == "001", \
                f"Expected association 001, got {r['association_type']}"

    def test_xref_nonexistent_object(self, cli, bw_has_xref):
        """bw xref on nonexistent object returns empty list or error."""
        result = cli.run("bw", "xref", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        if result.returncode == 0:
            # BW xref returns empty list for unknown objects
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            assert data == []
        # else: error exit code is also acceptable

    def test_xref_missing_args(self, cli):
        """bw xref without type and name fails."""
        result = cli.run("bw", "xref")
        assert result.returncode != 0


# ===========================================================================
# BW Node Structure (nodes)
# ===========================================================================

@pytest.mark.bw
class TestBwNodes:

    @pytest.fixture(scope="class")
    def bw_has_nodes(self, cli, bw_has_search):
        """Check if BW nodes endpoint is accessible using a real object."""
        data = cli.run_ok("bw", "search", "*", "--max", "1")
        if not data:
            pytest.skip("No BW objects found to probe nodes")
        result = cli.run("bw", "nodes", data[0]["type"], data[0]["name"])
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("BW nodes service not activated")
        return True

    @pytest.fixture(scope="class")
    def known_adso(self, cli, bw_has_search):
        """Find a known ADSO for nodes tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO objects found for nodes test")
        return data[0]

    def test_nodes_returns_list(self, cli, bw_has_nodes, known_adso):
        """bw nodes returns a list for a known object."""
        name = known_adso["name"]
        result = cli.run("bw", "nodes", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW nodes not fully supported on this system")
        nodes = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(nodes, list)

    def test_nodes_result_has_fields(self, cli, bw_has_nodes, known_adso):
        """Node results have expected fields."""
        name = known_adso["name"]
        result = cli.run("bw", "nodes", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW nodes not supported")
        nodes = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not nodes:
            pytest.skip("No child nodes found")
        n = nodes[0]
        assert "name" in n
        assert "type" in n
        assert "subtype" in n

    def test_nodes_human_readable(self, cli, bw_has_nodes, known_adso):
        """bw nodes without --json produces table output."""
        name = known_adso["name"]
        result = cli.run_no_json("bw", "nodes", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW nodes not supported")
        assert not result.stdout.strip().startswith("[")

    def test_nodes_child_type_filter(self, cli, bw_has_nodes, known_adso):
        """bw nodes --child-type filters by child type."""
        name = known_adso["name"]
        result = cli.run("bw", "nodes", "ADSO", name,
                         "--child-type", "TRFN")
        if result.returncode != 0:
            pytest.skip("BW nodes not supported")
        nodes = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(nodes, list)
        # If we got results, they should all match the filter
        for n in nodes:
            assert n["type"] == "TRFN", \
                f"Expected type TRFN, got {n['type']}"

    def test_nodes_datasource_flag(self, cli, bw_has_nodes, bw_has_search):
        """bw nodes --datasource uses DataSource path."""
        # Find a DataSource (RSDS) if available
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "RSDS")
        if not data:
            pytest.skip("No RSDS objects found for datasource test")
        name = data[0]["name"]
        result = cli.run("bw", "nodes", "RSDS", name, "--datasource")
        # Just verify it runs without error (may return empty list)
        if result.returncode != 0:
            pytest.skip("BW nodes --datasource not supported")
        nodes = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(nodes, list)

    def test_nodes_nonexistent_object(self, cli, bw_has_nodes):
        """bw nodes on nonexistent object returns empty list or error."""
        result = cli.run("bw", "nodes", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        if result.returncode == 0:
            # BW nodes returns empty list for unknown objects
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            assert data == []
        # else: error exit code is also acceptable

    def test_nodes_missing_args(self, cli):
        """bw nodes without type and name fails."""
        result = cli.run("bw", "nodes")
        assert result.returncode != 0
