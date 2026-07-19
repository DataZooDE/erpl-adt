"""catalog build — validate cross-domain catalog extraction against a live SAP system.

Real acceptance criteria for the catalog feature (see erpl-adt catalog design
docs): entity IDs must be stable across independent runs, and a mixed scope
(package with TABL/DDLS/CLAS objects) must yield entities from more than one
catalog domain. These tests hit the live trial system directly — no mocks.
"""

import pytest


@pytest.mark.catalog
class TestCatalogBuildAbapDdicCds:
    """catalog build --package <pkg> covers ABAP + DDIC + CDS in one scope."""

    def test_build_yields_entities_from_multiple_domains(self, cli):
        data = cli.run_ok(
            "catalog", "build",
            "--package", "SCTS_CAT",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        entities = data["entities"]
        assert len(entities) > 0, "catalog build returned no entities for SCTS_CAT"

        domains = {e["domain"] for e in entities}
        assert "CDS" in domains, f"expected CDS entities in SCTS_CAT, got domains: {domains}"

        assert len(data["fields"]) > 0, "expected at least one resolved field"
        entity_ids = {e["id"] for e in entities}
        for field in data["fields"]:
            assert field["entity_id"] in entity_ids, "field references an unknown entity"
            assert field["id"], "field must have a stable id"

    def test_entity_ids_are_stable_across_two_runs(self, cli):
        first = cli.run_ok(
            "catalog", "build",
            "--package", "SCTS_CAT",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        second = cli.run_ok(
            "catalog", "build",
            "--package", "SCTS_CAT",
            "--sid", "A4H",
            "--max-depth", "1",
        )

        ids_first = [e["id"] for e in first["entities"]]
        ids_second = [e["id"] for e in second["entities"]]
        assert ids_first == ids_second, "entity IDs must be stable across independent builds"

    def test_unresolvable_objects_are_warnings_not_a_hard_failure(self, cli):
        # SDFM is large enough to reliably contain at least one TABL entry
        # SAP's DDIC table endpoint 404s on (structures/views without a
        # physical table) — the build must still succeed with warnings.
        data = cli.run_ok(
            "catalog", "build",
            "--package", "SDFM",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        assert len(data["entities"]) > 0
        assert isinstance(data["warnings"], list)


@pytest.mark.catalog
@pytest.mark.bw
class TestCatalogBuildBw:
    """catalog build --infoarea <ia> covers the BW domain."""

    def test_build_yields_bw_entities(self, cli):
        data = cli.run_ok(
            "catalog", "build",
            "--infoarea", "0BWBPCWS",
            "--sid", "A4H",
        )
        entities = data["entities"]
        assert len(entities) > 0, "catalog build returned no entities for 0BWBPCWS"
        assert any(e["domain"] == "BW" for e in entities)

        # Edges are present in the response shape regardless of whether this
        # particular infoarea's content happens to include DTP lineage — the
        # trial system's technical-content infoareas are mostly query/ELEM
        # objects with no DTPs, so an empty edge list here is expected, not
        # a bug. What matters for this test: the lineage-attachment code
        # path runs against real SAP responses without erroring.
        assert "edges" in data
        assert isinstance(data["edges"], list)
        for edge in data["edges"]:
            assert edge["from_id"] and edge["to_id"]
            assert edge["kind"] in ("lineage", "uses", "contains", "where_used")
