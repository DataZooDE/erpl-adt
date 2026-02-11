"""E2E Scenario 3: Edit Existing Code with Explicit Locking.

Simulates the most common daily task — modify an existing class, add a new method.
Uses explicit lock/session-file workflow (the reliable agent pattern):
  read -> lock -> write -> read inactive -> syntax check -> unlock
"""

import os

import pytest


INITIAL_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    METHODS get_name RETURNING VALUE(rv_result) TYPE string.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_name.
    rv_result = 'Hello'.
  ENDMETHOD.
ENDCLASS.
"""

MODIFIED_SOURCE = """\
CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    METHODS get_name RETURNING VALUE(rv_result) TYPE string.
    METHODS get_greeting RETURNING VALUE(rv_result) TYPE string.
ENDCLASS.

CLASS {name} IMPLEMENTATION.
  METHOD get_name.
    rv_result = 'Hello'.
  ENDMETHOD.
  METHOD get_greeting.
    rv_result = |{{ get_name( ) }}, World!|.
  ENDMETHOD.
ENDCLASS.
"""


@pytest.mark.e2e
class TestEditExisting:
    """Edit an existing class with explicit lock/handle/session-file threading."""

    @pytest.mark.order(1)
    def test_step1_setup_class(self, e2e_context):
        """Setup: Create class and write initial source with get_name() method."""
        ctx = e2e_context
        # Create class
        data = ctx["cli"].run_ok(
            "object", "create",
            "--type", "CLAS/OC",
            "--name", ctx["name"],
            "--package", "$TMP",
            "--description", "E2E edit test class",
        )
        ctx["uri"] = data["uri"]
        # Construct source URI from object URI
        ctx["source_uri"] = ctx["uri"] + "/source/main"
        # Write initial source (auto-lock)
        source = INITIAL_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "initial.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file)

    @pytest.mark.order(2)
    def test_step2_read_current_source(self, e2e_context):
        """Step 2: Read current source and verify it contains get_name."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                 "--version", "inactive")
        assert "source" in data
        assert "get_name" in data["source"].lower()

    @pytest.mark.order(3)
    def test_step3_lock_object(self, e2e_context):
        """Step 3: Explicitly lock the object, capture handle."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("object", "lock", ctx["uri"],
                                 session_file=ctx["session_file"])
        assert "handle" in data
        assert data["handle"], "Lock handle is empty"
        ctx["handle"] = data["handle"]

    @pytest.mark.order(4)
    def test_step4_write_modified_source(self, e2e_context):
        """Step 4: Write modified source adding get_greeting() method."""
        ctx = e2e_context
        assert ctx.get("handle"), "Step 3 must run first (need lock handle)"
        source = MODIFIED_SOURCE.format(name=ctx["name"])
        src_file = os.path.join(str(ctx["tmp"]), "modified.abap")
        with open(src_file, "w") as f:
            f.write(source)
        ctx["cli"].run_ok("source", "write", ctx["source_uri"],
                          "--file", src_file,
                          "--handle", ctx["handle"],
                          session_file=ctx["session_file"])

    @pytest.mark.order(5)
    def test_step5_verify_modification(self, e2e_context):
        """Step 5: Read inactive version and verify get_greeting is present."""
        ctx = e2e_context
        data = ctx["cli"].run_ok("source", "read", ctx["source_uri"],
                                 "--version", "inactive")
        assert "source" in data
        assert "get_greeting" in data["source"].lower(), \
            "Modified source should contain get_greeting method"
        assert "get_name" in data["source"].lower(), \
            "Modified source should still contain get_name method"

    @pytest.mark.order(6)
    def test_step6_syntax_check_and_unlock(self, e2e_context):
        """Step 6: Syntax check passes, then release the lock."""
        ctx = e2e_context
        # Syntax check
        data = ctx["cli"].run_ok("source", "check", ctx["source_uri"])
        assert isinstance(data, list)
        # Unlock
        ctx["cli"].run_ok("object", "unlock", ctx["uri"],
                          "--handle", ctx["handle"],
                          session_file=ctx["session_file"])
        ctx["handle"] = None  # Mark as unlocked
