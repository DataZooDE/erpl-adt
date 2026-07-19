#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/mcp_http_server.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

using namespace erpl_adt;

namespace {

ToolRegistry MakeEchoRegistry() {
    ToolRegistry registry;
    registry.Register(
        "echo", "Echoes back its input",
        {{"type", "object"},
         {"properties", {{"text", {{"type", "string"}, {"description", "text to echo"}}}}},
         {"required", nlohmann::json::array({"text"})}},
        [](const nlohmann::json& params) -> ToolResult {
            return ToolResult{false, nlohmann::json::array({{{"type", "text"},
                                                              {"text", params.value("text", "")}}})};
        });
    return registry;
}

// Picks a high, unlikely-to-collide port per test run so parallel ctest
// shards don't fight over the same port.
uint16_t TestPort() { return 18734; }

} // anonymous namespace

TEST_CASE("McpHttpServer: tools/call over HTTP returns the same shape a stdio call would",
          "[mcp][http]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = TestPort();

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    // Give the listener a moment to bind before the client connects.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json init_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "initialize"},
                               {"params", {{"protocolVersion", "2024-11-05"},
                                           {"capabilities", nlohmann::json::object()},
                                           {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}};
    auto init_response = client.Post("/mcp", init_msg.dump(), "application/json");
    REQUIRE(init_response != nullptr);
    CHECK(init_response->status == 200);

    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 2},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hello"}}}}}};
    auto call_response = client.Post("/mcp", call_msg.dump(), "application/json");
    REQUIRE(call_response != nullptr);
    REQUIRE(call_response->status == 200);

    auto body = nlohmann::json::parse(call_response->body);
    CHECK(body["jsonrpc"] == "2.0");
    CHECK(body["id"] == 2);
    CHECK(body["result"]["content"][0]["text"] == "hello");

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: sets CORS headers so a browser-based client "
          "(e.g. the Flutter web client) isn't blocked by same-origin policy",
          "[mcp][http]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 2);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);

    // Preflight (OPTIONS) — browsers send this before a cross-origin POST
    // with a JSON content type.
    auto preflight = client.Options("/mcp");
    REQUIRE(preflight != nullptr);
    CHECK(preflight->status == 204);
    CHECK(preflight->get_header_value("Access-Control-Allow-Origin") == "*");

    // The actual response also needs the header — browsers check both.
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};
    auto response = client.Post("/mcp", call_msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->get_header_value("Access-Control-Allow-Origin") == "*");

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: serve_webui=false leaves unknown paths 404 (no behavior change for mcp --http)",
          "[mcp][http][webui]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 3);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    auto response = client.Get("/");
    REQUIRE(response != nullptr);
    CHECK(response->status == 404);

    server.Stop();
    server_thread.join();
}

#ifdef ERPL_ADT_HAVE_WEBUI

TEST_CASE("McpHttpServer: serve_webui=true serves the embedded Flutter web app",
          "[mcp][http][webui]") {
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/true);
    auto port = static_cast<uint16_t>(TestPort() + 4);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);

    auto index = client.Get("/");
    REQUIRE(index != nullptr);
    CHECK(index->status == 200);
    CHECK(index->get_header_value("Content-Type") == "text/html; charset=utf-8");
    CHECK(index->body.find("<html") != std::string::npos);

    auto script = client.Get("/main.dart.js");
    REQUIRE(script != nullptr);
    CHECK(script->status == 200);
    CHECK(script->get_header_value("Content-Type") == "application/javascript");

    // SPA fallback: go_router does client-side routing, so a deep link with
    // no matching embedded file must still resolve to index.html (200), not
    // a raw 404 — a hard refresh on e.g. /entity/<id> must keep working.
    auto deep_link = client.Get("/entity/abc123");
    REQUIRE(deep_link != nullptr);
    CHECK(deep_link->status == 200);
    CHECK(deep_link->get_header_value("Content-Type") == "text/html; charset=utf-8");
    CHECK(deep_link->body == index->body);

    // /mcp and /healthz still work — serve_webui must not shadow them.
    auto health = client.Get("/healthz");
    REQUIRE(health != nullptr);
    CHECK(health->status == 200);

    server.Stop();
    server_thread.join();
}

#else

TEST_CASE("McpHttpServer: serve_webui=true without an embedded build returns an instructional message",
          "[mcp][http][webui]") {
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/true);
    auto port = static_cast<uint16_t>(TestPort() + 4);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    auto response = client.Get("/");
    REQUIRE(response != nullptr);
    CHECK(response->status == 501);
    CHECK(response->body.find("make webui") != std::string::npos);

    server.Stop();
    server_thread.join();
}

#endif

TEST_CASE("McpHttpServer: /healthz responds ok", "[mcp][http]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 1);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    auto response = client.Get("/healthz");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);

    server.Stop();
    server_thread.join();
}
