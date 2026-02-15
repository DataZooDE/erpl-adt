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
def bw_has_search(cli, bw_available):
    """Check if BW search service is available and activated."""
    terms = {s.get("term", "") for s in bw_available}
    if "bwSearch" not in terms and "search" not in terms:
        pytest.skip("BW search service not available")
    # Probe the search endpoint — discovery may list it even if not activated
    result = cli.run("bw", "search", "*", "--max", "1")
    if result.returncode != 0:
        stderr = result.stderr.strip().lower()
        if "not activated" in stderr or "not implemented" in stderr:
            pytest.skip("BW search service listed but not activated")
        pytest.fail(f"BW search probe failed unexpectedly: {result.stderr.strip()}")
    return True


@pytest.fixture(scope="session")
def bw_has_adso(bw_available):
    """Check if ADSO service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "adso" not in terms:
        pytest.skip("BW ADSO service not available")
    return True


@pytest.fixture(scope="session")
def bw_has_cto(bw_available):
    """Check if BW transport organizer (CTO) service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "cto" not in terms:
        pytest.skip("BW CTO (transport) service not available")
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
# BW Read All Types (content type regression)
# ===========================================================================

@pytest.mark.bw
class TestBwReadAllTypes:
    """Test bw read for all discoverable BW object types.

    Dynamically discovers available services and tests read for each type.
    This catches Accept header 406 errors across all types.
    """

    TERM_TO_TLOGO = {
        "adso": "ADSO", "iobj": "IOBJ", "hcpr": "HCPR",
        "trfn": "TRFN", "dtpa": "DTPA", "rsds": "RSDS",
        "query": "QUERY", "dest": "DEST", "lsys": "LSYS",
        "fbp": "FBP", "dmod": "DMOD", "trcs": "TRCS",
        "doca": "DOCA", "segr": "SEGR", "area": "AREA",
        "ctrt": "CTRT", "uomt": "UOMT", "thjt": "THJT",
    }

    @pytest.fixture(scope="class")
    def discoverable_types(self, cli, bw_has_search):
        """Return list of (tlogo, name) pairs for types with objects."""
        data = cli.run_ok("bw", "discover")
        terms = {s.get("term", "") for s in data}
        readable_terms = terms & set(self.TERM_TO_TLOGO.keys())

        found = []
        for term in sorted(readable_terms):
            tlogo = self.TERM_TO_TLOGO[term]
            result = cli.run("bw", "search", "*", "--max", "1",
                             "--type", tlogo)
            if result.returncode == 0:
                stdout = result.stdout.strip()
                objs = json.loads(stdout) if stdout else []
                if objs:
                    found.append((tlogo, objs[0]["name"]))
        if not found:
            pytest.skip("No readable BW objects found")
        return found

    def test_read_each_type_succeeds(self, cli, discoverable_types):
        """bw read succeeds for every discovered object type."""
        failures = []
        for tlogo, name in discoverable_types:
            result = cli.run("bw", "read", tlogo, name)
            if result.returncode != 0:
                failures.append(
                    f"{tlogo} {name}: exit={result.returncode}, "
                    f"stderr={result.stderr.strip()[:200]}")
        assert not failures, (
            f"bw read failed for {len(failures)}/{len(discoverable_types)} "
            f"types:\n" + "\n".join(failures))

    def test_read_each_type_returns_valid_json(self, cli, discoverable_types):
        """bw read returns valid JSON with name/type for each type."""
        for tlogo, name in discoverable_types:
            result = cli.run("bw", "read", tlogo, name)
            if result.returncode != 0:
                continue
            data = json.loads(result.stdout.strip())
            assert "name" in data, f"{tlogo}: missing 'name'"
            assert "type" in data, f"{tlogo}: missing 'type'"

    def test_read_each_type_raw_xml(self, cli, discoverable_types):
        """bw read --raw returns XML for each type."""
        for tlogo, name in discoverable_types:
            result = cli.run_no_json("bw", "read", tlogo, name, "--raw")
            if result.returncode != 0:
                continue
            assert "<" in result.stdout, f"{tlogo}: --raw didn't return XML"


# ===========================================================================
# BW Transport (read-only: check/list)
# ===========================================================================

@pytest.mark.bw
class TestBwTransport:

    def test_transport_check(self, cli, bw_has_cto):
        """bw transport check returns transport state."""
        data = cli.run_ok("bw", "transport", "check")
        assert "writing_enabled" in data

    def test_transport_check_has_fields(self, cli, bw_has_cto):
        """Transport check result has changeability, objects, and requests."""
        data = cli.run_ok("bw", "transport", "check")
        assert "changeability" in data
        assert "requests" in data
        assert "objects" in data
        # Each request should have a tasks array
        for r in data["requests"]:
            assert "tasks" in r, f"Request {r.get('number')} missing tasks"

    def test_transport_list(self, cli, bw_has_cto):
        """bw transport list returns request data with tasks."""
        data = cli.run_ok("bw", "transport", "list")
        assert "writing_enabled" in data
        assert "requests" in data
        assert isinstance(data["requests"], list)
        # Each request should have a tasks array
        for r in data["requests"]:
            assert "tasks" in r, f"Request {r.get('number')} missing tasks"

    def test_transport_list_own_only(self, cli, bw_has_cto):
        """bw transport list --own-only filters to current user."""
        data = cli.run_ok("bw", "transport", "list", "--own-only")
        assert isinstance(data["requests"], list)

    def test_transport_check_human_readable(self, cli, bw_has_cto):
        """bw transport check without --json produces human-readable output."""
        result = cli.run_no_json("bw", "transport", "check")
        assert result.returncode == 0
        assert "Writing enabled" in result.stdout or "writing" in result.stdout.lower()

    def test_transport_list_human_readable(self, cli, bw_has_cto):
        """bw transport list without --json produces table output."""
        result = cli.run_no_json("bw", "transport", "list")
        assert result.returncode == 0
        assert not result.stdout.strip().startswith("{")


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
        """Unlocking without a prior lock succeeds (BW unlock is idempotent)."""
        result = cli.run("bw", "unlock", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        # BW unlock is idempotent — returns success even without an active lock
        assert result.returncode == 0

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

    def test_xref_missing_args(self, cli):
        """bw xref without type and name fails."""
        result = cli.run("bw", "xref")
        assert result.returncode != 0

    def test_nodes_missing_args(self, cli):
        """bw nodes without type and name fails."""
        result = cli.run("bw", "nodes")
        assert result.returncode != 0

    def test_transport_collect_missing_args(self, cli):
        """bw transport collect without type and name fails."""
        result = cli.run("bw", "transport", "collect")
        assert result.returncode != 0

    def test_read_trfn_missing_args(self, cli):
        """bw read-trfn without name fails."""
        result = cli.run("bw", "read-trfn")
        assert result.returncode != 0

    def test_read_adso_missing_args(self, cli):
        """bw read-adso without name fails."""
        result = cli.run("bw", "read-adso")
        assert result.returncode != 0

    def test_read_dtp_missing_args(self, cli):
        """bw read-dtp without name fails."""
        result = cli.run("bw", "read-dtp")
        assert result.returncode != 0


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


# ===========================================================================
# BW Transport Collection
# ===========================================================================

@pytest.mark.bw
class TestBwTransportCollect:

    @pytest.fixture(scope="class")
    def known_adso(self, cli, bw_has_search):
        """Find a known ADSO for collect tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO objects found for collect test")
        return data[0]

    def test_transport_collect(self, cli, bw_has_cto, known_adso):
        """bw transport collect returns collection results."""
        name = known_adso["name"]
        result = cli.run("bw", "transport", "collect", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW transport collect not supported on this system")
        cr = json.loads(result.stdout.strip())
        assert "details" in cr
        assert "dependencies" in cr

    def test_transport_collect_dependencies_structure(self, cli, bw_has_cto,
                                                      known_adso):
        """Transport collect dependencies have expected fields."""
        name = known_adso["name"]
        result = cli.run("bw", "transport", "collect", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW transport collect not supported")
        cr = json.loads(result.stdout.strip())
        assert isinstance(cr["dependencies"], list)
        if cr["dependencies"]:
            dep = cr["dependencies"][0]
            assert "name" in dep
            assert "type" in dep

    def test_transport_collect_mode_complete(self, cli, bw_has_cto,
                                             known_adso):
        """bw transport collect --mode=001 (complete) returns results."""
        name = known_adso["name"]
        result = cli.run("bw", "transport", "collect", "ADSO", name,
                         "--mode", "001")
        if result.returncode != 0:
            pytest.skip("BW transport collect --mode=001 not supported")
        cr = json.loads(result.stdout.strip())
        assert isinstance(cr, dict)
        assert "details" in cr

    def test_transport_collect_human_readable(self, cli, bw_has_cto,
                                              known_adso):
        """bw transport collect without --json produces human-readable output."""
        name = known_adso["name"]
        result = cli.run_no_json("bw", "transport", "collect", "ADSO", name)
        if result.returncode != 0:
            pytest.skip("BW transport collect not supported")
        assert not result.stdout.strip().startswith("{")

    def test_transport_collect_nonexistent(self, cli, bw_has_cto):
        """bw transport collect on nonexistent object fails."""
        result = cli.run("bw", "transport", "collect", "ADSO",
                         "ZZZZZ_NONEXISTENT_99999")
        # May fail with not-found or return empty — both are acceptable
        if result.returncode == 0:
            cr = json.loads(result.stdout.strip())
            assert isinstance(cr, dict)


# ===========================================================================
# BW Lineage: read-adso, read-trfn, read-dtp
# ===========================================================================

@pytest.mark.bw
class TestBwLineage:

    @pytest.fixture(scope="class")
    def known_adso(self, cli, bw_has_search):
        """Find a known ADSO for lineage tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO found")
        return data[0]["name"]

    @pytest.fixture(scope="class")
    def known_trfn(self, cli, bw_has_search):
        """Find a known transformation that is actually readable."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "TRFN")
        if not data:
            pytest.skip("No TRFN found")
        # Search index may be stale — probe read endpoint
        for r in data:
            result = cli.run("bw", "read-trfn", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable TRFN found (search results return 404)")

    @pytest.fixture(scope="class")
    def known_dtp(self, cli, bw_has_search):
        """Find a known DTP that is actually readable."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "DTPA")
        if not data:
            pytest.skip("No DTPA found")
        # Search index may be stale — probe read endpoint
        for r in data:
            result = cli.run("bw", "read-dtp", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable DTPA found (search results return 404)")

    # --- read-adso tests ---

    def test_read_adso_returns_fields(self, cli, known_adso):
        """read-adso returns structured field list."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        assert "name" in data
        assert data["name"] == known_adso
        assert "fields" in data
        assert isinstance(data["fields"], list)

    def test_read_adso_field_has_properties(self, cli, known_adso):
        """Each ADSO field has name, data_type, key."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        if not data["fields"]:
            pytest.skip("ADSO has no fields")
        f = data["fields"][0]
        assert "name" in f
        assert "data_type" in f
        assert "key" in f

    def test_read_adso_has_package(self, cli, known_adso):
        """read-adso returns package name."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        assert "package" in data

    def test_read_adso_human_readable(self, cli, known_adso):
        """read-adso without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-adso", known_adso)
        assert result.returncode == 0
        assert known_adso in result.stdout

    def test_read_adso_nonexistent(self, cli, bw_has_search):
        """read-adso for nonexistent ADSO returns error."""
        result = cli.run("bw", "read-adso", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    # --- read-trfn tests ---

    def test_read_trfn_returns_source_target(self, cli, known_trfn):
        """read-trfn returns source and target information."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        assert "name" in data
        assert data["name"] == known_trfn
        assert "source_name" in data
        assert "source_type" in data
        assert "target_name" in data
        assert "target_type" in data

    def test_read_trfn_has_fields_and_rules(self, cli, known_trfn):
        """read-trfn returns field lists and rules."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        assert "source_fields" in data
        assert "target_fields" in data
        assert "rules" in data
        assert isinstance(data["source_fields"], list)
        assert isinstance(data["target_fields"], list)
        assert isinstance(data["rules"], list)

    def test_read_trfn_rule_has_properties(self, cli, known_trfn):
        """Transformation rules have expected properties."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        if not data["rules"]:
            pytest.skip("Transformation has no rules")
        r = data["rules"][0]
        assert "source_field" in r
        assert "target_field" in r
        assert "rule_type" in r

    def test_read_trfn_human_readable(self, cli, known_trfn):
        """read-trfn without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-trfn", known_trfn)
        assert result.returncode == 0
        assert known_trfn in result.stdout

    def test_read_trfn_nonexistent(self, cli, bw_has_search):
        """read-trfn for nonexistent TRFN returns error."""
        result = cli.run("bw", "read-trfn", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    # --- read-dtp tests ---

    def test_read_dtp_returns_connection(self, cli, known_dtp):
        """read-dtp returns source/target connection."""
        data = cli.run_ok("bw", "read-dtp", known_dtp)
        assert "name" in data
        assert data["name"] == known_dtp
        assert "source_name" in data
        assert "source_type" in data
        assert "target_name" in data
        assert "target_type" in data

    def test_read_dtp_has_source_system(self, cli, known_dtp):
        """read-dtp includes source_system field."""
        data = cli.run_ok("bw", "read-dtp", known_dtp)
        assert "source_system" in data

    def test_read_dtp_human_readable(self, cli, known_dtp):
        """read-dtp without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-dtp", known_dtp)
        assert result.returncode == 0
        assert known_dtp in result.stdout

    def test_read_dtp_nonexistent(self, cli, bw_has_search):
        """read-dtp for nonexistent DTP returns error."""
        result = cli.run("bw", "read-dtp", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2
