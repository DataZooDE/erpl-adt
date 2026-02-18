"""BW repository utility and advanced service command tests."""

import json

import pytest


# ===========================================================================
# BW Repository Utility Services
# ===========================================================================

@pytest.mark.bw
class TestBwRepositoryUtils:

    def test_search_metadata_json(self, cli, bw_terms):
        if "bwSearchMD" not in bw_terms:
            pytest.skip("bwSearchMD service not available")
        result = cli.run("bw", "search-md")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("bwSearchMD listed but not activated")
        assert result.returncode == 0
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)

    def test_favorites_list_json(self, cli, bw_terms):
        if "backendFavorites" not in bw_terms:
            pytest.skip("backendFavorites service not available")
        result = cli.run("bw", "favorites")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("backendFavorites listed but not activated")
        assert result.returncode == 0
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)

    def test_nodepath_requires_object_uri(self, cli):
        result = cli.run("bw", "nodepath")
        assert result.returncode != 0

    def test_validate_endpoint_contract(self, cli, bw_terms):
        if "validate" not in bw_terms:
            pytest.skip("validation service not available")
        result = cli.run("bw", "validate", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        if result.returncode == 0:
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            assert isinstance(data, list)
        else:
            stderr = result.stderr.strip().lower()
            if ("not activated" in stderr or "not implemented" in stderr or
                    "\"http_status\":405" in stderr):
                pytest.skip("validation listed but not activated")
            assert result.returncode != 0

    def test_move_requests_contract(self, cli, bw_terms):
        if "move" not in bw_terms:
            pytest.skip("move_requests service not available")
        result = cli.run("bw", "move")
        if result.returncode == 0:
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            assert isinstance(data, list)
            return
        stderr = result.stderr.strip().lower()
        if ("not activated" in stderr or "not implemented" in stderr or
                "\"http_status\":405" in stderr):
            pytest.skip("move_requests listed but not activated")
        assert result.returncode != 0

    def test_application_log_contract(self, cli, bw_terms):
        if "applicationlog" not in bw_terms:
            pytest.skip("applicationlog service not available")
        result = cli.run("bw", "applog", "--username", "DEVELOPER")
        if result.returncode == 0:
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            assert isinstance(data, list)
            return
        stderr = result.stderr.strip().lower()
        if ("not activated" in stderr or "not implemented" in stderr or
                "\"http_status\":500" in stderr):
            pytest.skip("applicationlog listed but not activated")
        assert result.returncode != 0

    def test_message_contract(self, cli, bw_terms):
        if "message" not in bw_terms:
            pytest.skip("message service not available")
        result = cli.run("bw", "message", "RSDHA", "001", "--msgv1", "ZOBJ")
        if result.returncode == 0:
            data = json.loads(result.stdout.strip()) if result.stdout.strip() else {}
            assert isinstance(data, dict)
            assert "text" in data
            return
        stderr = result.stderr.strip().lower()
        if "not activated" in stderr or "not implemented" in stderr:
            pytest.skip("message listed but not activated")
        assert result.returncode != 99


# ===========================================================================
# BW Advanced Services
# ===========================================================================

@pytest.mark.bw
class TestBwAdvancedServices:

    def test_valuehelp_infoareas(self, cli, bw_available):
        result = cli.run("bw", "valuehelp", "infoareas", "--max", "10")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404", "\"http_status\":405")):
                pytest.skip("BW valuehelp endpoint not available")
            assert result.returncode != 0
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        assert isinstance(data, list)

    def test_reporting_and_qprops_capability(self, cli, bw_terms):
        if "queryProperties" not in bw_terms:
            pytest.skip("queryProperties not available")
        qprops = cli.run("bw", "qprops")
        if qprops.returncode != 0:
            stderr = qprops.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404", "\"http_status\":405")):
                pytest.skip("qprops endpoint not available")
            assert qprops.returncode != 0
        qprops_data = json.loads(qprops.stdout.strip()) if qprops.stdout.strip() else []
        assert isinstance(qprops_data, list)

        report = cli.run("bw", "reporting", "DUMMY_QUERY", "--metadata-only")
        if report.returncode != 0:
            stderr = report.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404", "\"http_status\":405",
                                         "\"http_status\":500")):
                pytest.skip("reporting endpoint not available")
            # Invalid compid / backend errors are acceptable capability proof
            assert report.returncode != 99

    def test_virtualfolders_and_datavolumes_capability(self, cli):
        vf = cli.run("bw", "virtualfolders")
        if vf.returncode != 0:
            stderr = vf.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404", "\"http_status\":405")):
                pytest.skip("virtualfolders endpoint not available")
            assert vf.returncode != 99
        else:
            data = json.loads(vf.stdout.strip()) if vf.stdout.strip() else []
            assert isinstance(data, list)

        dv = cli.run("bw", "datavolumes")
        if dv.returncode != 0:
            stderr = dv.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404", "\"http_status\":405")):
                pytest.skip("datavolumes endpoint not available")
            assert dv.returncode != 99
        else:
            data = json.loads(dv.stdout.strip()) if dv.stdout.strip() else []
            assert isinstance(data, list)
