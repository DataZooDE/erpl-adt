"""Integration tests for `erpl-adt screen` — disposable classic Dynpro CRUD.

ADT has no REST collection for classic Dynpro screens (confirmed against this
system's own discovery document and a 404 probe on
GET /sap/bc/adt/programs/programs/{prog}/screens). `erpl-adt screen
create/read/delete` drives SAP's own repository function modules
(RPY_DYNPRO_INSERT / RPY_DYNPRO_READ / RS_SCRP_DELETE) through a disposable
IF_OO_ADT_CLASSRUN helper class that is written, activated, run, and deleted
again on every invocation.

This is the executable documentation for issue #66: a disposable classic
screen with two distinct editable input fields (HEADERNAME, VALUE) that
classic selection-screen PARAMETERS (8-char name limit) cannot produce.
"""

import pytest


@pytest.mark.screen
class TestScreen:

    def test_create_read_delete_round_trip(self, cli, test_program):
        """Full round trip: create -> read shows both fields -> delete -> read fails."""
        program = test_program

        # Create a screen with two separate named input fields — the exact
        # scenario classic selection-screen PARAMETERS cannot produce (their
        # names are limited to 8 characters).
        create_data = cli.run_ok(
            "screen", "create",
            "--program", program,
            "--screen", "9001",
            "--package", "$TMP",
            "--fields", "HEADERNAME:Header Name:5:3:30,VALUE:Value:7:3:30",
        )
        assert create_data["program"] == program
        assert create_data["screen"] == "9001"

        # Read back the *real* deployed layout — not a guess — and verify
        # both fields exist as distinct, separately named input controls.
        read_data = cli.run_ok(
            "screen", "read",
            "--program", program,
            "--screen", "9001",
            "--package", "$TMP",
        )
        assert read_data["program"] == program
        assert read_data["screen"] == "9001"
        field_names = {f["name"] for f in read_data["fields"]}
        assert "HEADERNAME" in field_names
        assert "VALUE" in field_names

        header_field = next(f for f in read_data["fields"] if f["name"] == "HEADERNAME")
        value_field = next(f for f in read_data["fields"] if f["name"] == "VALUE")
        assert header_field["input"] is True
        assert value_field["input"] is True
        # The two fields are genuinely distinct controls at different screen
        # positions, not the same field read twice.
        assert header_field["line"] != value_field["line"]

        # Delete and verify it is actually gone (not just a claimed success).
        cli.run_ok(
            "screen", "delete",
            "--program", program,
            "--screen", "9001",
            "--package", "$TMP",
        )
        result = cli.run(
            "screen", "read",
            "--program", program,
            "--screen", "9001",
            "--package", "$TMP",
        )
        assert result.returncode != 0

    def test_create_missing_fields_is_validation_error(self, cli, test_program):
        """Missing --fields is a usage error, not a silent no-op."""
        result = cli.run(
            "screen", "create",
            "--program", test_program,
            "--screen", "9002",
            "--package", "$TMP",
        )
        assert result.returncode != 0

    def test_read_nonexistent_screen_is_not_found(self, cli, test_program):
        """Reading a screen that was never created is a clear NotFound, not empty success."""
        result = cli.run(
            "screen", "read",
            "--program", test_program,
            "--screen", "9099",
            "--package", "$TMP",
        )
        assert result.returncode != 0
