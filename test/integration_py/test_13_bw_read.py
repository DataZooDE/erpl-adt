"""BW read command tests — generic object read and all-types regression."""

import json

import pytest


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

    def test_read_missing_args(self, cli):
        """bw read without type and name fails."""
        result = cli.run("bw", "read")
        assert result.returncode != 0


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
