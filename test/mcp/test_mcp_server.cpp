#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/mcp_server.hpp>

#include <sstream>

using namespace erpl_adt;

namespace {

ToolRegistry MakeTestRegistry() {
    ToolRegistry registry;
    registry.Register("echo", "Echo the input",
        {{"type", "object"},
         {"properties", {{"message", {{"type", "string"}}}}},
         {"required", nlohmann::json::array({"message"})}},
        [](const nlohmann::json& params) -> ToolResult {
            return {false, nlohmann::json::array({
                {{"type", "text"}, {"text", params.value("message", "")}}
            })};
        });
    return registry;
}

} // anonymous namespace

// ===========================================================================
// HandleMessage
// ===========================================================================

TEST_CASE("McpServer: initialize returns capabilities", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());

    auto& r = *response;
    CHECK(r["jsonrpc"] == "2.0");
    CHECK(r["id"] == 1);
    CHECK(r["result"]["protocolVersion"] == "2024-11-05");
    CHECK(r["result"]["serverInfo"]["name"] == "erpl-adt");
    CHECK(r["result"]["capabilities"].contains("tools"));
}

TEST_CASE("McpServer: tools/list returns registered tools", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());

    auto& tools = (*response)["result"]["tools"];
    REQUIRE(tools.size() == 1);
    CHECK(tools[0]["name"] == "echo");
    CHECK(tools[0]["description"] == "Echo the input");
    CHECK(tools[0]["inputSchema"]["type"] == "object");
}

TEST_CASE("McpServer: tools/call executes tool", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params", {
            {"name", "echo"},
            {"arguments", {{"message", "hello world"}}}
        }}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());

    auto& content = (*response)["result"]["content"];
    REQUIRE(content.size() == 1);
    CHECK(content[0]["type"] == "text");
    CHECK(content[0]["text"] == "hello world");
}

TEST_CASE("McpServer: tools/call unknown tool returns error", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "tools/call"},
        {"params", {{"name", "nonexistent"}}}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());
    CHECK((*response).contains("error"));
}

TEST_CASE("McpServer: unknown method returns error", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 5},
        {"method", "unknown/method"}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());
    CHECK((*response)["error"]["code"] == -32601);
}

TEST_CASE("McpServer: notification returns no response", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };

    auto response = server.HandleMessage(msg);
    CHECK_FALSE(response.has_value());
}

TEST_CASE("McpServer: tools/call missing name returns error", "[mcp][server]") {
    std::istringstream in;
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 6},
        {"method", "tools/call"},
        {"params", nlohmann::json::object()}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());
    CHECK((*response)["error"]["code"] == -32602);
}

// ===========================================================================
// Run (stdio loop)
// ===========================================================================

TEST_CASE("McpServer: Run processes multiple messages", "[mcp][server]") {
    std::string input;
    input += nlohmann::json({
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    }).dump() + "\n";
    input += nlohmann::json({
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}
    }).dump() + "\n";

    std::istringstream in(input);
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    server.Run();

    // Parse output lines.
    std::istringstream output(out.str());
    std::string line;

    REQUIRE(std::getline(output, line));
    auto resp1 = nlohmann::json::parse(line);
    CHECK(resp1["id"] == 1);
    CHECK(resp1["result"]["protocolVersion"] == "2024-11-05");

    REQUIRE(std::getline(output, line));
    auto resp2 = nlohmann::json::parse(line);
    CHECK(resp2["id"] == 2);
    CHECK(resp2["result"]["tools"].size() == 1);
}

TEST_CASE("McpServer: Run handles parse errors", "[mcp][server]") {
    std::istringstream in("not json\n");
    std::ostringstream out;
    McpServer server(MakeTestRegistry(), in, out);

    server.Run();

    auto resp = nlohmann::json::parse(out.str());
    CHECK(resp.contains("error"));
    CHECK(resp["error"]["code"] == -32700);
}
