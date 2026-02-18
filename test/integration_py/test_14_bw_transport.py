"""BW transport and job command tests."""

import json

import pytest


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

    def test_transport_check_protocol_flags(self, cli, bw_has_cto):
        """bw transport check supports rddetails/rdprops/allmsgs flags."""
        data = cli.run_ok("bw", "transport", "check",
                          "--rddetails", "objs", "--rdprops", "--allmsgs")
        assert "writing_enabled" in data

    def test_transport_write_missing_transport(self, cli):
        """bw transport write without --transport fails with usage error."""
        result = cli.run("bw", "transport", "write", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_transport_missing_subaction(self, cli):
        """bw transport without sub-action fails."""
        result = cli.run("bw", "transport")
        assert result.returncode != 0


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

    def test_transport_collect_missing_args(self, cli):
        """bw transport collect without type and name fails."""
        result = cli.run("bw", "transport", "collect")
        assert result.returncode != 0


# ===========================================================================
# BW Jobs
# ===========================================================================

@pytest.mark.bw
class TestBwJobs:

    @pytest.fixture(scope="class")
    def bw_jobs_available(self, cli, bw_available):
        result = cli.run("bw", "job", "list")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("BW jobs service not activated")
            pytest.skip("BW jobs service unavailable on this system")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        return data

    def test_job_list_returns_array(self, cli, bw_jobs_available):
        assert isinstance(bw_jobs_available, list)

    def test_job_list_items_have_guid_when_present(self, bw_jobs_available):
        if not bw_jobs_available:
            pytest.skip("No jobs available for result/step checks")
        assert "guid" in bw_jobs_available[0]

    def test_job_result_for_existing_job(self, cli, bw_jobs_available):
        if not bw_jobs_available:
            pytest.skip("No jobs available for result test")
        guid = bw_jobs_available[0].get("guid")
        if not guid:
            pytest.skip("First job has no guid")
        data = cli.run_ok("bw", "job", "result", guid)
        assert data.get("guid") == guid

    def test_job_step_validates_args(self, cli):
        result = cli.run("bw", "job", "step", "GUID_ONLY")
        assert result.returncode != 0

    def test_job_missing_subaction(self, cli):
        """bw job without sub-action fails."""
        result = cli.run("bw", "job")
        assert result.returncode != 0
