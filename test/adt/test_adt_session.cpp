#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/adt_session.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// Helper: find a free port and spin up a local httplib::Server for tests
// that exercise real AdtSession logic (CSRF, retry, polling).
// ===========================================================================
namespace {

// A tiny RAII wrapper that starts an httplib::Server on a background thread
// and stops it on destruction.
class LocalServer {
public:
    explicit LocalServer(httplib::Server& svr) : svr_(svr) {
        // Let the OS pick a free port by binding to 0.
        port_ = svr_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { svr_.listen_after_bind(); });
        // Wait for server to be ready.
        svr_.wait_until_ready();
    }

    ~LocalServer() {
        svr_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] int Port() const noexcept { return port_; }

    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

private:
    httplib::Server& svr_;
    int port_ = 0;
    std::thread thread_;
};

// Create an AdtSession connected to a local test server.
std::unique_ptr<AdtSession> MakeTestSession(int port,
                                            AdtSessionOptions opts = {}) {
    auto client = SapClient::Create("001");
    REQUIRE(client.IsOk());
    opts.connect_timeout = std::chrono::seconds{5};
    opts.read_timeout = std::chrono::seconds{5};
    return std::make_unique<AdtSession>(
        "127.0.0.1", static_cast<uint16_t>(port),
        false, "testuser", "testpass", client.Value(), opts);
}

} // anonymous namespace

// ===========================================================================
// Mock-based tests: verify how ADT operations interact with IAdtSession
// ===========================================================================

TEST_CASE("Mock: SAP headers can be verified on GET calls", "[adt][session]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<ok/>"}));

    // Simulate what an ADT operation module would do: call Get with SAP headers
    HttpHeaders sap_headers = {
        {"sap-client", "001"},
        {"Accept-Language", "en"},
    };
    auto result = mock.Get("/sap/bc/adt/packages/ZTEST", sap_headers);

    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].headers.at("sap-client") == "001");
    CHECK(mock.GetCalls()[0].headers.at("Accept-Language") == "en");
}

TEST_CASE("Mock: CSRF token fetch + POST flow", "[adt][session]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-abc")));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "<created/>"}));

    // Simulate: fetch token, then POST
    auto token = mock.FetchCsrfToken();
    REQUIRE(token.IsOk());
    CHECK(token.Value() == "csrf-abc");

    auto result = mock.Post(
        "/sap/bc/adt/packages",
        "<xml/>",
        "application/xml",
        {{"x-csrf-token", token.Value()}});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 201);
    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].headers.at("x-csrf-token") == "csrf-abc");
}

TEST_CASE("Mock: 403 retry pattern", "[adt][session]") {
    // Simulates the pattern: call -> 403 -> re-fetch CSRF -> retry -> success
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({403, {}, "CSRF required"}));
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("new-token")));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "<ok/>"}));

    // First attempt
    auto r1 = mock.Post("/sap/bc/adt/packages", "<xml/>", "application/xml", {});
    REQUIRE(r1.IsOk());
    CHECK(r1.Value().status_code == 403);

    // Re-fetch token
    auto token = mock.FetchCsrfToken();
    REQUIRE(token.IsOk());

    // Retry
    auto r2 = mock.Post(
        "/sap/bc/adt/packages", "<xml/>", "application/xml",
        {{"x-csrf-token", token.Value()}});
    REQUIRE(r2.IsOk());
    CHECK(r2.Value().status_code == 201);

    CHECK(mock.PostCallCount() == 2);
    CHECK(mock.CsrfCallCount() == 1);
}

TEST_CASE("Mock: PollUntilComplete - running then completed", "[adt][session]") {
    MockAdtSession mock;
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Running, "<running/>", std::chrono::milliseconds{500}}));
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Running, "<still-running/>", std::chrono::milliseconds{1000}}));
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<done/>", std::chrono::milliseconds{1500}}));

    // Simulate polling loop
    PollResult final_result;
    for (int i = 0; i < 3; ++i) {
        auto r = mock.PollUntilComplete("/poll/loc", std::chrono::seconds{60});
        REQUIRE(r.IsOk());
        final_result = r.Value();
        if (final_result.status == PollStatus::Completed) {
            break;
        }
    }

    CHECK(final_result.status == PollStatus::Completed);
    CHECK(final_result.body == "<done/>");
    CHECK(mock.PollCallCount() == 3);
}

TEST_CASE("Mock: PollUntilComplete - running then failed", "[adt][session]") {
    MockAdtSession mock;
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Running, "<running/>", std::chrono::milliseconds{500}}));
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Failed, "<error/>", std::chrono::milliseconds{1000}}));

    auto r1 = mock.PollUntilComplete("/poll/loc", std::chrono::seconds{60});
    REQUIRE(r1.IsOk());
    CHECK(r1.Value().status == PollStatus::Running);

    auto r2 = mock.PollUntilComplete("/poll/loc", std::chrono::seconds{60});
    REQUIRE(r2.IsOk());
    CHECK(r2.Value().status == PollStatus::Failed);
    CHECK(r2.Value().body == "<error/>");
}

TEST_CASE("Mock: PollUntilComplete - timeout error", "[adt][session]") {
    MockAdtSession mock;
    mock.EnqueuePoll(Result<PollResult, Error>::Err(
        Error{"PollUntilComplete", "/poll/loc", std::nullopt,
              "Timeout exceeded", std::nullopt}));

    auto result = mock.PollUntilComplete("/poll/loc", std::chrono::seconds{5});
    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "Timeout exceeded");
}

TEST_CASE("Mock: cookie forwarding verification", "[adt][session]") {
    // Verify that the mock correctly records sequential calls,
    // which is what real cookie forwarding would look like.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {{"set-cookie", "sap-contextid=ABC123"}}, "<discovery/>"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<packages/>"}));

    auto r1 = mock.Get("/sap/bc/adt/discovery",
                        {{"x-csrf-token", "fetch"}});
    REQUIRE(r1.IsOk());

    // Second call — in real session, cookies from r1 would be forwarded
    auto r2 = mock.Get("/sap/bc/adt/packages/ZTEST", {});
    REQUIRE(r2.IsOk());

    CHECK(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/discovery");
    CHECK(mock.GetCalls()[1].path == "/sap/bc/adt/packages/ZTEST");
}

// ===========================================================================
// Real AdtSession tests using a local httplib::Server
// ===========================================================================

TEST_CASE("AdtSession: GET sends SAP headers", "[adt][session][live]") {
    httplib::Server svr;

    std::string received_sap_client;
    std::string received_accept_lang;
    std::string received_auth;

    svr.Get("/sap/bc/adt/test", [&](const httplib::Request& req,
                                     httplib::Response& res) {
        if (req.has_header("sap-client")) {
            received_sap_client = req.get_header_value("sap-client");
        }
        if (req.has_header("Accept-Language")) {
            received_accept_lang = req.get_header_value("Accept-Language");
        }
        if (req.has_header("Authorization")) {
            received_auth = req.get_header_value("Authorization");
        }
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Get("/sap/bc/adt/test");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(result.Value().body == "<ok/>");
    CHECK(received_sap_client == "001");
    CHECK(received_accept_lang == "en");
    CHECK_FALSE(received_auth.empty()); // Basic Auth header present
}

TEST_CASE("AdtSession: FetchCsrfToken extracts token from response", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request& req,
                                         httplib::Response& res) {
        // Only return token if requested
        if (req.has_header("x-csrf-token") &&
            req.get_header_value("x-csrf-token") == "fetch") {
            res.set_header("x-csrf-token", "my-csrf-token-123");
        }
        res.set_content("<discovery/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->FetchCsrfToken();
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "my-csrf-token-123");
}

TEST_CASE("AdtSession: FetchCsrfToken fails on non-200", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.status = 401;
        res.set_content("Unauthorized", "text/plain");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->FetchCsrfToken();
    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "FetchCsrfToken");
    CHECK(result.Error().http_status.value() == 401);
}

TEST_CASE("AdtSession: FetchCsrfToken fails when header missing", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        // Return 200 but no x-csrf-token header
        res.set_content("<discovery/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->FetchCsrfToken();
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("No x-csrf-token") != std::string::npos);
}

TEST_CASE("AdtSession: POST auto-fetches CSRF token", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::string received_csrf_on_post;

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        ++discovery_count;
        res.set_header("x-csrf-token", "auto-token-456");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/packages", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        if (req.has_header("x-csrf-token")) {
            received_csrf_on_post = req.get_header_value("x-csrf-token");
        }
        res.status = 201;
        res.set_content("<created/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    // POST without prior FetchCsrfToken — session should auto-fetch
    auto result = session->Post("/sap/bc/adt/packages", "<xml/>", "application/xml");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 201);
    CHECK(received_csrf_on_post == "auto-token-456");
    CHECK(discovery_count.load() == 1);
}

TEST_CASE("AdtSession: POST 403 triggers CSRF re-fetch and retry", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> post_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        int count = ++discovery_count;
        // First fetch returns old token, second returns new
        std::string token = (count == 1) ? "old-token" : "new-token";
        res.set_header("x-csrf-token", token);
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/packages", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        int count = ++post_count;
        std::string token;
        if (req.has_header("x-csrf-token")) {
            token = req.get_header_value("x-csrf-token");
        }
        // First POST with old token → 403
        if (count == 1) {
            res.status = 403;
            res.set_content("CSRF validation failed", "text/plain");
        } else {
            // Retry with new token → 201
            res.status = 201;
            res.set_content("<created/>", "text/xml");
        }
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Post("/sap/bc/adt/packages", "<xml/>", "application/xml");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 201);
    // Should have fetched token twice (initial + re-fetch on 403)
    CHECK(discovery_count.load() == 2);
    // Should have attempted POST twice
    CHECK(post_count.load() == 2);
}

TEST_CASE("AdtSession: DELETE auto-fetches CSRF and retries on 403", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> delete_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        int count = ++discovery_count;
        std::string token = (count == 1) ? "old-token" : "new-token";
        res.set_header("x-csrf-token", token);
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Delete("/sap/bc/adt/abapgit/repos/KEY1", [&](const httplib::Request&,
                                                       httplib::Response& res) {
        int count = ++delete_count;
        if (count == 1) {
            res.status = 403;
            res.set_content("CSRF required", "text/plain");
        } else {
            res.status = 204;
        }
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Delete("/sap/bc/adt/abapgit/repos/KEY1");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 204);
    CHECK(discovery_count.load() == 2);
    CHECK(delete_count.load() == 2);
}

TEST_CASE("AdtSession: GET 403 triggers CSRF re-fetch and retry", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> get_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        ++discovery_count;
        res.set_header("x-csrf-token", "fresh-token");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/sap/bc/adt/packages/ZTEST", [&](const httplib::Request&,
                                               httplib::Response& res) {
        int count = ++get_count;
        if (count == 1) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
        } else {
            res.set_content("<package/>", "text/xml");
        }
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Get("/sap/bc/adt/packages/ZTEST");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(result.Value().body == "<package/>");
    CHECK(discovery_count.load() == 1); // One CSRF fetch on 403
    CHECK(get_count.load() == 2); // Two GET attempts
}

TEST_CASE("AdtSession: PollUntilComplete - 202 then 200", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> poll_count{0};

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.set_header("x-csrf-token", "tok");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/poll/location/123", [&](const httplib::Request&,
                                       httplib::Response& res) {
        int count = ++poll_count;
        if (count <= 2) {
            res.status = 202;
            res.set_content("<running/>", "text/xml");
        } else {
            res.status = 200;
            res.set_content("<completed/>", "text/xml");
        }
    });

    LocalServer server(svr);
    AdtSessionOptions opts;
    opts.poll_interval = std::chrono::seconds{0}; // No delay in tests
    auto session = MakeTestSession(server.Port(), opts);

    auto result = session->PollUntilComplete("/poll/location/123",
                                             std::chrono::seconds{10});
    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Completed);
    CHECK(result.Value().body == "<completed/>");
    CHECK(poll_count.load() == 3);
}

TEST_CASE("AdtSession: PollUntilComplete - failure status", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.set_header("x-csrf-token", "tok");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/poll/fail", [](const httplib::Request&,
                              httplib::Response& res) {
        res.status = 500;
        res.set_content("<error>activation failed</error>", "text/xml");
    });

    LocalServer server(svr);
    AdtSessionOptions opts;
    opts.poll_interval = std::chrono::seconds{0};
    auto session = MakeTestSession(server.Port(), opts);

    auto result = session->PollUntilComplete("/poll/fail",
                                             std::chrono::seconds{10});
    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Failed);
    CHECK(result.Value().body.find("activation failed") != std::string::npos);
}

TEST_CASE("AdtSession: PollUntilComplete - timeout", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.set_header("x-csrf-token", "tok");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/poll/forever", [](const httplib::Request&,
                                 httplib::Response& res) {
        // Always return 202 (never completes)
        res.status = 202;
        res.set_content("<still running/>", "text/xml");
    });

    LocalServer server(svr);
    AdtSessionOptions opts;
    opts.poll_interval = std::chrono::seconds{0};
    auto session = MakeTestSession(server.Port(), opts);

    // Use a very short timeout
    auto result = session->PollUntilComplete("/poll/forever",
                                             std::chrono::seconds{1});
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Timeout);
    CHECK(result.Error().message.find("Timed out waiting for async operation") != std::string::npos);
}

TEST_CASE("AdtSession: response headers are captured", "[adt][session][live]") {
    httplib::Server svr;

    svr.Get("/sap/bc/adt/test", [](const httplib::Request&,
                                    httplib::Response& res) {
        res.set_header("X-Custom-Header", "custom-value");
        res.set_header("Content-Type", "text/xml");
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Get("/sap/bc/adt/test");
    REQUIRE(result.IsOk());
    // httplib lowercases header names in some implementations — check
    // for the value being present somewhere
    bool found = false;
    for (const auto& [key, value] : result.Value().headers) {
        if (value == "custom-value") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("AdtSession: Basic Auth is sent on every request", "[adt][session][live]") {
    httplib::Server svr;

    std::vector<std::string> auth_headers;

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        if (req.has_header("Authorization")) {
            auth_headers.push_back(req.get_header_value("Authorization"));
        }
        res.set_header("x-csrf-token", "tok");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/sap/bc/adt/second", [&](const httplib::Request& req,
                                       httplib::Response& res) {
        if (req.has_header("Authorization")) {
            auth_headers.push_back(req.get_header_value("Authorization"));
        }
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    [[maybe_unused]] auto r1 = session->FetchCsrfToken();
    [[maybe_unused]] auto r2 = session->Get("/sap/bc/adt/second");

    // Both requests should have the same Authorization header
    REQUIRE(auth_headers.size() == 2);
    CHECK(auth_headers[0] == auth_headers[1]);
    CHECK(auth_headers[0].find("Basic") != std::string::npos);
}

TEST_CASE("AdtSession: POST sends body and content-type", "[adt][session][live]") {
    httplib::Server svr;

    std::string received_body;
    std::string received_content_type;

    svr.Get("/sap/bc/adt/discovery", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.set_header("x-csrf-token", "tok");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/packages", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        received_body = req.body;
        if (req.has_header("Content-Type")) {
            received_content_type = req.get_header_value("Content-Type");
        }
        res.status = 201;
        res.set_content("<created/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Post("/sap/bc/adt/packages",
                                "<package>ZTEST</package>",
                                "application/xml");
    REQUIRE(result.IsOk());
    CHECK(received_body == "<package>ZTEST</package>");
    CHECK(received_content_type == "application/xml");
}

TEST_CASE("AdtSession: CSRF token is cached across requests", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        ++discovery_count;
        res.set_header("x-csrf-token", "cached-token");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/packages", [](const httplib::Request&,
                                         httplib::Response& res) {
        res.status = 201;
        res.set_content("<ok/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/abapgit/repos", [](const httplib::Request&,
                                               httplib::Response& res) {
        res.status = 201;
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    // Two POSTs — CSRF should only be fetched once
    [[maybe_unused]] auto r1 = session->Post("/sap/bc/adt/packages", "<xml1/>", "application/xml");
    [[maybe_unused]] auto r2 = session->Post("/sap/bc/adt/abapgit/repos", "<xml2/>", "application/xml");

    CHECK(discovery_count.load() == 1);
}
