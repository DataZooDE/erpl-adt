"""ABAP Unit testing tests — validate test execution via CLI."""

import os
import tempfile

import pytest


@pytest.mark.testing
@pytest.mark.slow
class TestAbapUnit:

    def test_run_tests_for_class(self, cli):
        """test run on a standard class succeeds."""
        data = cli.run_ok("test", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "total_methods" in data
        assert "all_passed" in data

    def test_run_tests_by_name(self, cli):
        """test with object name resolves via search."""
        data = cli.run_ok("test", "CL_ABAP_RANDOM")
        assert "total_methods" in data
        assert "all_passed" in data

    def test_run_tests_by_name_explicit_action(self, cli):
        """test run with object name resolves via search."""
        data = cli.run_ok("test", "run", "CL_ABAP_RANDOM")
        assert "total_methods" in data
        assert "all_passed" in data

    def test_run_tests_nonexistent_object(self, cli):
        """test with nonexistent object name returns exit code 2."""
        result = cli.run("test", "ZZZZ_NONEXISTENT_12345")
        assert result.returncode == 2

    def test_result_has_classes(self, cli):
        """Test result contains classes array."""
        data = cli.run_ok("test", "run",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "classes" in data
        assert isinstance(data["classes"], list)

    def test_failing_test_has_alerts(self, test_class, cli, session_file):
        """Write a failing test and verify test run reports failure."""
        uri = test_class["uri"]
        name = test_class["name"]

        # Lock + write a failing test.
        lock_data = cli.run_ok("object", "lock", uri,
                               session_file=session_file)
        handle = lock_data["handle"]

        source = (
            f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC\n"
            "  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.\n"
            "  PRIVATE SECTION.\n"
            "    METHODS test_fail FOR TESTING.\n"
            "ENDCLASS.\n\n"
            f"CLASS {name} IMPLEMENTATION.\n"
            "  METHOD test_fail.\n"
            "    cl_abap_unit_assert=>assert_equals(\n"
            "      act = 1\n"
            "      exp = 2 ).\n"
            "  ENDMETHOD.\n"
            "ENDCLASS.\n"
        )
        with tempfile.NamedTemporaryFile(mode="w", suffix=".abap",
                                         delete=False) as f:
            f.write(source)
            src_file = f.name

        source_uri = uri + "/source/main"
        try:
            cli.run_ok("source", "write", source_uri,
                       "--file", src_file,
                       "--handle", handle,
                       session_file=session_file)
        finally:
            os.unlink(src_file)

        cli.run_ok("object", "unlock", uri,
                   "--handle", handle,
                   session_file=session_file)

        # Activation must come after the unlock — SAP refuses to activate an
        # object the session is still holding a lock on ("User X is currently
        # editing ..."). It is required: ABAP Unit only discovers test methods
        # in the *active* version, so skipping it leaves SAP with nothing to
        # run. run_ok so a failed activation fails here, loudly, instead of
        # surfacing later as a silently empty test run.
        cli.run_ok("activate", uri)

        # Run tests.
        data = cli.run_ok("test", "run", uri)
        # We just wrote and activated a class with one FOR TESTING method that
        # asserts 1 == 2. The test runner MUST detect it. A silently-zero
        # total_methods would mean the runner is skipping methods it should
        # find — a #9-style "command succeeded but did nothing" bug.
        assert data.get("total_methods", 0) > 0, (
            "test run returned total_methods=0 after writing a class with "
            "a FOR TESTING method — runner is likely skipping inactive "
            f"sources: {data}"
        )
        assert data["total_failed"] > 0 or not data["all_passed"], (
            f"Expected at least one failed test (1 != 2), got: {data}"
        )
