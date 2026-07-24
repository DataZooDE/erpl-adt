#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/adt/catalog_overlay.hpp>
#include <erpl_adt/storage/duckdb_catalog_store.hpp>

using namespace erpl_adt;

namespace {

std::shared_ptr<ICatalogStore> MakeSeededStore() {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    CatalogFeed feed;
    feed.system_sid = "A4H";
    feed.built_at = "2026-07-19T10:00:00Z";

    auto entity_id = DeriveEntityId("A4H", CatalogDomain::Bw, "IOBJ", "0PUR_VALUE");
    CatalogEntity entity(entity_id);
    entity.system_sid = "A4H";
    entity.domain = CatalogDomain::Bw;
    entity.object_type = "IOBJ";
    entity.technical_name = "0PUR_VALUE";
    entity.display_name = "0PUR_VALUE";
    entity.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(std::move(entity));

    REQUIRE(store->WriteFeed(feed).IsOk());
    return store;
}

} // anonymous namespace

TEST_CASE("ParseOverlayYaml: parses a well-formed overlay file", "[adt][catalog][overlay]") {
    const std::string yaml = R"yaml(
0abc123:
  definition: "Total value of procurement transactions"
  owner: "jane.doe@example.com"
  lob: "Procurement"
  confidentiality: Internal
0def456:
  definition: "Material group description"
)yaml";

    auto result = ParseOverlayYaml(yaml);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);

    const auto& e0 = result.Value()[0];
    CHECK(e0.entity_id == "0abc123");
    REQUIRE(e0.definition.has_value());
    CHECK(*e0.definition == "Total value of procurement transactions");
    REQUIRE(e0.confidentiality.has_value());
    CHECK(*e0.confidentiality == "Internal");

    const auto& e1 = result.Value()[1];
    CHECK(e1.entity_id == "0def456");
    CHECK_FALSE(e1.owner.has_value());
}

TEST_CASE("ParseOverlayYaml: rejects an invalid confidentiality value",
          "[adt][catalog][overlay]") {
    const std::string yaml = R"yaml(
0abc123:
  confidentiality: TopSecret
)yaml";

    auto result = ParseOverlayYaml(yaml);
    REQUIRE(result.IsErr());
}

TEST_CASE("ParseOverlayYaml: empty document yields no entries", "[adt][catalog][overlay]") {
    auto result = ParseOverlayYaml("");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ApplyOverlay: curates a known entity and it is distinguishable "
          "from technical fields",
          "[adt][catalog][overlay]") {
    auto store = MakeSeededStore();
    auto entity_id = DeriveEntityId("A4H", CatalogDomain::Bw, "IOBJ", "0PUR_VALUE");

    std::vector<OverlayEntry> entries = {
        {entity_id.Value(), "Procurement value", "jane.doe@example.com", "Procurement", "Internal"}};

    auto result = ApplyOverlay(*store, entries, "jane.doe@example.com");
    CHECK(result.applied_count == 1);
    CHECK(result.orphan_ids.empty());

    auto entity = store->GetEntity(entity_id);
    REQUIRE(entity.IsOk());
    REQUIRE(entity.Value().has_value());
    REQUIRE(entity.Value()->biz_definition.has_value());
    CHECK(*entity.Value()->biz_definition == "Procurement value");
    // Still distinguishable — technical fields untouched by curation.
    CHECK(entity.Value()->technical_name == "0PUR_VALUE");
}

TEST_CASE("ApplyOverlay: an entry with an unknown entity_id is reported as an orphan, "
          "not silently dropped or fatal",
          "[adt][catalog][overlay]") {
    auto store = MakeSeededStore();

    std::vector<OverlayEntry> entries = {
        {"nonexistent-id", "some definition", std::nullopt, std::nullopt, std::nullopt}};

    auto result = ApplyOverlay(*store, entries, "jane.doe@example.com");
    CHECK(result.applied_count == 0);
    REQUIRE(result.orphan_ids.size() == 1);
    CHECK(result.orphan_ids[0] == "nonexistent-id");
}

TEST_CASE("ApplyOverlay: one bad entry does not block the rest of the batch",
          "[adt][catalog][overlay]") {
    auto store = MakeSeededStore();
    auto entity_id = DeriveEntityId("A4H", CatalogDomain::Bw, "IOBJ", "0PUR_VALUE");

    std::vector<OverlayEntry> entries = {
        {"nonexistent-id", "orphaned", std::nullopt, std::nullopt, std::nullopt},
        {entity_id.Value(), "Procurement value", std::nullopt, std::nullopt, std::nullopt}};

    auto result = ApplyOverlay(*store, entries, "jane.doe@example.com");
    CHECK(result.applied_count == 1);
    CHECK(result.orphan_ids.size() == 1);
}
