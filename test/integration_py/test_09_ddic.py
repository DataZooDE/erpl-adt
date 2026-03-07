"""DDIC (Data Dictionary) tests — validate package and table operations via CLI."""

import pytest


@pytest.mark.ddic
class TestDdic:

    def test_list_package_contents(self, cli):
        """package list returns results for $TMP."""
        data = cli.run_ok("package", "list", "$TMP")
        assert isinstance(data, list)

    def test_package_entry_has_fields(self, cli):
        """Package entries have object_type, object_name."""
        data = cli.run_ok("package", "list", "$TMP")
        if not data:
            pytest.skip("$TMP is empty on this system")
        entry = data[0]
        assert "object_type" in entry
        assert "object_name" in entry

    def test_package_exists(self, cli):
        """package exists returns true for $TMP."""
        data = cli.run_ok("package", "exists", "$TMP")
        assert data.get("exists") is True

    def test_package_not_exists(self, cli):
        """package exists returns false for non-existent package."""
        data = cli.run_ok("package", "exists", "ZNONEXISTENT_PKG_99999")
        assert data.get("exists") is False

    def test_get_table_definition(self, cli):
        """ddic table returns table metadata (if SFLIGHT exists)."""
        result = cli.run("ddic", "table", "sflight")
        if result.returncode != 0:
            pytest.skip("SFLIGHT table not found on this system")
        import json
        data = json.loads(result.stdout)
        assert "name" in data
        assert "fields" in data

    def test_table_has_metadata(self, cli):
        """SFLIGHT table metadata includes name and description."""
        result = cli.run("ddic", "table", "sflight")
        if result.returncode != 0:
            pytest.skip("SFLIGHT table not found on this system")
        import json
        data = json.loads(result.stdout)
        assert data["name"].upper() == "SFLIGHT"
        assert len(data.get("description", "")) > 0, "Expected non-empty description"

    def test_nonexistent_table_fails(self, cli):
        """ddic table for non-existent table returns error."""
        result = cli.run("ddic", "table", "znonexistent_table_99999")
        assert result.returncode != 0

    def test_table_human_readable(self, cli):
        """ddic table without --json produces human-readable output."""
        result = cli.run_no_json("ddic", "table", "sflight")
        if result.returncode != 0:
            pytest.skip("SFLIGHT table not found on this system")
        assert "SFLIGHT" in result.stdout.upper()
        assert not result.stdout.strip().startswith("{"), "Output should not be JSON"

    def test_package_tree(self, cli):
        """package tree recursively lists objects from $TMP."""
        data = cli.run_ok("package", "tree", "$TMP")
        assert isinstance(data, list)

    def test_package_tree_with_type_filter(self, cli):
        """package tree with --type filters object types."""
        data = cli.run_ok("package", "tree", "$TMP", "--type", "CLAS")
        assert isinstance(data, list)
        for entry in data:
            assert "CLAS" in entry["object_type"], \
                f"Expected CLAS type, got {entry['object_type']}"

    def test_package_tree_entries_have_package(self, cli):
        """package tree entries include package provenance."""
        data = cli.run_ok("package", "tree", "$TMP")
        if not data:
            pytest.skip("$TMP is empty on this system")
        assert "package" in data[0]

    def test_table_abap_builtin_types_not_truncated(self, cli):
        """Fields with abap.* built-in types are not truncated to 'abap'.

        ZNW_ORDERS uses abap.int4, abap.char(N), abap.dec(M,N) etc.
        Before the fix, all these were parsed as type "abap".
        """
        import json

        result = cli.run("ddic", "table", "znw_orders")
        if result.returncode != 0:
            pytest.skip("ZNW_ORDERS not deployed on this system")

        data = json.loads(result.stdout)
        assert "fields" in data, "Expected 'fields' in table definition"

        fields_by_name = {f["name"].lower(): f for f in data["fields"]}

        # Key field uses abap.int4 (no params) — must NOT be just "abap"
        if "order_id" in fields_by_name:
            t = fields_by_name["order_id"]["type"]
            assert t != "abap", f"order_id type truncated: got '{t}', expected 'abap.int4'"
            assert "int4" in t.lower(), f"order_id type wrong: '{t}'"

        # Char field with length param — must NOT be just "abap"
        if "cust_id" in fields_by_name:
            t = fields_by_name["cust_id"]["type"]
            assert t != "abap", f"cust_id type truncated: got '{t}', expected 'abap.char(5)'"
            assert "char" in t.lower(), f"cust_id type wrong: '{t}'"
            assert "5" in t, f"cust_id length missing: '{t}'"

        # Decimal with precision and scale — must NOT be just "abap"
        if "freight" in fields_by_name:
            t = fields_by_name["freight"]["type"]
            assert t != "abap", f"freight type truncated: got '{t}', expected 'abap.dec(13,4)'"
            assert "dec" in t.lower(), f"freight type wrong: '{t}'"

        # At least one non-truncated type must be present
        non_abap = [f for f in data["fields"] if f["type"] != "abap"]
        assert len(non_abap) > 0, "All field types truncated to 'abap' — fix not working"
