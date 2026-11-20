"""Outcome exit codes — an agent's edit loop gated on `$?` (issue #75).

Agents and scripts drive erpl-adt with `--json` and branch on the exit code:

    erpl-adt --json source write ZCL_X --file x.abap --activate || fix-and-retry
    erpl-adt --json test ZCL_X  || report-failures
    erpl-adt --json check ZCL_X || block-release

So a failed activation, a failing unit test and an ATC error must end the
process non-zero (5, 7 and 8) — in JSON mode as in text mode — while the JSON
on stdout still says what failed. Each test below is one such loop on a
disposable $TMP class: break it, see the gate close, fix it, see it open.
"""

import json
import os

import pytest


# The system check variant (ATC customizing `systemCheckVariant`) is the one
# whose findings carry priority 1 for code that breaks the ABAP language
# version; the plain DEFAULT variant reports the same code at priority 2.
ATC_VARIANT = os.getenv("SAP_ATC_VARIANT", "ZABAP_CLOUD_DEVELOPMENT")

EXIT_ACTIVATION = 5
EXIT_TEST_FAILURE = 7
EXIT_CHECK_ERROR = 8


def clean_source(name):
    return (
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


def broken_source(name):
    """Compiles nowhere: the variable is never declared."""
    return clean_source(name).replace("rv_result = 42.",
                                      "rv_result = lv_undeclared_x.")


def tested_source(name, expected):
    """A class with one unit test asserting get_value( ) = `expected`."""
    return (
        f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC\n"
        "  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.\n"
        "  PUBLIC SECTION.\n"
        "    METHODS get_value RETURNING VALUE(rv_result) TYPE i.\n"
        "  PRIVATE SECTION.\n"
        "    METHODS test_get_value FOR TESTING.\n"
        "ENDCLASS.\n\n"
        f"CLASS {name} IMPLEMENTATION.\n"
        "  METHOD get_value.\n"
        "    rv_result = 42.\n"
        "  ENDMETHOD.\n"
        "  METHOD test_get_value.\n"
        "    cl_abap_unit_assert=>assert_equals(\n"
        f"      act = get_value( ) exp = {expected} ).\n"
        "  ENDMETHOD.\n"
        "ENDCLASS.\n"
    )


def atc_error_source(name):
    """Active, compiles, but BREAK-POINT breaks the cloud language scope."""
    return clean_source(name).replace("rv_result = 42.",
                                      "BREAK-POINT.\n    rv_result = 42.")


def write(cli, uri, source, tmp_path, *flags):
    path = tmp_path / "src.abap"
    path.write_text(source)
    return cli.run("source", "write", uri + "/source/main",
                   "--file", str(path), *flags)


def single_json(result):
    """stdout must hold exactly one JSON document — what `jq` and agents parse."""
    return json.loads(result.stdout)


@pytest.mark.activation
@pytest.mark.slow
class TestOutcomeExitCodes:

    def test_failed_activation_exits_5_in_json_mode(self, cli, test_class,
                                                    tmp_path):
        uri, name = test_class["uri"], test_class["name"]
        assert write(cli, uri, broken_source(name), tmp_path).returncode == 0

        result = cli.run("activate", uri)
        data = single_json(result)
        assert data["failed"] > 0, data
        assert data["error_messages"], data
        assert result.returncode == EXIT_ACTIVATION, (
            f"activation failed ({data['failed']} error(s)) but exit was "
            f"{result.returncode}")

        assert write(cli, uri, clean_source(name), tmp_path).returncode == 0
        fixed = cli.run("activate", uri)
        assert fixed.returncode == 0, fixed.stderr
        assert single_json(fixed)["failed"] == 0

    def test_source_write_activate_json_is_one_object(self, cli, test_class,
                                                      tmp_path):
        uri, name = test_class["uri"], test_class["name"]

        result = write(cli, uri, broken_source(name), tmp_path, "--activate")
        data = single_json(result)
        assert data["written"] == uri + "/source/main", data
        assert data["activation"]["failed"] > 0, data
        assert data["activation"]["error_messages"], data
        assert result.returncode == EXIT_ACTIVATION, result.stderr

        result = write(cli, uri, clean_source(name), tmp_path, "--activate")
        assert result.returncode == 0, result.stderr
        data = single_json(result)
        assert data["activation"]["failed"] == 0, data

    def test_source_check_sees_the_inactive_version(self, cli, test_class,
                                                    tmp_path):
        """Check what was just written, before activating it."""
        uri, name = test_class["uri"], test_class["name"]
        assert write(cli, uri, clean_source(name), tmp_path,
                     "--activate").returncode == 0
        # Written, not activated: the active version is still clean.
        assert write(cli, uri, broken_source(name), tmp_path).returncode == 0

        result = cli.run("source", "check", uri + "/source/main",
                         "--version", "inactive")
        messages = single_json(result)
        errors = [m for m in messages if m["type"] == "E"]
        assert errors, f"inactive check missed the undeclared variable: {messages}"
        assert any("LV_UNDECLARED_X" in m["text"].upper() for m in errors), errors
        assert result.returncode == EXIT_CHECK_ERROR, result.stderr

        active = cli.run("source", "check", uri + "/source/main",
                         "--version", "active")
        assert active.returncode == 0, active.stderr
        assert not [m for m in single_json(active) if m["type"] == "E"]

    def test_failing_unit_test_exits_7(self, cli, test_class, tmp_path):
        uri, name = test_class["uri"], test_class["name"]
        assert write(cli, uri, tested_source(name, 41), tmp_path,
                     "--activate").returncode == 0

        result = cli.run("test", uri)
        data = single_json(result)
        assert data["total_methods"] > 0, data
        assert data["all_passed"] is False, data
        assert result.returncode == EXIT_TEST_FAILURE, (
            f"{data['total_failed']} test(s) failed but exit was "
            f"{result.returncode}")

        assert write(cli, uri, tested_source(name, 42), tmp_path,
                     "--activate").returncode == 0
        fixed = cli.run("test", uri)
        assert fixed.returncode == 0, fixed.stderr
        assert single_json(fixed)["all_passed"] is True

    def test_atc_error_exits_8(self, cli, test_class, tmp_path):
        uri, name = test_class["uri"], test_class["name"]
        assert write(cli, uri, atc_error_source(name), tmp_path,
                     "--activate").returncode == 0

        result = cli.run("check", uri, "--variant", ATC_VARIANT)
        data = single_json(result)
        assert data["error_count"] > 0, (
            f"BREAK-POINT under {ATC_VARIANT} must be a priority-1 finding: "
            f"{data}")
        breakpoint_finding = next(
            f for f in data["findings"] if f["priority"] == 1)
        assert breakpoint_finding["uri"].startswith(uri + "/source/main#start="), \
            breakpoint_finding
        assert breakpoint_finding["message"], breakpoint_finding
        assert result.returncode == EXIT_CHECK_ERROR, result.stderr

        assert write(cli, uri, clean_source(name), tmp_path,
                     "--activate").returncode == 0
        clean = cli.run("check", uri, "--variant", ATC_VARIANT)
        assert clean.returncode == 0, clean.stderr
        assert single_json(clean)["error_count"] == 0
