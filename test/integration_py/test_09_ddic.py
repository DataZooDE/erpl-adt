"""DDIC (Data Dictionary) tests — validate package and table operations via CLI.

Strong assertions: instead of `if not data: pytest.skip(...)` (which hid
issue #9-style bugs where a command returns empty), this file seeds a known
object in $TMP at class setup and asserts that list/tree commands return it.

SFLIGHT is a standard SAP demo table present on every trial/cloud system —
the previous "skip if missing" was defensive but never triggered; we now
fail loudly so a real test-env break is visible.
"""

import json
import random

import pytest


# ---------------------------------------------------------------------------
# Class-scoped fixture: seed one ABAP class in $TMP so list/tree have a
# known artifact to find. Best-effort teardown.
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def seeded_tmp_class(cli):
    """Create a class in $TMP for the duration of the test class.

    Yields a dict {name, uri}. Deleted on teardown (best-effort).
    """
    name = f"ZTEST_DDIC_{random.randint(10000, 99999)}"
    data = cli.run_ok(
        "object", "create",
        "--type", "CLAS/OC",
        "--name", name,
        "--package", "$TMP",
        "--description", "ddic test seed class",
    )
    uri = data.get("uri", f"/sap/bc/adt/oo/classes/{name.lower()}")
    yield {"name": name, "uri": uri}
    cli.run("object", "delete", uri)


@pytest.mark.ddic
class TestPackageListing:
    """package list / package tree — must surface a seeded object."""

    def test_list_contains_seeded_class(self, cli, seeded_tmp_class):
        """package list returns the class we just created in $TMP."""
        data = cli.run_ok("package", "list", "$TMP")
        assert isinstance(data, list)
        assert len(data) > 0, "$TMP package list is empty after seeding"

        names = {e.get("object_name", "").upper() for e in data}
        assert seeded_tmp_class["name"] in names, (
            f"Seeded class {seeded_tmp_class['name']} missing from "
            f"$TMP package list (got {len(names)} entries)"
        )

    @pytest.mark.usefixtures("seeded_tmp_class")
    def test_list_entries_have_required_fields(self, cli):
        """Every package-list entry has object_type, object_name, object_uri."""
        data = cli.run_ok("package", "list", "$TMP")
        assert len(data) > 0
        for entry in data:
            for field in ("object_type", "object_name", "object_uri"):
                assert field in entry, f"Missing field {field!r} in {entry}"
            # Field must not just be present — must be populated.
            assert entry["object_name"], f"Empty object_name in {entry}"
            assert entry["object_type"], f"Empty object_type in {entry}"

    def test_tree_contains_seeded_class(self, cli, seeded_tmp_class):
        """package tree (recursive) also surfaces the seeded class."""
        data = cli.run_ok("package", "tree", "$TMP")
        assert isinstance(data, list)
        assert len(data) > 0, "$TMP package tree is empty after seeding"

        names = {e.get("object_name", "").upper() for e in data}
        assert seeded_tmp_class["name"] in names, (
            f"Seeded class {seeded_tmp_class['name']} missing from "
            f"$TMP package tree"
        )

    def test_tree_type_filter_excludes_other_types(self, cli, seeded_tmp_class):
        """package tree --type CLAS returns only CLAS entries, including seed."""
        data = cli.run_ok("package", "tree", "$TMP", "--type", "CLAS")
        assert isinstance(data, list)
        # The seeded class must be present (otherwise the filter dropped it).
        names = {e.get("object_name", "").upper() for e in data}
        assert seeded_tmp_class["name"] in names, (
            "Seeded class missing from --type CLAS filtered tree"
        )
        # And only CLAS should be present.
        for entry in data:
            assert "CLAS" in entry["object_type"], (
                f"Type filter leaked non-CLAS entry: {entry}"
            )

    @pytest.mark.usefixtures("seeded_tmp_class")
    def test_tree_entries_have_package_provenance(self, cli):
        """package tree entries include `package` provenance."""
        data = cli.run_ok("package", "tree", "$TMP")
        assert len(data) > 0
        assert "package" in data[0]

    def test_package_exists_returns_true_for_tmp(self, cli):
        """package exists is true for the standard $TMP package."""
        data = cli.run_ok("package", "exists", "$TMP")
        assert data.get("exists") is True

    def test_package_exists_returns_false_for_nonexistent(self, cli):
        """package exists is false (not error) for a bogus name."""
        data = cli.run_ok("package", "exists", "ZNONEXISTENT_PKG_99999")
        assert data.get("exists") is False

    def test_list_of_nonexistent_package_returns_not_found(self, cli):
        """package list of a non-existent package returns exit code 2.

        Previously the command silently returned [] for missing packages
        (the SAP nodestructure endpoint replies HTTP 200 with an empty body
        for both "package empty" and "package missing"). Without
        disambiguation, users could not tell whether a populated package's
        list was genuinely empty or whether they had typoed the name.
        Catches the orphaned-package failure mode the discovery sweep found.
        """
        result = cli.run("package", "list", "ZNONEXISTENT_PKG_99999")
        assert result.returncode == 2, (
            f"Expected exit 2 (not found), got {result.returncode}\n"
            f"stdout: {result.stdout[:300]}"
        )


@pytest.mark.ddic
class TestDdicTable:
    """ddic table — SFLIGHT is part of the SAP demo content; assume present.

    If your test environment lacks SFLIGHT the tests fail loudly — that is
    a test-environment defect, not a regression in this code.
    """

    def test_sflight_returns_metadata_and_fields(self, cli):
        """ddic table sflight returns name + populated fields array."""
        data = cli.run_ok("ddic", "table", "sflight")
        assert data.get("name", "").upper() == "SFLIGHT"
        fields = data.get("fields")
        assert isinstance(fields, list)
        assert len(fields) > 5, (
            f"SFLIGHT has only {len(fields)} fields — parser likely truncated"
        )

    def test_sflight_field_shape(self, cli):
        """Each SFLIGHT field exposes name, type, length, abap_type."""
        data = cli.run_ok("ddic", "table", "sflight")
        fields = data["fields"]
        for f in fields:
            for field in ("name", "type", "abap_type"):
                assert field in f, f"Field missing {field!r}: {f}"
            assert f["name"], f"Empty field name: {f}"
            assert f["abap_type"], f"Empty abap_type: {f}"
        # SFLIGHT is known to have a mandt/client key field.
        names = [f["name"].lower() for f in fields]
        assert "mandt" in names or "carrid" in names, (
            f"Expected mandt or carrid in SFLIGHT, got {names}"
        )

    def test_sflight_description_populated(self, cli):
        """SFLIGHT has a non-empty short text — was previously a weak check."""
        data = cli.run_ok("ddic", "table", "sflight")
        desc = data.get("description", "")
        assert len(desc) > 0, (
            "SFLIGHT short text empty — DDIC short-text parsing regression"
        )

    def test_nonexistent_table_returns_not_found(self, cli):
        """ddic table for a bogus name returns the not-found exit code (2)."""
        result = cli.run("ddic", "table", "znonexistent_table_99999")
        assert result.returncode == 2, (
            f"Expected exit code 2 (not found), got {result.returncode}\n"
            f"stderr: {result.stderr}"
        )

    def test_table_human_readable_renders_table(self, cli):
        """ddic table without --json renders a table with SFLIGHT's name."""
        result = cli.run_no_json("ddic", "table", "sflight")
        assert result.returncode == 0
        assert "SFLIGHT" in result.stdout.upper()
        assert not result.stdout.strip().startswith("{"), \
            "Non-JSON mode should not emit JSON"

    def test_table_default_resolves_types(self, cli):
        """By default, --resolve-types fetches data elements → Length column."""
        result = cli.run_no_json("ddic", "table", "sflight")
        assert result.returncode == 0
        header_line = next(
            (l for l in result.stdout.splitlines() if "Field" in l or "Type" in l),
            "",
        )
        assert "Length" in header_line, \
            "Length column missing when resolve_types=true (default)"
        assert "Description" in header_line, \
            "Description column missing when resolve_types=true (default)"

    def test_table_no_resolve_types_hides_columns(self, cli):
        """--no-resolve-types omits Length and Description for DDL tables."""
        result = cli.run_no_json(
            "ddic", "table", "sflight", "--no-resolve-types",
        )
        assert result.returncode == 0
        header_line = next(
            (l for l in result.stdout.splitlines() if "Field" in l or "Type" in l),
            "",
        )
        # On DDL-format tables these columns vanish; on classic DD02L tables
        # they may stay because the metadata XML carries them directly.
        # We assert at least one of the two is gone — the flag must have
        # *some* observable effect to count as functioning.
        hidden = ("Length" not in header_line) or ("Description" not in header_line)
        assert hidden, (
            "--no-resolve-types had no observable effect on the header:\n"
            f"{header_line}"
        )


@pytest.mark.ddic
class TestDdicCustomTable:
    """ZNW_ORDERS-specific regressions — only run if that table is deployed.

    We keep the skip here because ZNW_ORDERS is part of an optional test
    deployment, not stock SAP demo content. The skip message is precise
    so a CI failure cannot be confused with the table genuinely missing.
    """

    def test_abap_builtin_types_not_truncated(self, cli):
        """abap.int4, abap.char(N), abap.dec(M,N) must not collapse to 'abap'.

        Regression guard: before the fix, all abap.* types were parsed as
        the literal string 'abap' because the parser dropped the suffix.
        """
        result = cli.run("ddic", "table", "znw_orders")
        if result.returncode != 0:
            pytest.skip(
                "ZNW_ORDERS not deployed on this system — "
                "deploy from test/integration_py/sap_artifacts/ to enable"
            )

        data = json.loads(result.stdout)
        fields = data.get("fields", [])
        assert fields, "ZNW_ORDERS returned no fields"
        fields_by_name = {f["name"].lower(): f for f in fields}

        if "order_id" in fields_by_name:
            t = fields_by_name["order_id"]["type"]
            assert t != "abap", \
                f"order_id type truncated to 'abap'; expected 'abap.int4'"
            assert "int4" in t.lower(), f"order_id type wrong: {t!r}"

        if "cust_id" in fields_by_name:
            t = fields_by_name["cust_id"]["type"]
            assert t != "abap", \
                f"cust_id type truncated to 'abap'; expected 'abap.char(5)'"
            assert "char" in t.lower(), f"cust_id type wrong: {t!r}"
            assert "5" in t, f"cust_id length missing: {t!r}"

        if "freight" in fields_by_name:
            t = fields_by_name["freight"]["type"]
            assert t != "abap", \
                f"freight type truncated to 'abap'; expected 'abap.dec(13,4)'"
            assert "dec" in t.lower(), f"freight type wrong: {t!r}"

        non_abap = [f for f in fields if f["type"] != "abap"]
        assert non_abap, "All field types collapsed to 'abap' — fix not working"
