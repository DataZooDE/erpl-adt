"""E2E Scenario 4: TDD Red-Green Cycle.

Simulates test-driven development — write a failing test first, verify the red
source is written, fix the code, verify the green source is written, run checks.

Note: On ABAP Cloud trial systems, `test run` only finds methods in the ACTIVE
version. Since `source write` creates inactive code and there is no standalone
`activate` command, test assertions guard on total_methods > 0 to handle both
activated and non-activated systems.
"""

import json
import os

import pytest


RED_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC
  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.
  PUBLIC SECTION.
    METHODS get_value RETURNING VALUE(rv_result) TYPE i.
  PRIVATE SECTION.
    METHODS test_get_value FOR TESTING.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_value.
    rv_result = 0.
  ENDMETHOD.
  METHOD test_get_value.
    cl_abap_unit_assert=>assert_equals( act = get_value( ) exp = 100 msg = 'get_value should return 100' ).
  ENDMETHOD.
ENDCLASS.
"""

GREEN_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC
  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.
  PUBLIC SECTION.
    METHODS get_value RETURNING VALUE(rv_result) TYPE i.
  PRIVATE SECTION.
    METHODS test_get_value FOR TESTING.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_value.
    rv_result = 100.
  ENDMETHOD.
  METHOD test_get_value.
    cl_abap_unit_assert=>assert_equals( act = get_value( ) exp = 100 msg = 'get_value should return 100' ).
  ENDMETHOD.
ENDCLASS.
"""


@pytest.mark.e2e
@pytest.mark.slow
class TestRedGreenCycle:
    """TDD cycle: red (failing test) -> green (passing test) -> quality check."""

    @pytest.mark.order(1)
    def test_step1_create_class(self, e2e_context):
        """Setup: Create a new class for the TDD cycle."""
        ctx = e2e_context
        data = ctx["cli"].run_ok(
            "object", "create",
            "--type", "CLAS/OC",
            "--name", ctx["name"],
            "--package", "$TMP",
            "--description", "E2E TDD red-green test class",
        )
        ctx["uri"] = data["uri"]
        ctx["source_uri"] = ctx["uri"] + "/source/main"

    @pytest.mark.order(2)
    def test_step2_write_red_source(self, e2e_context):
        """Step 2: Write 'red' source — implementation returns 0, test expects 100."""
        ctx = e2e_context
        source = RED_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "red.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file)

    @pytest.mark.order(3)
    def test_step3_verify_red_source(self, e2e_context):
        """Step 3: Verify the red source is written — get_value returns 0."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                 "--version", "inactive")
        assert "source" in data
        source = data["source"]
        assert "get_value" in source.lower()
        assert "rv_result = 0" in source, \
            "Red source should have rv_result = 0"
        # Run tests — on systems with activation, tests should fail;
        # on systems without, test runner may find 0 methods (inactive code).
        test_data = ctx["cli"].run_ok("test", "run", ctx["uri"])
        if test_data.get("total_methods", 0) > 0:
            assert test_data["all_passed"] is False, \
                f"Expected test failure: {json.dumps(test_data, indent=2)}"
            assert test_data["total_failed"] > 0
        ctx["red_test_data"] = test_data

    @pytest.mark.order(4)
    def test_step4_inspect_red_results(self, e2e_context):
        """Step 4: Inspect test results or source content from red phase."""
        ctx = e2e_context
        test_data = ctx.get("red_test_data", {})
        if test_data.get("total_methods", 0) > 0:
            # Tests ran — verify alerts contain meaningful info
            has_alert = False
            for cls in test_data.get("classes", []):
                for method in cls.get("methods", []):
                    for alert in method.get("alerts", []):
                        has_alert = True
                        assert any(k in alert for k in
                                   ("kind", "severity", "title", "details")), \
                            f"Alert missing expected fields: {alert}"
            assert has_alert, "Expected at least one alert in failing test"
        else:
            # No methods found (inactive code) — verify source content instead
            data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                     "--version", "inactive")
            assert "rv_result = 0" in data["source"], \
                "Red source should still show rv_result = 0"

    @pytest.mark.order(5)
    def test_step5_write_green_source(self, e2e_context):
        """Step 5: Write 'green' source — fix get_value() to return 100."""
        ctx = e2e_context
        source = GREEN_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "green.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file)

    @pytest.mark.order(6)
    def test_step6_verify_green_source(self, e2e_context):
        """Step 6: Verify green source — get_value now returns 100."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                 "--version", "inactive")
        assert "source" in data
        assert "rv_result = 100" in data["source"], \
            "Green source should have rv_result = 100"
        # Run tests — should pass (or vacuously pass with 0 methods)
        test_data = ctx["cli"].run_ok("test", "run", ctx["uri"])
        assert test_data["all_passed"] is True, \
            f"Expected tests to pass: {json.dumps(test_data, indent=2)}"

    @pytest.mark.order(7)
    def test_step7_quality_checks(self, e2e_context):
        """Step 7: Syntax check + ATC quality check on green code."""
        ctx = e2e_context
        # Syntax check
        syntax = ctx["cli"].run_ok("source", "check", ctx["source_uri"])
        assert isinstance(syntax, list)
        # ATC check
        atc = ctx["cli"].run_ok("check", "run", ctx["uri"])
        assert "worklist_id" in atc
        assert "error_count" in atc
