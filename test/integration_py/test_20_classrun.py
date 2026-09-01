"""Integration tests for 'object run' — ABAP console class execution."""

import pytest


@pytest.mark.classrun
class TestClassRun:

    def test_run_flight_data_generator(self, cli):
        """Run /DMO/CL_FLIGHT_DATA_GENERATOR and verify non-empty output."""
        data = cli.run_ok("object", "run", "/DMO/CL_FLIGHT_DATA_GENERATOR")
        assert "output" in data
        assert isinstance(data["output"], str)
        assert len(data["output"]) > 0

    def test_run_nonexistent_class(self, cli):
        """A class that is not there is a failure, not console output.

        classrun answers HTTP 200 and puts "Object X of type CLAS does not
        exist." in the *output*, which read literally says the run succeeded.
        `object run` now checks existence first and exits 2. The contract
        itself lives in test_24_missing_target_contract.py; this pins that the
        message still names the cause.
        """
        result = cli.run("object", "run", "ZZZZ_NONEXISTENT_99999")
        assert result.returncode != 0
        assert "does not exist" in (result.stdout + result.stderr).lower()

    def test_run_plain_text_output(self, cli):
        """Without --json flag, output is printed directly to stdout."""
        result = cli.run_no_json("object", "run", "/DMO/CL_FLIGHT_DATA_GENERATOR")
        assert result.returncode == 0
        assert len(result.stdout) > 0
