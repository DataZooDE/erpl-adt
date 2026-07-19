#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/adt/catalog_lineage.hpp>

using namespace erpl_adt;

namespace {

BwLineageGraph MakeSampleGraph() {
    BwLineageGraph graph;
    graph.root_type = "DTPA";
    graph.root_name = "ZDTP";

    graph.nodes = {
        {"obj:DTPA:ZDTP", "DTPA", "ZDTP", "dtp", "", "a", {}},
        {"obj:RSDS:ZSRC", "RSDS", "ZSRC", "source_object", "", "a", {}},
        {"obj:ADSO:ZADSO", "ADSO", "ZADSO", "target_object", "", "a", {}},
        {"field:RSDS:ZSRC:MATNR", "RSDS_FIELD", "MATNR", "source_field", "", "a", {}},
        {"field:ADSO:ZADSO:MATNR", "ADSO_FIELD", "MATNR", "target_field", "", "a", {}},
    };
    graph.edges = {
        {"e1", "obj:DTPA:ZDTP", "obj:RSDS:ZSRC", "dtp_source", {}},
        {"e2", "field:RSDS:ZSRC:MATNR", "field:ADSO:ZADSO:MATNR", "field_mapping",
         {{"rule_type", "1:1"}}},
    };
    return graph;
}

} // anonymous namespace

TEST_CASE("ConvertBwLineageGraph: object-level edges map to entity lineage edges",
          "[adt][catalog][lineage]") {
    auto graph = MakeSampleGraph();
    auto dtp_id = DeriveEntityId("A4H", CatalogDomain::Bw, "DTPA", "ZDTP");
    std::set<std::string> known = {dtp_id.Value()};

    auto conversion = ConvertBwLineageGraph("A4H", graph, known);

    bool found_dtp_to_rsds = false;
    for (const auto& edge : conversion.edges) {
        if (edge.from_id.Value() == dtp_id.Value()) {
            found_dtp_to_rsds = true;
            CHECK(edge.kind == "lineage");
        }
    }
    CHECK(found_dtp_to_rsds);
}

TEST_CASE("ConvertBwLineageGraph: out-of-scope referenced objects get stub entities",
          "[adt][catalog][lineage]") {
    auto graph = MakeSampleGraph();
    auto dtp_id = DeriveEntityId("A4H", CatalogDomain::Bw, "DTPA", "ZDTP");
    std::set<std::string> known = {dtp_id.Value()};

    auto conversion = ConvertBwLineageGraph("A4H", graph, known);

    auto rsds_id = DeriveEntityId("A4H", CatalogDomain::Bw, "RSDS", "ZSRC");
    auto adso_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "ZADSO");

    bool has_rsds_stub = false, has_adso_stub = false, has_dtp_stub = false;
    for (const auto& e : conversion.stub_entities) {
        if (e.id.Value() == rsds_id.Value()) has_rsds_stub = true;
        if (e.id.Value() == adso_id.Value()) has_adso_stub = true;
        if (e.id.Value() == dtp_id.Value()) has_dtp_stub = true;
    }
    CHECK(has_rsds_stub);
    CHECK(has_adso_stub);
    CHECK_FALSE(has_dtp_stub);  // DTP was already known — must not be re-stubbed
}

TEST_CASE("ConvertBwLineageGraph: field-level field_mapping edges are aggregated onto "
          "one entity-level lineage edge with a field_mapping payload",
          "[adt][catalog][lineage]") {
    auto graph = MakeSampleGraph();
    std::set<std::string> known;  // nothing known — everything gets stubbed

    auto conversion = ConvertBwLineageGraph("A4H", graph, known);

    auto rsds_id = DeriveEntityId("A4H", CatalogDomain::Bw, "RSDS", "ZSRC");
    auto adso_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "ZADSO");

    const CatalogEdge* field_edge = nullptr;
    for (const auto& e : conversion.edges) {
        if (e.from_id.Value() == rsds_id.Value() && e.to_id.Value() == adso_id.Value()) {
            field_edge = &e;
        }
    }
    REQUIRE(field_edge != nullptr);
    CHECK(field_edge->kind == "lineage");
    CHECK_FALSE(field_edge->field_mapping_json.empty());
    CHECK(field_edge->field_mapping_json.find("MATNR") != std::string::npos);
}

TEST_CASE("CatalogWhereUsed: returns every edge pointing at the target",
          "[adt][catalog][lineage]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Bw, "DTPA", "A");
    auto b = DeriveEntityId("A4H", CatalogDomain::Bw, "RSDS", "B");
    auto c = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "C");

    CatalogFeed feed;
    CatalogEdge e1(a, b);
    e1.kind = "lineage";
    CatalogEdge e2(c, b);
    e2.kind = "uses";
    CatalogEdge e3(a, c);  // does not point at b
    e3.kind = "lineage";
    feed.edges = {e1, e2, e3};

    auto used = CatalogWhereUsed(feed, b);
    REQUIRE(used.size() == 2);
    for (const auto& e : used) {
        CHECK(e.to_id.Value() == b.Value());
    }
}

TEST_CASE("CatalogColumnLineage: walks field_mapping edges back to the earliest source",
          "[adt][catalog][lineage]") {
    auto src = DeriveEntityId("A4H", CatalogDomain::Bw, "RSDS", "SRC");
    auto mid = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "MID");
    auto dst = DeriveEntityId("A4H", CatalogDomain::Bw, "QUERY", "DST");

    CatalogFeed feed;
    // SRC.MATNR -> MID.MATNR -> DST.MATERIAL
    CatalogEdge e1(src, mid);
    e1.kind = "lineage";
    e1.field_mapping_json = R"([{"from_field":"MATNR","to_field":"MATNR"}])";
    CatalogEdge e2(mid, dst);
    e2.kind = "lineage";
    e2.field_mapping_json = R"([{"from_field":"MATNR","to_field":"MATERIAL"}])";
    feed.edges = {e1, e2};

    auto chain = CatalogColumnLineage(feed, dst, "MATERIAL");
    REQUIRE(chain.size() == 3);
    CHECK(chain[0].entity_id.Value() == dst.Value());
    CHECK(chain[0].field_name == "MATERIAL");
    CHECK(chain[1].entity_id.Value() == mid.Value());
    CHECK(chain[1].field_name == "MATNR");
    CHECK(chain[2].entity_id.Value() == src.Value());
    CHECK(chain[2].field_name == "MATNR");
}

TEST_CASE("CatalogColumnLineage: stops at max_depth to avoid unbounded cycles",
          "[adt][catalog][lineage]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "A");
    auto b = DeriveEntityId("A4H", CatalogDomain::Bw, "ADSO", "B");

    CatalogFeed feed;
    // A cycle: A.F -> B.F -> A.F -> ...
    CatalogEdge e1(b, a);
    e1.kind = "lineage";
    e1.field_mapping_json = R"([{"from_field":"F","to_field":"F"}])";
    CatalogEdge e2(a, b);
    e2.kind = "lineage";
    e2.field_mapping_json = R"([{"from_field":"F","to_field":"F"}])";
    feed.edges = {e1, e2};

    auto chain = CatalogColumnLineage(feed, a, "F", /*max_depth=*/4);
    CHECK(chain.size() <= 5);  // depth-bounded, must terminate
}
