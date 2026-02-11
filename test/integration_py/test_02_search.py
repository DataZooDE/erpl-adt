"""Search operation tests — validate object search via CLI."""

import pytest


@pytest.mark.search
class TestSearch:

    def test_search_wildcard(self, cli):
        """Wildcard search returns results (short form)."""
        data = cli.run_ok("search", "CL_ABAP_*", "--max", "10")
        assert isinstance(data, list)
        assert len(data) > 0

    def test_search_exact_name(self, cli):
        """Search for CL_ABAP_RANDOM returns a matching result."""
        data = cli.run_ok("search", "CL_ABAP_RANDOM", "--max", "10")
        names = [r["name"] for r in data]
        assert any("CL_ABAP_RANDOM" in n.upper() for n in names)

    def test_search_type_filter(self, cli):
        """Search with --type CLAS filters to class results."""
        data = cli.run_ok("search", "CL_ABAP_*", "--max", "10",
                          "--type", "CLAS")
        for r in data:
            assert "CLAS" in r["type"], f"Expected CLAS type, got {r['type']}"

    def test_search_max_results(self, cli):
        """--max parameter limits the number of results."""
        data = cli.run_ok("search", "CL_*", "--max", "3")
        assert len(data) <= 3

    def test_search_result_has_fields(self, cli):
        """Each search result has name, type, uri, package."""
        data = cli.run_ok("search", "CL_ABAP_RANDOM", "--max", "5")
        assert len(data) > 0
        r = data[0]
        assert "name" in r
        assert "type" in r
        assert "uri" in r
        assert "package" in r

    def test_search_no_results(self, cli):
        """Search for non-existent object returns empty list."""
        data = cli.run_ok("search", "ZNONEXISTENT_XXXXX_99999",
                          "--max", "10")
        assert data == []

    def test_search_human_readable(self, cli):
        """Search without --json=true produces human-readable table output."""
        result = cli.run_no_json("search", "CL_ABAP_RANDOM", "--max", "5")
        assert result.returncode == 0
        # Human-readable output should have table-like content, not JSON
        stdout = result.stdout
        assert not stdout.strip().startswith("["), "Output should not be JSON array"
        assert "CL_ABAP_RANDOM" in stdout.upper()

    def test_search_backward_compat_query(self, cli):
        """Old 'search query' form still works for backward compatibility."""
        data = cli.run_ok("search", "query", "CL_ABAP_RANDOM", "--max", "5")
        assert isinstance(data, list)
        assert len(data) > 0
