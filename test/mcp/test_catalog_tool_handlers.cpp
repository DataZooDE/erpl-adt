#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/mcp/catalog_tool_handlers.hpp>
#include <erpl_adt/storage/duckdb_catalog_store.hpp>

#include <nlohmann/json.hpp>

using namespace erpl_adt;

namespace {

std::shared_ptr<ICatalogStore> MakeSeededStore() {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

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
    tabl.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(tabl);

    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    CatalogEntity clas(clas_id);
    clas.system_sid = "A4H";
    clas.domain = CatalogDomain::Abap;
    clas.object_type = "CLAS";
    clas.technical_name = "ZCL_PROCUREMENT";
    clas.display_name = "Procurement value calculator";
    clas.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(clas);

    CatalogField f1(tabl_id);
    f1.id = tabl_id.Value() + "#PRICE";
    f1.name = "PRICE";
    f1.data_type = "S_PRICE";
    feed.fields.push_back(std::move(f1));

    CatalogEdge edge(clas_id, tabl_id);
    edge.id = clas_id.Value() + "->" + tabl_id.Value();
    edge.kind = "uses";
    edge.extracted_at = "2026-07-19T10:00:00Z";
    feed.edges.push_back(std::move(edge));

    REQUIRE(store->WriteFeed(feed).IsOk());
    return store;
}

ToolResult CallTool(ToolRegistry& registry, const std::string& name, nlohmann::json params) {
    return registry.Execute(name, params);
}

nlohmann::json ParseContent(const ToolResult& result) {
    REQUIRE_FALSE(result.content.empty());
    return nlohmann::json::parse(result.content[0]["text"].get<std::string>());
}

} // anonymous namespace

TEST_CASE("RegisterCatalogStoreTools: registers all 8 catalog tools",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    for (const auto* name : {"catalog_search", "catalog_get", "catalog_where_used",
                              "catalog_lineage", "catalog_driver_tree", "catalog_sync_status",
                              "catalog_stats", "catalog_annotate"}) {
        CHECK(registry.HasTool(name));
    }
    CHECK(registry.Tools().size() == 8);
}

TEST_CASE("catalog_annotate: curates business fields onto a known entity",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto result = CallTool(registry, "catalog_annotate",
                           {{"id", tabl_id.Value()}, {"definition", "Flight schedule master data"}});
    auto j = ParseContent(result);
    CHECK(j["applied"] == 1);

    auto get_result = CallTool(registry, "catalog_get", {{"id", tabl_id.Value()}});
    auto get_j = ParseContent(get_result);
    CHECK(get_j["biz_definition"] == "Flight schedule master data");
}

TEST_CASE("catalog_annotate: an unknown id is a tool error, not a silent no-op",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_annotate",
                           {{"id", "nonexistent"}, {"definition", "x"}});
    CHECK(result.is_error);
}

TEST_CASE("catalog_search: finds a curated entity by full-text match", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", {{"query", "procurement"}});
    auto j = ParseContent(result);
    REQUIRE_FALSE(j["hits"].empty());
    CHECK(j["hits"][0]["technical_name"] == "ZCL_PROCUREMENT");
    CHECK(j.contains("schema_version"));
    CHECK(j.contains("cache_synced_at"));
}

TEST_CASE("catalog_get: returns an entity with its fields", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto result = CallTool(registry, "catalog_get", {{"id", tabl_id.Value()}});
    auto j = ParseContent(result);
    CHECK(j["technical_name"] == "SFLIGHT");
    REQUIRE(j["fields"].size() == 1);
    CHECK(j["fields"][0]["name"] == "PRICE");
}

TEST_CASE("catalog_get: unknown id is a param error", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_get", {{"id", "nonexistent"}});
    CHECK(result.is_error);
}

TEST_CASE("catalog_where_used: finds the entity referencing the target", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    auto result = CallTool(registry, "catalog_where_used", {{"id", tabl_id.Value()}});
    auto j = ParseContent(result);
    REQUIRE(j["edges"].size() == 1);
    CHECK(j["edges"][0]["from_id"] == clas_id.Value());
}

TEST_CASE("catalog_lineage: walks outgoing edges from the entity", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto result = CallTool(registry, "catalog_lineage", {{"id", clas_id.Value()}});
    auto j = ParseContent(result);
    REQUIRE(j["chain"].size() == 1);
    CHECK(j["chain"][0]["to_id"] == tabl_id.Value());
}

TEST_CASE("catalog_stats: reports row counts", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_stats", nlohmann::json::object());
    auto j = ParseContent(result);
    CHECK(j["entity_count"] == 2);
    CHECK(j["field_count"] == 1);
    CHECK(j["edge_count"] == 1);
}

TEST_CASE("catalog_sync_status: empty before any sync has run", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_sync_status", nlohmann::json::object());
    auto j = ParseContent(result);
    CHECK(j["runs"].empty());
}
