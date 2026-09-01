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

TEST_CASE("McpHttpServer: CORS headers echo an allowed origin, never '*'",
          "[mcp][http][security]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 2);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    const httplib::Headers origin{{"Origin", "http://localhost:5173"}};

    // Preflight (OPTIONS) — browsers send this before a cross-origin POST
    // with a JSON content type.
    auto preflight = client.Options("/mcp", origin);
    REQUIRE(preflight != nullptr);
    CHECK(preflight->status == 204);
    CHECK(preflight->get_header_value("Access-Control-Allow-Origin") ==
          "http://localhost:5173");

    // The actual response also needs the header — browsers check both.
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};
    auto response = client.Post("/mcp", origin, call_msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value("Access-Control-Allow-Origin") ==
          "http://localhost:5173");
    CHECK(response->get_header_value("Vary") == "Origin");

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: a request without an Origin still works",
          "[mcp][http][security]") {
    // curl, the CLI and every native MCP client send no Origin. Origin
    // validation must not touch them.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 20);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};
    auto response = client.Post("/mcp", call_msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    // Nothing to echo, so no CORS header is emitted at all.
    CHECK(response->get_header_value("Access-Control-Allow-Origin").empty());

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: a cross-origin browser request is refused with 403",
          "[mcp][http][security]") {
    // The confused-deputy case: a page the developer happens to visit posting
    // adt_write_source at their SAP system.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 21);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    const httplib::Headers evil{{"Origin", "https://evil.example"}};
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};

    auto response = client.Post("/mcp", evil, call_msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 403);
    CHECK(response->get_header_value("Access-Control-Allow-Origin").empty());
    // The tool must not have run.
    CHECK(response->body.find("hi") == std::string::npos);

    auto preflight = client.Options("/mcp", evil);
    REQUIRE(preflight != nullptr);
    CHECK(preflight->status == 403);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: --cors-origin allows a named origin",
          "[mcp][http][security]") {
    HttpSecurityOptions security;
    security.allowed_origins = {"https://catalog.example"};
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/false, security);
    auto port = static_cast<uint16_t>(TestPort() + 22);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};
    auto allowed = client.Post("/mcp", {{"Origin", "https://catalog.example"}},
                               call_msg.dump(), "application/json");
    REQUIRE(allowed != nullptr);
    CHECK(allowed->status == 200);
    CHECK(allowed->get_header_value("Access-Control-Allow-Origin") ==
          "https://catalog.example");

    auto refused = client.Post("/mcp", {{"Origin", "https://other.example"}},
                               call_msg.dump(), "application/json");
    REQUIRE(refused != nullptr);
    CHECK(refused->status == 403);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: --cors-origin '*' restores the old wildcard",
          "[mcp][http][security]") {
    HttpSecurityOptions security;
    security.allowed_origins = {"*"};
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/false, security);
    auto port = static_cast<uint16_t>(TestPort() + 23);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};
    auto response = client.Post("/mcp", {{"Origin", "https://anything.example"}},
                                call_msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value("Access-Control-Allow-Origin") == "*");

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: a bearer token gates /mcp but not /healthz",
          "[mcp][http][security]") {
    HttpSecurityOptions security;
    security.auth_token = "s3cret";
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/false, security);
    auto port = static_cast<uint16_t>(TestPort() + 24);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json call_msg = {{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "tools/call"},
                               {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}};

    auto without = client.Post("/mcp", call_msg.dump(), "application/json");
    REQUIRE(without != nullptr);
    CHECK(without->status == 401);
    CHECK(without->get_header_value("WWW-Authenticate") == "Bearer");
    CHECK(without->body.find("hi") == std::string::npos);  // tool did not run

    auto wrong = client.Post("/mcp", {{"Authorization", "Bearer nope"}},
                             call_msg.dump(), "application/json");
    REQUIRE(wrong != nullptr);
    CHECK(wrong->status == 401);

    auto right = client.Post("/mcp", {{"Authorization", "Bearer s3cret"}},
                             call_msg.dump(), "application/json");
    REQUIRE(right != nullptr);
    CHECK(right->status == 200);
    CHECK(right->body.find("hi") != std::string::npos);

    // Liveness probes must not need the token.
    auto health = client.Get("/healthz");
    REQUIRE(health != nullptr);
    CHECK(health->status == 200);

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

// ===========================================================================
// HTTP status conformance
// ===========================================================================

TEST_CASE("McpHttpServer: a notification is answered with 202, not 204",
          "[mcp][http][protocol]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 30);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json notification = {{"jsonrpc", "2.0"},
                                   {"method", "notifications/initialized"}};
    auto response = client.Post("/mcp", notification.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 202);
    CHECK(response->body.empty());

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: an unimplemented method is 404 with -32601",
          "[mcp][http][protocol]") {
    // The pairing is what lets a client tell a modern server from a legacy
    // HTTP+SSE one while probing.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 31);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json msg = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "no/such/method"}};
    auto response = client.Post("/mcp", msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 404);
    auto body = nlohmann::json::parse(response->body);
    CHECK(body["error"]["code"] == -32601);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: an unknown tool is 200 with -32602",
          "[mcp][http][protocol]") {
    // An unknown *tool* is a bad parameter to an implemented method, so the
    // HTTP status stays 200 — only the JSON-RPC error distinguishes it.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 32);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    nlohmann::json msg = {{"jsonrpc", "2.0"},
                          {"id", 9},
                          {"method", "tools/call"},
                          {"params", {{"name", "nope"}}}};
    auto response = client.Post("/mcp", msg.dump(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    auto body = nlohmann::json::parse(response->body);
    CHECK(body["error"]["code"] == -32602);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: GET and DELETE on /mcp are 405",
          "[mcp][http][protocol]") {
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 33);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    auto get = client.Get("/mcp");
    REQUIRE(get != nullptr);
    CHECK(get->status == 405);
    CHECK(get->get_header_value("Allow") == "POST, OPTIONS");

    auto del = client.Delete("/mcp");
    REQUIRE(del != nullptr);
    CHECK(del->status == 405);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: the web UI catch-all does not swallow GET /mcp",
          "[mcp][http][protocol]") {
    // The route-ordering trap: Get(".*") serving the SPA would otherwise
    // answer index.html here instead of 405.
    McpHttpServer server(MakeEchoRegistry(), /*serve_webui=*/true);
    auto port = static_cast<uint16_t>(TestPort() + 34);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    auto get = client.Get("/mcp");
    REQUIRE(get != nullptr);
    CHECK(get->status == 405);

    server.Stop();
    server_thread.join();
}

// ===========================================================================
// Host validation (DNS rebinding)
// ===========================================================================

namespace {

// The message body used by the Host tests below — a tool call, so a request
// that is wrongly let through is visible as an executed tool.
std::string EchoCall() {
    return nlohmann::json{{"jsonrpc", "2.0"},
                          {"id", 1},
                          {"method", "tools/call"},
                          {"params", {{"name", "echo"},
                                      {"arguments", {{"text", "hi"}}}}}}
        .dump();
}

} // anonymous namespace

TEST_CASE("McpHttpServer: a rebound Host is refused with no flags at all",
          "[mcp][http][security]") {
    // The shape #50 is about, against the default configuration — which is
    // the only configuration most people run. evil.example points
    // rebind.evil.example at 127.0.0.1; the browser then believes it is
    // calling its own origin, so it sends an Origin the Origin check cannot
    // fault and can read the answers. Host is the half the attacker cannot
    // launder.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 40);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    const httplib::Headers rebound{
        {"Host", "rebind.evil.example"},
        {"Origin", "http://rebind.evil.example"}};
    auto refused = client.Post("/mcp", rebound, EchoCall(), "application/json");
    REQUIRE(refused != nullptr);
    CHECK(refused->status == 403);
    CHECK(refused->get_header_value("Access-Control-Allow-Origin").empty());
    CHECK(refused->body.find("hi") == std::string::npos);  // the tool did not run
    // The refusal has to be actionable: the operator reads it in a browser
    // console and needs to know which name to allow.
    CHECK(refused->body.find("rebind.evil.example") != std::string::npos);
    CHECK(refused->body.find("--allowed-hosts") != std::string::npos);

    // A browser that sends no Origin still announces itself with Sec-Fetch-*
    // (the SPA's GET routes), and is held to the same rule.
    const httplib::Headers sec_fetch{{"Host", "rebind.evil.example"},
                                     {"Sec-Fetch-Site", "same-origin"}};
    auto also_refused =
        client.Post("/mcp", sec_fetch, EchoCall(), "application/json");
    REQUIRE(also_refused != nullptr);
    CHECK(also_refused->status == 403);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: a non-browser client may use any hostname",
          "[mcp][http][security]") {
    // curl, native MCP clients, a server-to-server caller behind a proxy:
    // no Origin, no Sec-Fetch-*, and no way to carry a rebinding attack —
    // rebinding needs a browser. Refusing these would cost real deployments
    // and buy nothing.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 43);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    const httplib::Headers named{{"Host", "mcp.internal.example"}};
    auto response = client.Post("/mcp", named, EchoCall(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: loopback and IP literals need no configuration",
          "[mcp][http][security]") {
    // The two cases that must never need a flag: the developer on localhost,
    // and `--host 0.0.0.0` reached at a LAN address. An IP literal has no DNS
    // name to rebind.
    McpHttpServer server(MakeEchoRegistry());
    auto port = static_cast<uint16_t>(TestPort() + 44);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    for (const auto& host : {"127.0.0.1", "localhost", "192.168.1.5"}) {
        const httplib::Headers headers{
            {"Host", host}, {"Origin", std::string("http://") + host}};
        auto response =
            client.Post("/mcp", headers, EchoCall(), "application/json");
        REQUIRE(response != nullptr);
        INFO("Host: " << host);
        CHECK(response->status == 200);
    }

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: --allowed-hosts names the hosts that are allowed",
          "[mcp][http][security]") {
    // The DNS-rebinding shape: evil.example points rebind.evil.example at
    // 127.0.0.1, so the browser calls this server believing it is same-origin
    // and sends an Origin the Origin check cannot fault. Host is the half the
    // attacker cannot launder.
    HttpSecurityOptions security;
    security.allowed_hosts = {"mcp.internal.example"};

    McpHttpServer server(MakeEchoRegistry(), false, security);
    auto port = static_cast<uint16_t>(TestPort() + 41);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);

    const httplib::Headers rebound{
        {"Host", "rebind.evil.example"},
        {"Origin", "http://rebind.evil.example"}};
    auto refused = client.Post("/mcp", rebound, EchoCall(), "application/json");
    REQUIRE(refused != nullptr);
    CHECK(refused->status == 403);
    CHECK(refused->get_header_value("Access-Control-Allow-Origin").empty());
    CHECK(refused->body.find("hi") == std::string::npos);  // the tool did not run

    // The same request without browser markers is a native client, which
    // cannot be carrying a rebinding attack, and is served.
    const httplib::Headers rebound_no_origin{{"Host", "rebind.evil.example"}};
    auto served =
        client.Post("/mcp", rebound_no_origin, EchoCall(), "application/json");
    REQUIRE(served != nullptr);
    CHECK(served->status == 200);

    // Loopback and the configured name still work.
    const httplib::Headers loopback{{"Host", "127.0.0.1"}};
    auto allowed = client.Post("/mcp", loopback, EchoCall(), "application/json");
    REQUIRE(allowed != nullptr);
    CHECK(allowed->status == 200);

    const httplib::Headers configured{{"Host", "mcp.internal.example"}};
    auto named = client.Post("/mcp", configured, EchoCall(), "application/json");
    REQUIRE(named != nullptr);
    CHECK(named->status == 200);

    server.Stop();
    server_thread.join();
}

TEST_CASE("McpHttpServer: enforcing hosts leaves the web UI's own origin alone",
          "[mcp][http][security]") {
    // The embedded UI is reached at whatever address the browser used, and
    // its POSTs are same-origin. Enforcement must not cost it anything.
    HttpSecurityOptions security;
    security.allowed_hosts = {"catalog.internal"};

    McpHttpServer server(MakeEchoRegistry(), false, security);
    auto port = static_cast<uint16_t>(TestPort() + 42);

    std::thread server_thread([&] { (void)server.Run("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", port);
    const httplib::Headers same_origin{{"Host", "catalog.internal"},
                                       {"Origin", "http://catalog.internal"}};
    auto response = client.Post("/mcp", same_origin, EchoCall(), "application/json");
    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value("Access-Control-Allow-Origin") ==
          "http://catalog.internal");

    server.Stop();
    server_thread.join();
}
