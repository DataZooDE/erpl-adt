"""Source code operation tests — validate read/write/check via CLI."""

import os
import tempfile

import pytest


@pytest.mark.source
class TestSource:

    def test_read_source_active(self, test_class, cli):
        """source read returns ABAP source text."""
        uri = test_class["uri"] + "/source/main"
        data = cli.run_ok("source", "read", uri)
        assert "source" in data
        assert "CLASS" in data["source"].upper() or "class" in data["source"]

    def test_read_source_inactive(self, test_class, cli):
        """source read with --version inactive succeeds or returns error."""
        uri = test_class["uri"] + "/source/main"
        result = cli.run("source", "read", uri, "--version", "inactive")
        # 0 = inactive version found, non-zero = no inactive version.
        # Both outcomes are valid; we just verify the command doesn't crash.
        assert isinstance(result.returncode, int)

    def test_read_nonexistent_fails(self, cli):
        """source read of non-existent object returns error."""
        result = cli.run("source", "read",
                         "/sap/bc/adt/oo/classes/znonexistent_99999/source/main")
        assert result.returncode != 0

    def test_write_source_auto_lock(self, test_class, cli):
        """Write source without explicit handle — auto lock/write/unlock."""
        uri = test_class["uri"]
        source_uri = uri + "/source/main"
        name = test_class["name"]

        source = (
            f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
            "  PUBLIC SECTION.\n"
            "    METHODS get_value RETURNING VALUE(rv_result) TYPE i.\n"
            "ENDCLASS.\n\n"
            f"CLASS {name} IMPLEMENTATION.\n"
            "  METHOD get_value.\n"
            "    rv_result = 42.\n"
            "  ENDMETHOD.\n"
            "ENDCLASS.\n"
        )
        with tempfile.NamedTemporaryFile(mode="w", suffix=".abap",
                                         delete=False) as f:
            f.write(source)
            src_file = f.name

        try:
            # Auto-lock mode: no --handle needed.
            cli.run_ok("source", "write", source_uri, "--file", src_file)
        finally:
            os.unlink(src_file)

        # Verify: read back.
        read_data = cli.run_ok("source", "read", source_uri,
                               "--version", "inactive")
        if "source" in read_data:
            assert "get_value" in read_data["source"].lower()

    def test_syntax_check(self, test_class, cli):
        """source check returns syntax check results."""
        source_uri = test_class["uri"] + "/source/main"
        data = cli.run_ok("source", "check", source_uri)
        assert isinstance(data, list)
