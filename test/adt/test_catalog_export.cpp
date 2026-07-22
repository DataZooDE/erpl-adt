#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_export.hpp>
#include <erpl_adt/adt/catalog_ids.hpp>

using namespace erpl_adt;

namespace {

CatalogFeed MakeSampleFeed() {
    CatalogFeed feed;
    feed.system_sid = "A4H";
    feed.built_at = "2026-07-19T10:00:00Z";

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CatalogEntity tabl(tabl_id);
    tabl.system_sid = "A4H";
    tabl.domain = CatalogDomain::Ddic;
    tabl.object_type = "TABL";
    tabl.technical_name = "SFLIGHT";
    tabl.display_name = "Flight schedule";
    tabl.package_or_infoarea = "STEST";
    tabl.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(std::move(tabl));

    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_EXAMPLE");
    CatalogEntity clas(clas_id);
    clas.system_sid = "A4H";
    clas.domain = CatalogDomain::Abap;
    clas.object_type = "CLAS";
    clas.technical_name = "ZCL_EXAMPLE";
    clas.display_name = "Example class";
    clas.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(std::move(clas));

    CatalogField f1(tabl_id);
    f1.id = tabl_id.Value() + "#CARRID";
    f1.name = "CARRID";
    f1.description = "Airline code";
    f1.data_type = "S_CARR_ID";
    feed.fields.push_back(std::move(f1));

    CatalogEdge edge(clas_id, tabl_id);
    edge.id = clas_id.Value() + "->" + tabl_id.Value();
    edge.kind = "uses";
    edge.extracted_at = "2026-07-19T10:00:00Z";
    feed.edges.push_back(std::move(edge));

    feed.warnings.push_back("table ZFOO: not found");

    return feed;
}

} // anonymous namespace

TEST_CASE("RenderCatalogFeedJson: is byte-identical across two renders of an "
          "unchanged feed",
          "[adt][catalog][export]") {
    auto feed = MakeSampleFeed();
    auto a = RenderCatalogFeedJson(feed);
    auto b = RenderCatalogFeedJson(feed);
    CHECK(a == b);
    CHECK(a.find("SFLIGHT") != std::string::npos);
    CHECK(a.find("ZCL_EXAMPLE") != std::string::npos);
    CHECK(a.find("catalog.feed.v1") != std::string::npos);
}

TEST_CASE("RenderCatalogFeedJson: entities/fields/edges/warnings all present",
          "[adt][catalog][export]") {
    auto feed = MakeSampleFeed();
    auto json = RenderCatalogFeedJson(feed);
    CHECK(json.find("CARRID") != std::string::npos);
    CHECK(json.find("\"kind\":\"uses\"") != std::string::npos);
    CHECK(json.find("not found") != std::string::npos);
    CHECK(json.find("Airline code") != std::string::npos);
}

TEST_CASE("RenderCatalogFeedOpenMetadataJson: DDIC entities become tables with columns; "
          "ABAP entities are omitted from the table profile",
          "[adt][catalog][export]") {
    auto feed = MakeSampleFeed();
    auto json = RenderCatalogFeedOpenMetadataJson(feed, "erpl_adt", "A4H");

    CHECK(json.find("SFLIGHT") != std::string::npos);
    CHECK(json.find("CARRID") != std::string::npos);
    CHECK(json.find("A4H.STEST.SFLIGHT") != std::string::npos);  // FQN
    CHECK(json.find("ZCL_EXAMPLE") == std::string::npos);
    CHECK(json.find("Airline code") != std::string::npos);
}

TEST_CASE("RenderCatalogFeedMermaid: renders entity nodes and an edge",
          "[adt][catalog][export]") {
    auto feed = MakeSampleFeed();
    auto mermaid = RenderCatalogFeedMermaid(feed);
    CHECK(mermaid.find("graph") != std::string::npos);
    CHECK(mermaid.find("SFLIGHT") != std::string::npos);
    CHECK(mermaid.find("ZCL_EXAMPLE") != std::string::npos);
    CHECK(mermaid.find("-->") != std::string::npos);
}
