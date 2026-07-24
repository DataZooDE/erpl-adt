"""catalog build — RFC function-module interface extraction against a live SAP system.

Function modules (FUGR/FF) previously contributed an entity with no fields
and no edges: their IMPORTING/EXPORTING/TABLES/CHANGING parameters, and the
DDIC structures those parameters reference, were never read. This left
catalog-wide lineage/where-used blind to RFC-shaped dependencies. These
tests hit the live trial system directly — no mocks.
"""

import pytest


@pytest.mark.catalog
class TestCatalogBuildRfcLineage:
    """catalog build extracts function-module parameters and 'uses' edges."""

    def test_function_modules_get_fields_and_uses_edges(self, cli):
        data = cli.run_ok(
            "catalog", "build",
            "--package", "SCTS_CAT",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        entities = data["entities"]
        function_modules = [e for e in entities if e["object_type"] == "FUGR/FF"]
        assert function_modules, "expected at least one FUGR/FF entity in SCTS_CAT"

        fm_ids = {fm["id"] for fm in function_modules}
        fm_fields = [f for f in data["fields"] if f["entity_id"] in fm_ids]
        assert fm_fields, "expected at least one parameter field on a function module"
        assert any(f["role"] in ("importing", "exporting", "tables", "changing")
                    for f in fm_fields), \
            f"field roles should be RFC parameter kinds, got: {[f['role'] for f in fm_fields[:5]]}"

        uses_edges = [e for e in data["edges"]
                      if e["kind"] == "uses" and e["from_id"] in fm_ids]
        assert uses_edges, "expected at least one 'uses' edge from a function module"

        entity_ids = {e["id"] for e in entities}
        for edge in uses_edges:
            assert edge["to_id"] in entity_ids, \
                "uses edge target must have a corresponding entity (real or stub)"

    def test_unresolved_parameter_types_become_stub_entities(self, cli):
        data = cli.run_ok(
            "catalog", "build",
            "--package", "SCTS_CAT",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        stubs = [e for e in data["entities"] if e["object_type"] == "unknown"]
        assert stubs, "expected stub entities for function-module parameter types"
        for stub in stubs[:5]:
            assert stub["domain"] == "DDIC"
            assert stub["technical_name"], "stub must carry the referenced type name"
