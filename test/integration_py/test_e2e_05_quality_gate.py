"""E2E Scenario 5: Quality Gate and Transport Workflow.

Simulates a release readiness workflow — create transport, write clean code,
verify quality, prepare for shipment:
  transport create -> transport list -> create class -> test -> check -> transport release
"""

import json
import os
import re

import pytest


TRANSPORT_PATTERN = re.compile(r"^[A-Z0-9]{3}[A-Z]\d{6}$")

QUALITY_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC
  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.
  PUBLIC SECTION.
    METHODS get_status RETURNING VALUE(rv_result) TYPE string.
  PRIVATE SECTION.
    METHODS test_get_status FOR TESTING.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_status.
    rv_result = 'OK'.
  ENDMETHOD.
  METHOD test_get_status.
    cl_abap_unit_assert=>assert_equals( act = get_status( ) exp = 'OK' ).
  ENDMETHOD.
ENDCLASS.
"""


@pytest.mark.e2e
@pytest.mark.slow
class TestQualityGate:
    """Quality gate workflow: transport + tests + ATC checks + release."""

    @pytest.mark.order(1)
    def test_step1_create_transport(self, e2e_context):
        """Step 1: Create a transport request for the work."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("transport", "create",
                                 "--desc", "E2E quality gate test",
                                 "--package", "$TMP")
        assert "transport_number" in data
        assert TRANSPORT_PATTERN.match(data["transport_number"]), \
            f"Invalid transport number: {data['transport_number']}"
        ctx["transport"] = data["transport_number"]

    @pytest.mark.order(2)
    def test_step2_verify_transport_list(self, e2e_context):
        """Step 2: Verify transport list command works and transport format is valid."""
        ctx = e2e_context
        assert ctx.get("transport"), "Step 1 must run first"
        # Verify the created transport has a valid format
        assert TRANSPORT_PATTERN.match(ctx["transport"])
        # Verify transport list command succeeds (may be empty on trial systems)
        data = ctx["cli"].run_ok("transport", "list", "--user", "DEVELOPER")
        assert isinstance(data, list)
        # If the list has entries, verify structure
        if data:
            t = data[0]
            assert "number" in t
            assert "owner" in t

    @pytest.mark.order(3)
    def test_step3_create_class_with_source(self, e2e_context):
        """Step 3: Create class and write clean source with unit test."""
        ctx = e2e_context
        # Create class
        data = ctx["cli"].run_ok(
            "object", "create",
            "--type", "CLAS/OC",
            "--name", ctx["name"],
            "--package", "$TMP",
            "--description", "E2E quality gate class",
        )
        ctx["uri"] = data["uri"]
        # Construct source URI from object URI
        ctx["source_uri"] = ctx["uri"] + "/source/main"
        # Write source (auto-lock)
        source = QUALITY_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "quality.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file)

    @pytest.mark.order(4)
    def test_step4_run_tests(self, e2e_context):
        """Step 4: Verify unit tests pass."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("test", "run", ctx["uri"])
        assert data["all_passed"] is True, \
            f"Tests failed: {json.dumps(data, indent=2)}"

    @pytest.mark.order(5)
    def test_step5_run_atc_checks(self, e2e_context):
        """Step 5: Run ATC quality checks and capture findings."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("check", "run", ctx["uri"])
        assert "worklist_id" in data
        assert "error_count" in data
        assert "findings" in data
        ctx["check_results"] = data

    @pytest.mark.order(6)
    def test_step6_assert_quality_gate(self, e2e_context):
        """Step 6: Assert quality gate — tests passed AND check structure valid."""
        ctx = e2e_context
        # Re-run tests to confirm
        test_data = ctx["cli"].run_ok("test", "run", ctx["uri"])
        assert test_data["all_passed"] is True, "Quality gate: tests must pass"
        # Check results from step 5
        check = ctx.get("check_results", {})
        assert "worklist_id" in check, "Quality gate: ATC check must return valid structure"

    @pytest.mark.order(7)
    def test_step7_release_transport(self, e2e_context):
        """Step 7: Attempt transport release (accept exit 0 or 9 for trial systems)."""
        ctx = e2e_context
        assert ctx.get("transport"), "Step 1 must run first"
        result = ctx["cli"].run("transport", "release", ctx["transport"])
        # Exit 0 = released successfully, 9 = transport error (trial systems may reject)
        assert result.returncode in (0, 9), \
            f"Unexpected exit code {result.returncode}: {result.stderr}"
