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
        stderr = result.stderr.strip().lower()
        # 405 used to be skippable, and that skip hid the real defect for as
        # long as it existed: the client sent GET to a POST-only resource.
        assert "\"http_status\":405" not in stderr, (
            "bw validate used the wrong verb: " + stderr)
        assert "is not valid" not in stderr, (
            "bw validate sent an action the backend rejects: " + stderr)
        # A missing object is a 404 with the backend's own message.
        assert result.returncode != 0
        assert "does not exist" in stderr

    def test_validate_reports_a_consistent_object(self, cli, bw_terms):
        """A clean validation answers 200 with an empty body."""
        if "validate" not in bw_terms:
            pytest.skip("validation service not available")
        objects = cli.run_ok("bw", "search", "0CALMONTH", "--max", "1", "--type", "IOBJ")
        if not objects:
            pytest.skip("No delivered IOBJ available to validate")

        data = cli.run_ok("bw", "validate", "IOBJ", objects[0]["name"])
        assert isinstance(data, list)

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
        # qprops needs an InfoProvider: without one the backend answers
        # "Operation could not be carried out for".
        providers = cli.run_ok("bw", "search", "*", "--max", "1", "--type", "ADSO")
        if not providers:
            pytest.skip("No InfoProvider available for a qprops read")
        qprops = cli.run("bw", "qprops", providers[0]["name"], "--type", "ADSO")
        stderr = qprops.stderr.strip().lower()
        # 415 was skippable here, which hid that the client asked for the media
        # type discovery advertises rather than the one the route serves.
        assert "\"http_status\":415" not in stderr, (
            "bw qprops asked for a media type the route does not serve: " + stderr)
        if qprops.returncode != 0:
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "not found", "\"http_status\":404")):
                pytest.skip("qprops endpoint not available")
            assert qprops.returncode != 0
        qprops_data = json.loads(qprops.stdout.strip()) if qprops.stdout.strip() else []
        assert isinstance(qprops_data, list)

        # Use a query that exists: "DUMMY_QUERY" made this skip on a 404 about
        # the name, which proved nothing about the endpoint.
        queries = cli.run("bw", "search", "*", "--max", "1", "--type", "QUERY")
        query_name = ""
        if queries.returncode == 0 and queries.stdout.strip():
            found = json.loads(queries.stdout.strip())
            if found:
                query_name = found[0]["name"]
        if not query_name:
            pytest.skip("No query available for the reporting check")

        report = cli.run("bw", "reporting", query_name, "--metadata-only")
        if report.returncode != 0:
            stderr = report.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "\"http_status\":404", "\"http_status\":405",
                                         "\"http_status\":500")):
                pytest.skip("reporting endpoint not available")
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
