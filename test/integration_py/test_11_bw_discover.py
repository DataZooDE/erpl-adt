"""BW discover and search command tests."""

import pytest


# ===========================================================================
# BW Discovery
# ===========================================================================

@pytest.mark.bw
class TestBwDiscovery:

    def test_discover_returns_services(self, cli, bw_available):
        """bw discover returns a non-empty list of services."""
        assert len(bw_available) > 0

    def test_discover_service_has_fields(self, cli, bw_available):
        """Each service entry has scheme, term, href."""
        svc = bw_available[0]
        assert "scheme" in svc
        assert "term" in svc
        assert "href" in svc

    def test_discover_json_output(self, cli, bw_available):
        """bw discover --json output is valid JSON array."""
        data = cli.run_ok("bw", "discover")
        assert isinstance(data, list)

    def test_discover_human_readable(self, cli, bw_available):
        """bw discover without --json produces table output."""
        result = cli.run_no_json("bw", "discover")
        assert result.returncode == 0
        # Table output should have headers, not JSON brackets
        stdout = result.stdout
        assert not stdout.strip().startswith("[")


# ===========================================================================
# BW Search
# ===========================================================================

@pytest.mark.bw
class TestBwSearch:

    def test_search_wildcard(self, cli, bw_has_search):
        """bw search with wildcard returns results (or empty list)."""
        data = cli.run_ok("bw", "search", "*", "--max", "5")
        assert isinstance(data, list)

    def test_search_result_has_fields(self, cli, bw_has_search):
        """Search results have expected fields including technical_name."""
        data = cli.run_ok("bw", "search", "*", "--max", "5")
        if not data:
            pytest.skip("No BW objects found to validate fields")
        r = data[0]
        assert "name" in r
        assert "type" in r
        assert "uri" in r
        assert "status" in r
        # technical_name is included when non-empty (most BW objects have it)
        has_technical = any("technical_name" in x for x in data)
        assert has_technical, "Expected at least one result with technical_name"
        # last_changed is optional — not all object types populate it

    def test_search_max_results(self, cli, bw_has_search):
        """--max parameter limits results."""
        data = cli.run_ok("bw", "search", "*", "--max", "2")
        assert len(data) <= 2

    def test_search_type_filter(self, cli, bw_has_search):
        """--type filter narrows results to matching type."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "ADSO")
        for r in data:
            assert r["type"] == "ADSO", f"Expected ADSO, got {r['type']}"

    def test_search_no_results(self, cli, bw_has_search):
        """Search for nonexistent pattern returns empty list."""
        data = cli.run_ok("bw", "search",
                          "ZZZZNONEXISTENT_XXXXX_99999", "--max", "5")
        assert data == []

    def test_search_default_action(self, cli, bw_has_search):
        """'bw *' invokes search as default action."""
        # This tests that SetDefaultAction("bw", "search") works
        data = cli.run_ok("bw", "*", "--max", "3")
        assert isinstance(data, list)

    def test_search_human_readable(self, cli, bw_has_search):
        """bw search without --json produces table output."""
        result = cli.run_no_json("bw", "search", "*", "--max", "3")
        assert result.returncode == 0
        assert not result.stdout.strip().startswith("[")

    def test_search_empty_pattern(self, cli):
        """bw search with no pattern fails."""
        result = cli.run("bw", "search")
        assert result.returncode != 0
