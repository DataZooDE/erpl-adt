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
    clas.changed_at = "2026-07-18T09:00:00Z";
    feed.entities.push_back(clas);

    CatalogField f1(tabl_id);
    f1.id = tabl_id.Value() + "#PRICE";
    f1.name = "PRICE";
    f1.description = "Ticket price";
    f1.data_type = "S_PRICE";
    f1.length = 15;
    f1.decimals = 2;
    f1.unit = "CURR";
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

// Separate from MakeSeededStore() so the subtype-specific tests don't shift
// the entity/object-type counts the existing catalog_object_types and
// catalog_search tests already assert on.
std::shared_ptr<ICatalogStore> MakeBwSeededStore() {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    CatalogFeed feed;
    feed.system_sid = "A4H";
    feed.built_at = "2026-07-19T10:00:00Z";

    auto query_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    CatalogEntity query(query_id);
    query.system_sid = "A4H";
    query.domain = CatalogDomain::Bw;
    query.object_type = "ELEM";
    query.object_subtype = "REP";
    query.technical_name = "0BPC_BPF_ACTIVITY_REP";
    query.display_name = "BPC BPF Activity Report";
    query.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(query);

    auto var_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0CMONTH");
    CatalogEntity var(var_id);
    var.system_sid = "A4H";
    var.domain = CatalogDomain::Bw;
    var.object_type = "ELEM";
    var.object_subtype = "VAR";
    var.technical_name = "0CMONTH";
    var.display_name = "Current Calendar Month";
    var.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(var);

    REQUIRE(store->WriteFeed(feed).IsOk());
    return store;
}

nlohmann::json ParseContent(const ToolResult& result) {
    REQUIRE_FALSE(result.content.empty());
    return nlohmann::json::parse(result.content[0]["text"].get<std::string>());
}

} // anonymous namespace

TEST_CASE("RegisterCatalogStoreTools: registers all 10 catalog tools",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    for (const auto* name : {"catalog_search", "catalog_get", "catalog_where_used",
                              "catalog_lineage", "catalog_driver_tree", "catalog_sync_status",
                              "catalog_stats", "catalog_object_types", "catalog_object_subtypes",
                              "catalog_annotate"}) {
        CHECK(registry.HasTool(name));
    }
    CHECK(registry.Tools().size() == 10);
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
    CHECK(get_j["biz_curated_by"] == "mcp");
    CHECK(get_j.contains("biz_curated_at"));
    CHECK(get_j["extracted_at"] == "2026-07-19 10:00:00");
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
    CHECK(j["hits"][0]["extracted_at"] == "2026-07-19 10:00:00");
    CHECK(j["hits"][0]["changed_at"] == "2026-07-18 09:00:00");
    CHECK(j.contains("schema_version"));
    CHECK(j.contains("cache_synced_at"));
}

TEST_CASE("catalog_search: an empty query browses all entities instead of erroring",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", {{"query", ""}});
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    CHECK(j["hits"].size() == 2);
}

TEST_CASE("catalog_search: query param can be omitted entirely (implies browse)",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", nlohmann::json::object());
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    CHECK(j["hits"].size() == 2);
}

TEST_CASE("catalog_search: paginates via cursor/next_cursor/has_more",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto page1 = CallTool(registry, "catalog_search", {{"query", ""}, {"max_results", 1}});
    auto j1 = ParseContent(page1);
    REQUIRE(j1["hits"].size() == 1);
    CHECK(j1["has_more"] == true);
    CHECK(j1["next_cursor"] == 1);

    auto page2 = CallTool(registry, "catalog_search",
                          {{"query", ""}, {"max_results", 1}, {"cursor", 1}});
    auto j2 = ParseContent(page2);
    REQUIRE(j2["hits"].size() == 1);
    CHECK(j2["has_more"] == false);
    CHECK(j2["next_cursor"].is_null());
    CHECK(j2["hits"][0]["id"] != j1["hits"][0]["id"]);
}

TEST_CASE("catalog_search: domain and curated_only filters narrow results",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", {{"query", ""}, {"domain", "ABAP"}});
    auto j = ParseContent(result);
    REQUIRE(j["hits"].size() == 1);
    CHECK(j["hits"][0]["technical_name"] == "ZCL_PROCUREMENT");

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    REQUIRE_FALSE(CallTool(registry, "catalog_annotate",
                           {{"id", tabl_id.Value()}, {"definition", "x"}})
                      .is_error);

    auto curated_result =
        CallTool(registry, "catalog_search", {{"query", ""}, {"curated_only", true}});
    auto curated_j = ParseContent(curated_result);
    REQUIRE(curated_j["hits"].size() == 1);
    CHECK(curated_j["hits"][0]["technical_name"] == "SFLIGHT");
}

TEST_CASE("catalog_search: object_type filter narrows results", "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", {{"query", ""}, {"object_type", "CLAS"}});
    auto j = ParseContent(result);
    REQUIRE(j["hits"].size() == 1);
    CHECK(j["hits"][0]["technical_name"] == "ZCL_PROCUREMENT");

    auto none = CallTool(registry, "catalog_search", {{"query", ""}, {"object_type", "FUGR/FF"}});
    auto none_j = ParseContent(none);
    CHECK(none_j["hits"].empty());
}

TEST_CASE("catalog_object_types: returns distinct (domain, object_type) counts",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_object_types", nlohmann::json::object());
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    REQUIRE(j["types"].size() == 2);

    bool found_tabl = false, found_clas = false;
    for (const auto& t : j["types"]) {
        if (t["object_type"] == "TABL") {
            found_tabl = true;
            CHECK(t["domain"] == "DDIC");
            CHECK(t["count"] == 1);
        }
        if (t["object_type"] == "CLAS") {
            found_clas = true;
            CHECK(t["domain"] == "ABAP");
            CHECK(t["count"] == 1);
        }
    }
    CHECK(found_tabl);
    CHECK(found_clas);
}

TEST_CASE("catalog_object_types: query param narrows counts to the current search "
          "scope",
          "[mcp][catalog][handlers]") {
    auto store = MakeSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result =
        CallTool(registry, "catalog_object_types", {{"query", "procurement"}});
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    REQUIRE(j["types"].size() == 1);
    CHECK(j["types"][0]["object_type"] == "CLAS");
}

TEST_CASE("catalog_get: serializes object_subtype when present",
          "[mcp][catalog][handlers]") {
    auto store = MakeBwSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto query_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    auto result = CallTool(registry, "catalog_get", {{"id", query_id.Value()}});
    auto j = ParseContent(result);
    CHECK(j["object_subtype"] == "REP");
}

TEST_CASE("catalog_search: subtype filter narrows results to real queries",
          "[mcp][catalog][handlers]") {
    auto store = MakeBwSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_search", {{"query", ""}, {"subtype", "REP"}});
    auto j = ParseContent(result);
    REQUIRE(j["hits"].size() == 1);
    CHECK(j["hits"][0]["technical_name"] == "0BPC_BPF_ACTIVITY_REP");

    auto var_only = CallTool(registry, "catalog_search", {{"query", ""}, {"subtype", "VAR"}});
    auto var_j = ParseContent(var_only);
    REQUIRE(var_j["hits"].size() == 1);
    CHECK(var_j["hits"][0]["technical_name"] == "0CMONTH");
}

TEST_CASE("catalog_object_subtypes: returns distinct "
          "(domain, object_type, object_subtype) counts",
          "[mcp][catalog][handlers]") {
    auto store = MakeBwSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result = CallTool(registry, "catalog_object_subtypes", nlohmann::json::object());
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    REQUIRE(j["subtypes"].size() == 2);

    bool found_rep = false, found_var = false;
    for (const auto& t : j["subtypes"]) {
        CHECK(t["domain"] == "BW");
        CHECK(t["object_type"] == "ELEM");
        if (t["object_subtype"] == "REP") { found_rep = true; CHECK(t["count"] == 1); }
        if (t["object_subtype"] == "VAR") { found_var = true; CHECK(t["count"] == 1); }
    }
    CHECK(found_rep);
    CHECK(found_var);
}

TEST_CASE("catalog_object_subtypes: query param narrows counts to the current "
          "search scope",
          "[mcp][catalog][handlers]") {
    auto store = MakeBwSeededStore();
    ToolRegistry registry;
    RegisterCatalogStoreTools(registry, store);

    auto result =
        CallTool(registry, "catalog_object_subtypes", {{"query", "activity"}});
    REQUIRE_FALSE(result.is_error);
    auto j = ParseContent(result);
    REQUIRE(j["subtypes"].size() == 1);
    CHECK(j["subtypes"][0]["object_subtype"] == "REP");
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
    CHECK(j["fields"][0]["description"] == "Ticket price");
    CHECK(j["fields"][0]["length"] == 15);
    CHECK(j["fields"][0]["decimals"] == 2);
    CHECK(j["fields"][0]["unit"] == "CURR");
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
