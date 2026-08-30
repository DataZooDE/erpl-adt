#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/tool_registry.hpp>

using namespace erpl_adt;

TEST_CASE("ToolRegistry: register and list tools", "[mcp][registry]") {
    ToolRegistry registry;

    nlohmann::json schema = {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}}}
        }},
        {"required", nlohmann::json::array({"query"})}
    };

    registry.Register("search", "Search for ABAP objects", schema,
        [](const nlohmann::json& params) -> ToolResult {
            return {false, nlohmann::json::array({
                {{"type", "text"}, {"text", "found: " + params["query"].get<std::string>()}}
            })};
        });

    CHECK(registry.Tools().size() == 1);
    CHECK(registry.Tools()[0].name == "search");
    CHECK(registry.Tools()[0].description == "Search for ABAP objects");
}

TEST_CASE("ToolRegistry: execute registered tool", "[mcp][registry]") {
    ToolRegistry registry;
    registry.Register("echo", "Echo input", nlohmann::json::object(),
        [](const nlohmann::json& params) -> ToolResult {
            return {false, nlohmann::json::array({
                {{"type", "text"}, {"text", params.dump()}}
            })};
        });

    auto result = registry.Execute("echo", {{"msg", "hello"}});
    CHECK_FALSE(result.is_error);
    CHECK(result.content.size() == 1);
}

TEST_CASE("ToolRegistry: execute unknown tool returns error", "[mcp][registry]") {
    ToolRegistry registry;

    auto result = registry.Execute("nonexistent", {});
    CHECK(result.is_error);
    CHECK(result.content[0]["text"].get<std::string>().find("Unknown tool") != std::string::npos);
}

TEST_CASE("ToolRegistry: HasTool", "[mcp][registry]") {
    ToolRegistry registry;
    registry.Register("foo", "Foo tool", nlohmann::json::object(),
        [](const nlohmann::json&) -> ToolResult {
            return {false, nlohmann::json::array()};
        });

    CHECK(registry.HasTool("foo"));
    CHECK_FALSE(registry.HasTool("bar"));
}

TEST_CASE("ToolRegistry: handler exception caught", "[mcp][registry]") {
    ToolRegistry registry;
    registry.Register("throw", "Throws", nlohmann::json::object(),
        [](const nlohmann::json&) -> ToolResult {
            throw std::runtime_error("boom");
        });

    auto result = registry.Execute("throw", {});
    CHECK(result.is_error);
    CHECK(result.content[0]["text"].get<std::string>().find("boom") != std::string::npos);
}

TEST_CASE("ToolRegistry: multiple tools registered", "[mcp][registry]") {
    ToolRegistry registry;
    registry.Register("a", "Tool A", nlohmann::json::object(),
        [](const nlohmann::json&) -> ToolResult {
            return {false, nlohmann::json::array({{{"type", "text"}, {"text", "A"}}})};
        });
    registry.Register("b", "Tool B", nlohmann::json::object(),
        [](const nlohmann::json&) -> ToolResult {
            return {false, nlohmann::json::array({{{"type", "text"}, {"text", "B"}}})};
        });

    CHECK(registry.Tools().size() == 2);
    CHECK(registry.Execute("a", {}).content[0]["text"] == "A");
    CHECK(registry.Execute("b", {}).content[0]["text"] == "B");
}

// ===========================================================================
// Family gating (--tools)
// ===========================================================================

namespace {

ToolRegistry MakeThreeFamilyRegistry() {
    ToolRegistry registry;
    const nlohmann::json schema = {{"type", "object"},
                                   {"properties", nlohmann::json::object()},
                                   {"required", nlohmann::json::array()}};
    auto noop = [](const nlohmann::json&) -> ToolResult {
        return {false, nlohmann::json::array({{{"type", "text"}, {"text", "ok"}}})};
    };
    for (const auto* name : {"adt_search", "adt_read_source", "bw_search",
                             "bw_read_object", "catalog_search"}) {
        registry.Register(name, "test tool", schema, noop);
    }
    return registry;
}

}  // namespace

TEST_CASE("ToolRegistry::FamilyOf splits on the first underscore",
          "[mcp][registry][tools]") {
    CHECK(ToolRegistry::FamilyOf("adt_read_source") == "adt");
    CHECK(ToolRegistry::FamilyOf("bw_lineage_graph") == "bw");
    CHECK(ToolRegistry::FamilyOf("catalog_search") == "catalog");
    CHECK(ToolRegistry::FamilyOf("noUnderscore") == "noUnderscore");
}

TEST_CASE("RetainFamilies keeps only the named families",
          "[mcp][registry][tools]") {
    auto registry = MakeThreeFamilyRegistry();
    const auto removed = registry.RetainFamilies({"adt"});

    CHECK(removed == 3);
    REQUIRE(registry.Tools().size() == 2);
    CHECK(registry.HasTool("adt_search"));
    CHECK(registry.HasTool("adt_read_source"));
    CHECK(!registry.HasTool("bw_search"));
    CHECK(!registry.HasTool("catalog_search"));
}

TEST_CASE("RetainFamilies accepts several families",
          "[mcp][registry][tools]") {
    auto registry = MakeThreeFamilyRegistry();
    CHECK(registry.RetainFamilies({"bw", "catalog"}) == 2);
    REQUIRE(registry.Tools().size() == 3);
    CHECK(!registry.HasTool("adt_search"));
    CHECK(registry.HasTool("bw_search"));
    CHECK(registry.HasTool("catalog_search"));
}

TEST_CASE("RetainFamilies with an empty list changes nothing",
          "[mcp][registry][tools]") {
    // The default: no --tools flag means every registered family stays.
    auto registry = MakeThreeFamilyRegistry();
    CHECK(registry.RetainFamilies({}) == 0);
    CHECK(registry.Tools().size() == 5);
}

TEST_CASE("RetainFamilies drops the handler too, not just the schema",
          "[mcp][registry][tools]") {
    // A hidden tool must not stay callable — tools/list and tools/call have
    // to agree about what exists.
    auto registry = MakeThreeFamilyRegistry();
    (void)registry.RetainFamilies({"adt"});

    auto result = registry.Execute("bw_search", nlohmann::json::object());
    CHECK(result.is_error);
    REQUIRE(result.content.is_array());
    REQUIRE(!result.content.empty());
    CHECK(result.content[0]["text"].get<std::string>().find("Unknown tool") !=
          std::string::npos);
}

TEST_CASE("RetainFamilies preserves registration order",
          "[mcp][registry][tools]") {
    // Deterministic tools/list ordering is what lets clients cache the list
    // and keeps prompt-cache hits stable.
    auto registry = MakeThreeFamilyRegistry();
    (void)registry.RetainFamilies({"adt", "catalog"});
    REQUIRE(registry.Tools().size() == 3);
    CHECK(registry.Tools()[0].name == "adt_search");
    CHECK(registry.Tools()[1].name == "adt_read_source");
    CHECK(registry.Tools()[2].name == "catalog_search");
}
