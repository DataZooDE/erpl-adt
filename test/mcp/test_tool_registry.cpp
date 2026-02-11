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
