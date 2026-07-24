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
class TestCatalogBuildFieldDescriptions:
    """DDIC table fields and CDS view fields carry a human-readable
    description when the source data resolves one — DDIC via data-element
    enrichment, CDS via a field's own @EndUserText.label annotation."""

    def test_ddic_table_fields_have_descriptions(self, cli):
        data = cli.run_ok(
            "catalog", "build",
            "--package", "SDFM",
            "--sid", "A4H",
            "--resolve-ddic-types",
        )
        ddic_entities = {e["id"] for e in data["entities"] if e["domain"] == "DDIC"}
        described = [f for f in data["fields"]
                    if f["entity_id"] in ddic_entities and f.get("description")]
        assert described, "expected at least one DDIC field with a resolved description"


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

        assert "edges" in data
        assert isinstance(data["edges"], list)
        for edge in data["edges"]:
            assert edge["from_id"] and edge["to_id"]
            assert edge["kind"] in ("lineage", "uses", "contains", "where_used")

    def test_bw_queries_get_iobj_refs_as_fields_and_provider_edges(self, cli):
        # 0BWTCT has real ELEM (Query)/InfoProvider content — BFS-walked
        # queries surface as type "ELEM", not "QUERY", and previously got
        # zero fields (iobj_refs weren't collected) and zero edges (the only
        # edge source wired in was DTP lineage, which this scope has none
        # of — InfoProvider<->Query xref/orphan-elem edges exist but were
        # silently dropped by BuildBwScope).
        data = cli.run_ok(
            "catalog", "build",
            "--infoarea", "0BWTCT",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        entities = data["entities"]
        elem_ids = {e["id"] for e in entities if e["object_type"] == "ELEM"}
        assert elem_ids, "expected at least one ELEM (query) entity in 0BWTCT"

        elem_fields = [f for f in data["fields"] if f["entity_id"] in elem_ids]
        assert elem_fields, "expected iobj_refs surfaced as fields on at least one query"
        assert any(f.get("role") in (
            "row", "column", "free", "dimension", "filter", "variable",
            "key_figure", "restricted_key_figure", "calculated_key_figure")
                    for f in elem_fields)

        assert data["edges"], (
            "expected InfoProvider<->Query edges even with no DTP objects in scope")

    def test_bw_calculated_key_figures_carry_a_formula(self, cli):
        # 0BCT_CB has real queries with embedded calculated/restricted key
        # figures (0D_FC_AE_FIXOPER_Q001, 0TCTHP24_CK_100, 0TCT_CKF_Q0107_02)
        # — their formula/restriction was previously discarded entirely
        # (CollectQueryResourceReferences captured only the subcomponent's
        # own flat XML attributes, never the nested formula/selection tree).
        data = cli.run_ok(
            "catalog", "build",
            "--infoarea", "0BCT_CB",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        calc_fields = [f for f in data["fields"]
                       if f.get("role") in ("calculated_key_figure", "restricted_key_figure")]
        assert calc_fields, "expected at least one calculated/restricted key figure field"
        assert any(f.get("formula") for f in calc_fields), (
            "expected at least one calculated/restricted key figure to carry a formula")

    def test_bw_elem_entities_carry_object_subtype(self, cli):
        # 0BCT_CB has both a real query (0BPC_BPF_ACTIVITY_REP, subtype REP)
        # and a variable (0CMONTH, subtype VAR) — both share object_type
        # ELEM and were previously indistinguishable in the catalog, since
        # BwExportedObject.subtype was discarded when building CatalogEntity.
        data = cli.run_ok(
            "catalog", "build",
            "--infoarea", "0BCT_CB",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        elem_entities = [e for e in data["entities"] if e["object_type"] == "ELEM"]
        assert elem_entities, "expected at least one ELEM entity in 0BCT_CB"
        subtypes = {e.get("object_subtype") for e in elem_entities}
        assert "REP" in subtypes, "expected at least one real query (subtype REP)"
        assert subtypes - {"REP"}, (
            "expected at least one non-query ELEM subtype (VAR/CKF/RKF/FILT/STR) "
            "to confirm ELEM isn't uniformly query-only")

    def test_bw_infoproviders_get_dimensional_fields(self, cli):
        # CUBE/MPRO/HCPR/ODSO/HYBR have no per-type detail endpoint the way
        # ADSO does — /sap/bw/modeling/infoprov/{name} covers all of them
        # uniformly. 0RSFC_AE_DEMO has real classic InfoCubes.
        data = cli.run_ok(
            "catalog", "build",
            "--infoarea", "0RSFC_AE_DEMO",
            "--sid", "A4H",
            "--max-depth", "1",
        )
        provider_ids = {e["id"] for e in data["entities"]
                        if e["object_type"] in ("CUBE", "MPRO", "HCPR", "ODSO", "HYBR")}
        assert provider_ids, "expected at least one InfoProvider entity in 0RSFC_AE_DEMO"

        provider_fields = [f for f in data["fields"] if f["entity_id"] in provider_ids]
        assert provider_fields, "expected dimensional fields on at least one InfoProvider"
