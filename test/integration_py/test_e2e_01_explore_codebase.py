"""E2E Scenario 1: Explore an Unknown Codebase (read-only).

Simulates an agent landing on a new SAP system and mapping what's there:
  discover -> package exists -> package list -> search -> object read -> source read
"""

import pytest


@pytest.mark.e2e
class TestExploreCodebase:
    """Read-only codebase exploration — the most common agent first-contact workflow."""

    @pytest.mark.order(1)
    def test_step1_discover_services(self, cli):
        """Step 1: Discover system capabilities."""
        data = cli.run_ok("discover", "services")
        assert "workspaces" in data
        assert len(data["workspaces"]) > 0
        assert "has_packages" in data
        # Store for later steps
        self.__class__.capabilities = data

    @pytest.mark.order(2)
    def test_step2_package_exists(self, cli):
        """Step 2: Check the $TMP development package exists."""
        data = cli.run_ok("package", "exists", "$TMP")
        assert data.get("exists") is True

    @pytest.mark.order(3)
    def test_step3_package_list(self, cli):
        """Step 3: List $TMP contents, verify objects are present."""
        data = cli.run_ok("package", "list", "$TMP")
        assert isinstance(data, list)
        # $TMP should have at least some objects
        self.__class__.package_contents = data

    @pytest.mark.order(4)
    def test_step4_search_known_class(self, cli):
        """Step 4: Search for a known standard class (CL_ABAP_RANDOM)."""
        data = cli.run_ok("search", "CL_ABAP_RANDOM", "--max", "5")
        assert isinstance(data, list)
        assert len(data) > 0
        # Find the exact match
        match = None
        for r in data:
            if "CL_ABAP_RANDOM" in r.get("name", "").upper():
                match = r
                break
        assert match is not None, "CL_ABAP_RANDOM not found in search results"
        self.__class__.search_result = match

    @pytest.mark.order(5)
    def test_step5_object_read(self, cli):
        """Step 5: Read the object found by search, verify structure."""
        uri = self.__class__.search_result["uri"]
        data = cli.run_ok("object", "read", uri)
        assert "name" in data
        assert "CL_ABAP_RANDOM" in data["name"].upper()
        assert "type" in data
        assert "uri" in data
        # Construct source URI from object URI (includes may be empty)
        self.__class__.source_uri = uri + "/source/main"

    @pytest.mark.order(6)
    def test_step6_source_read(self, cli):
        """Step 6: Read actual ABAP source, verify it contains CLASS/METHOD keywords."""
        source_uri = self.__class__.source_uri
        data = cli.run_ok("source", "read", source_uri)
        assert "source" in data
        source_text = data["source"].upper()
        assert "CLASS" in source_text, "Source should contain CLASS keyword"
        assert "METHOD" in source_text, "Source should contain METHOD keyword"
