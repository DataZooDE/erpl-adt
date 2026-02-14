"""Activation tests — validate activate command via CLI."""

import os
import tempfile

import pytest


@pytest.mark.activation
@pytest.mark.slow
class TestActivation:

    def test_activate_standard_class(self, cli):
        """activate on a standard class succeeds."""
        data = cli.run_ok("activate", "CL_ABAP_RANDOM")
        assert "activated" in data

    def test_activate_by_uri(self, cli):
        """activate by URI succeeds."""
        data = cli.run_ok("activate", "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "activated" in data

    def test_activate_nonexistent_object(self, cli):
        """activate with nonexistent object returns exit code 2."""
        result = cli.run("activate", "ZZZZ_NONEXISTENT_12345")
        assert result.returncode == 2

    def test_source_write_with_activate_flag(self, test_class, cli, session_file):
        """source write --activate writes and activates in one step."""
        uri = test_class["uri"]
        name = test_class["name"]

        source = (
            f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC\n"
            "  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.\n"
            "  PUBLIC SECTION.\n"
            "ENDCLASS.\n\n"
            f"CLASS {name} IMPLEMENTATION.\n"
            "ENDCLASS.\n"
        )
        with tempfile.NamedTemporaryFile(mode="w", suffix=".abap",
                                         delete=False) as f:
            f.write(source)
            src_file = f.name

        source_uri = uri + "/source/main"
        try:
            result = cli.run("source", "write", source_uri,
                             "--file", src_file, "--activate")
            assert result.returncode == 0, (
                f"source write --activate failed: {result.stderr}"
            )
        finally:
            os.unlink(src_file)
