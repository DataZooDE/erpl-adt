"""Object CRUD tests — validate object read/create/delete via CLI."""

import pytest


@pytest.mark.object
class TestObject:

    def test_read_standard_class(self, cli):
        """Read a standard class (CL_ABAP_RANDOM)."""
        data = cli.run_ok("object", "read",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "CL_ABAP_RANDOM" in data.get("name", "").upper()

    def test_read_object_has_fields(self, cli):
        """Object structure has name, type, uri."""
        data = cli.run_ok("object", "read",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "name" in data
        assert "type" in data
        assert "uri" in data

    def test_read_object_has_includes(self, cli):
        """Object structure has includes list."""
        data = cli.run_ok("object", "read",
                          "/sap/bc/adt/oo/classes/cl_abap_random")
        assert "includes" in data
        assert isinstance(data["includes"], list)

    def test_read_nonexistent_fails(self, cli):
        """Read non-existent class returns non-zero exit code."""
        result = cli.run("object", "read",
                         "/sap/bc/adt/oo/classes/znonexistent_class_99999")
        assert result.returncode != 0

    def test_create_and_read_class(self, test_class, cli):
        """Created test class is readable via CLI."""
        data = cli.run_ok("object", "read", test_class["uri"])
        assert test_class["name"] in data.get("name", "").upper()

    def test_create_class_returns_uri(self, cli, test_class_name):
        """object create returns the new object's URI."""
        name = test_class_name
        data = cli.run_ok(
            "object", "create",
            "--type", "CLAS/OC",
            "--name", name,
            "--package", "$TMP",
            "--description", "Pytest object test",
        )
        assert "uri" in data

        # Cleanup: auto-lock delete (no explicit handle needed).
        uri = data["uri"]
        try:
            cli.run("object", "delete", uri)
        except Exception:
            pass
