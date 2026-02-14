"""BW Modeling API tests — validate BW commands against a live SAP system.

These tests exercise the /sap/bw/modeling/ REST surface. They are skipped
automatically when the target SAP system does not have BW capabilities
(the standard ABAP Cloud Developer Trial does not include BW).

All tests here are read-only — no locks, saves, or deletes.
"""

import json

import pytest


# ---------------------------------------------------------------------------
# Session-scoped BW availability check
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def bw_available(cli):
    """Probe BW discovery endpoint once. Skip all BW tests if unavailable."""
    result = cli.run("bw", "discover")
    if result.returncode != 0:
        pytest.skip("BW Modeling API not available on this system")
    data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
    if not data:
        pytest.skip("BW discovery returned no services")
    return data


@pytest.fixture(scope="session")
def bw_has_search(bw_available):
    """Check if BW search service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "bwSearch" not in terms and "search" not in terms:
        pytest.skip("BW search service not available")
    return True


@pytest.fixture(scope="session")
def bw_has_adso(bw_available):
    """Check if ADSO service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "adso" not in terms:
        pytest.skip("BW ADSO service not available")
    return True


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
        """Search results have expected fields."""
        data = cli.run_ok("bw", "search", "*", "--max", "5")
        if not data:
            pytest.skip("No BW objects found to validate fields")
        r = data[0]
        assert "name" in r
        assert "type" in r

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


# ===========================================================================
# BW Read Object
# ===========================================================================

@pytest.mark.bw
class TestBwRead:

    @pytest.fixture(scope="class")
    def known_object(self, cli, bw_has_search):
        """Find a known active ADSO to use for read tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO objects found for read tests")
        return data[0]

    def test_read_object(self, cli, bw_has_adso, known_object):
        """bw read returns object metadata."""
        name = known_object["name"]
        data = cli.run_ok("bw", "read", "ADSO", name)
        assert data["name"] == name
        assert data["type"] == "ADSO"

    def test_read_has_metadata_fields(self, cli, bw_has_adso, known_object):
        """Read result includes standard metadata fields."""
        name = known_object["name"]
        data = cli.run_ok("bw", "read", "ADSO", name)
        assert "name" in data
        assert "type" in data
        assert "version" in data

    def test_read_active_version(self, cli, bw_has_adso, known_object):
        """Read with --version=a returns active version."""
        name = known_object["name"]
        data = cli.run_ok("bw", "read", "ADSO", name, "--version", "a")
        assert data["name"] == name

    def test_read_raw_xml(self, cli, bw_has_adso, known_object):
        """bw read --raw returns XML content."""
        name = known_object["name"]
        result = cli.run_no_json("bw", "read", "ADSO", name, "--raw")
        assert result.returncode == 0
        assert "<?xml" in result.stdout or "<" in result.stdout

    def test_read_nonexistent_object(self, cli, bw_has_adso):
        """Reading a nonexistent object returns exit code 2 (not found)."""
        result = cli.run("bw", "read", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    def test_read_human_readable(self, cli, bw_has_adso, known_object):
        """bw read without --json prints human-readable summary."""
        name = known_object["name"]
        result = cli.run_no_json("bw", "read", "ADSO", name)
        assert result.returncode == 0
        assert name in result.stdout


# ===========================================================================
# BW Transport (read-only: check/list)
# ===========================================================================

@pytest.mark.bw
class TestBwTransport:

    def test_transport_check(self, cli, bw_available):
        """bw transport check returns transport state."""
        data = cli.run_ok("bw", "transport", "check")
        assert "writing_enabled" in data

    def test_transport_check_has_fields(self, cli, bw_available):
        """Transport check result has changeability and requests."""
        data = cli.run_ok("bw", "transport", "check")
        assert "changeability" in data
        assert "requests" in data

    def test_transport_list(self, cli, bw_available):
        """bw transport list returns request data."""
        data = cli.run_ok("bw", "transport", "list")
        assert "writing_enabled" in data
        assert "requests" in data
        assert isinstance(data["requests"], list)

    def test_transport_list_own_only(self, cli, bw_available):
        """bw transport list --own-only filters to current user."""
        data = cli.run_ok("bw", "transport", "list", "--own-only")
        assert isinstance(data["requests"], list)


# ===========================================================================
# BW Error handling — server-side errors (require BW endpoint)
# ===========================================================================

@pytest.mark.bw
class TestBwServerErrors:

    def test_lock_nonexistent_object(self, cli, bw_available):
        """Locking a nonexistent BW object fails gracefully."""
        result = cli.run("bw", "lock", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode != 0

    def test_unlock_without_lock(self, cli, bw_available):
        """Unlocking without a prior lock fails gracefully."""
        result = cli.run("bw", "unlock", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        # Should fail — no lock to release
        assert result.returncode != 0

    def test_activate_nonexistent_object(self, cli, bw_available):
        """Activating a nonexistent BW object fails."""
        result = cli.run("bw", "activate", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode != 0

    def test_job_status_invalid_guid(self, cli, bw_available):
        """Job status with invalid GUID fails."""
        result = cli.run("bw", "job", "status",
                         "INVALID_GUID_000000000000")
        assert result.returncode != 0


# ===========================================================================
# BW CLI validation — argument/flag errors (no server needed)
# ===========================================================================

@pytest.mark.bw
class TestBwCliValidation:

    def test_transport_write_missing_transport(self, cli):
        """bw transport write without --transport fails with usage error."""
        result = cli.run("bw", "transport", "write", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_save_missing_lock_handle(self, cli):
        """bw save without --lock-handle fails with usage error."""
        result = cli.run("bw", "save", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_delete_missing_lock_handle(self, cli):
        """bw delete without --lock-handle fails with usage error."""
        result = cli.run("bw", "delete", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_read_missing_args(self, cli):
        """bw read without type and name fails."""
        result = cli.run("bw", "read")
        assert result.returncode != 0

    def test_lock_missing_args(self, cli):
        """bw lock without type and name fails."""
        result = cli.run("bw", "lock")
        assert result.returncode != 0

    def test_activate_missing_args(self, cli):
        """bw activate without type and name fails."""
        result = cli.run("bw", "activate")
        assert result.returncode != 0

    def test_transport_missing_subaction(self, cli):
        """bw transport without sub-action fails."""
        result = cli.run("bw", "transport")
        assert result.returncode != 0

    def test_job_missing_subaction(self, cli):
        """bw job without sub-action fails."""
        result = cli.run("bw", "job")
        assert result.returncode != 0

    def test_search_empty_pattern(self, cli):
        """bw search with no pattern fails."""
        result = cli.run("bw", "search")
        assert result.returncode != 0
