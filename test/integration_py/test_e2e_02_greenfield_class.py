"""E2E Scenario 2: Greenfield Class Creation (full lifecycle).

Simulates an agent creating a brand-new utility class from scratch:
  create -> read -> write source -> read inactive -> syntax check -> test run -> delete
"""

import json
import os

import pytest


ABAP_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC
  FOR TESTING RISK LEVEL HARMLESS DURATION SHORT.
  PUBLIC SECTION.
    METHODS get_answer RETURNING VALUE(rv_result) TYPE i.
  PRIVATE SECTION.
    METHODS test_get_answer FOR TESTING.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_answer.
    rv_result = 42.
  ENDMETHOD.
  METHOD test_get_answer.
    cl_abap_unit_assert=>assert_equals( act = get_answer( ) exp = 42 ).
  ENDMETHOD.
ENDCLASS.
"""


@pytest.mark.e2e
@pytest.mark.slow
class TestGreenfieldClass:
    """Full lifecycle: create class, write source, test, delete."""

    @pytest.mark.order(1)
    def test_step1_create_class(self, e2e_context):
        """Step 1: Create a new ABAP class in $TMP."""
        ctx = e2e_context
        data = ctx["cli"].run_ok(
            "object", "create",
            "--type", "CLAS/OC",
            "--name", ctx["name"],
            "--package", "$TMP",
            "--description", "E2E greenfield test class",
        )
        assert "uri" in data
        ctx["uri"] = data["uri"]

    @pytest.mark.order(2)
    def test_step2_read_object(self, e2e_context):
        """Step 2: Read the created object and verify structure."""
        ctx = e2e_context
        assert ctx["uri"] is not None, "Step 1 must run first"
        data = ctx["cli"].run_ok("object", "read", ctx["uri"])
        assert ctx["name"] in data.get("name", "").upper()
        assert "type" in data
        # Construct source URI from object URI
        ctx["source_uri"] = ctx["uri"] + "/source/main"

    @pytest.mark.order(3)
    def test_step3_write_source(self, e2e_context):
        """Step 3: Write real ABAP source with method + unit test (auto-lock)."""
        ctx = e2e_context
        assert ctx["source_uri"] is not None, "Step 2 must run first"
        source = ABAP_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "greenfield.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file)

    @pytest.mark.order(4)
    def test_step4_read_inactive(self, e2e_context):
        """Step 4: Read inactive version and verify write took effect."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                 "--version", "inactive")
        assert "source" in data
        assert "get_answer" in data["source"].lower()

    @pytest.mark.order(5)
    def test_step5_syntax_check(self, e2e_context):
        """Step 5: Syntax check passes (empty or no errors)."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "check", ctx["source_uri"])
        assert isinstance(data, list)

    @pytest.mark.order(6)
    def test_step6_run_tests(self, e2e_context):
        """Step 6: Unit tests pass (get_answer returns 42)."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("test", "run", ctx["uri"])
        assert "all_passed" in data
        assert data["all_passed"] is True, \
            f"Tests failed: {json.dumps(data, indent=2)}"

    @pytest.mark.order(7)
    def test_step7_delete_class(self, e2e_context):
        """Step 7: Delete the class (auto-lock)."""
        ctx = e2e_context
        ctx["cli"].run_ok("object", "delete", ctx["uri"])
        # Mark as deleted so fixture doesn't try cleanup again
        ctx["uri"] = None

    @pytest.mark.order(8)
    def test_step8_verify_deleted(self, e2e_context):
        """Step 8: Verify object read fails after deletion."""
        ctx = e2e_context
        # URI was cleared in step 7 — reconstruct it
        name_lower = ctx["name"].lower()
        uri = f"/sap/bc/adt/oo/classes/{name_lower}"
        result = ctx["cli"].run("object", "read", uri)
        assert result.returncode != 0, "Object should not exist after deletion"
