#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/config/app_config.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <fstream>
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

TEST_CASE("AdtSession: GET sends the configured logon language (#26)",
          "[adt][session][live]") {
    httplib::Server svr;

    std::string received_accept_lang;
    svr.Get("/sap/bc/adt/test", [&](const httplib::Request& req,
                                     httplib::Response& res) {
        if (req.has_header("Accept-Language")) {
            received_accept_lang = req.get_header_value("Accept-Language");
        }
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);

    AdtSessionOptions opts;
    opts.language = SapLanguage::Create("DE").Value();
    auto session = MakeTestSession(server.Port(), opts);

    auto result = session->Get("/sap/bc/adt/test");
    REQUIRE(result.IsOk());
    // SAP logon language is selected via the Accept-Language header; the ISO
    // code is sent lower-cased per HTTP convention (e.g. "de", not "DE").
    CHECK(received_accept_lang == "de");
}

TEST_CASE("AdtSession: BW requests inject bwmt-level by default", "[adt][session][live]") {
    httplib::Server svr;
    std::string received_bwmt_level;

    svr.Get("/sap/bw/modeling/discovery", [&](const httplib::Request& req,
                                               httplib::Response& res) {
        if (req.has_header("bwmt-level")) {
            received_bwmt_level = req.get_header_value("bwmt-level");
        }
        res.set_content("<discovery/>", "application/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Get("/sap/bw/modeling/discovery");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(received_bwmt_level == "50");
}

TEST_CASE("AdtSession: caller bwmt-level overrides default", "[adt][session][live]") {
    httplib::Server svr;
    std::string received_bwmt_level;

    svr.Get("/sap/bw/modeling/discovery", [&](const httplib::Request& req,
                                               httplib::Response& res) {
        if (req.has_header("bwmt-level")) {
            received_bwmt_level = req.get_header_value("bwmt-level");
        }
        res.set_content("<discovery/>", "application/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    HttpHeaders headers;
    headers["bwmt-level"] = "77";
    auto result = session->Get("/sap/bw/modeling/discovery", headers);
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(received_bwmt_level == "77");
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
        // First POST with old token -> 403
        if (count == 1) {
            res.status = 403;
            res.set_content("CSRF validation failed", "text/plain");
        } else {
            // Retry with new token -> 201
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

TEST_CASE("AdtSession: POST 403 with SAP error body does NOT retry", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> post_count{0};

    svr.Get("/sap/bw/modeling/discovery", [&](const httplib::Request&,
                                               httplib::Response& res) {
        ++discovery_count;
        res.set_header("x-csrf-token", "token-123");
        res.set_content("<discovery/>", "text/xml");
    });

    // BW lock conflict: SAP returns 403 with XML error body
    svr.Post("/sap/bw/modeling/iobj/0calday", [&](const httplib::Request&,
                                                    httplib::Response& res) {
        ++post_count;
        res.status = 403;
        res.set_content(
            R"(<?xml version="1.0" encoding="utf-8"?>)"
            R"(<exc:exception xmlns:exc="http://www.sap.com/abap/exception">)"
            R"(<exc:message>InfoObject 0CALDAY is locked by user DEVELOPER</exc:message>)"
            R"(</exc:exception>)",
            "application/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Post("/sap/bw/modeling/iobj/0calday", "", "application/xml");
    REQUIRE(result.IsOk());
    // Should return the 403 directly — no retry
    CHECK(result.Value().status_code == 403);
    CHECK(result.Value().body.find("locked by user DEVELOPER") != std::string::npos);
    // Only one CSRF fetch (initial) — no re-fetch on this 403
    CHECK(discovery_count.load() == 1);
    // Only one POST attempt — no retry
    CHECK(post_count.load() == 1);
}

// Regression: a plain-text 403 (genuine CSRF expiry) that incidentally mentions
// the literal text "<message>" must still trigger a CSRF retry. The prior
// substring-only test classified any body containing "<message" as a SAP app
// error, which would block CSRF retries in this case.
TEST_CASE("AdtSession: POST 403 with plain-text body mentioning <message> still retries",
          "[adt][session][live][hardening]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> post_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        int count = ++discovery_count;
        std::string token = (count == 1) ? "old-tok" : "new-tok";
        res.set_header("x-csrf-token", token);
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/packages", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        int count = ++post_count;
        if (count == 1) {
            res.status = 403;
            // Plain-text body that *mentions* "<message>" — must not be
            // mistaken for an XML <exc:message> SAP application error.
            res.set_content(
                "CSRF token validation failed — see the <message> element "
                "in /sap/bc/adt/discovery for a fresh token.",
                "text/plain");
        } else {
            // Verify the retry carried the refreshed token.
            REQUIRE(req.has_header("x-csrf-token"));
            CHECK(req.get_header_value("x-csrf-token") == "new-tok");
            res.status = 201;
            res.set_content("<created/>", "text/xml");
        }
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Post("/sap/bc/adt/packages", "<xml/>",
                                  "application/xml");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 201);
    CHECK(discovery_count.load() == 2);  // initial + re-fetch
    CHECK(post_count.load() == 2);        // failed + retry
}

TEST_CASE("AdtSession: GET 403 with SAP error body does NOT retry", "[adt][session][live]") {
    httplib::Server svr;

    std::atomic<int> discovery_count{0};
    std::atomic<int> get_count{0};

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request&,
                                          httplib::Response& res) {
        ++discovery_count;
        res.set_header("x-csrf-token", "token-123");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Get("/sap/bc/adt/some/resource", [&](const httplib::Request&,
                                               httplib::Response& res) {
        ++get_count;
        res.status = 403;
        res.set_content(
            R"(<error><message>Access denied for resource</message></error>)",
            "application/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    auto result = session->Get("/sap/bc/adt/some/resource");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 403);
    CHECK(get_count.load() == 1);  // No retry
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

TEST_CASE("AdtSession: stateful cookie header prioritizes sap-contextid", "[adt][session][live]") {
    httplib::Server svr;

    std::string received_cookie;
    std::string csrf_fetch_path;

    // In stateful mode CSRF is always fetched from /sap/bc/adt/discovery.
    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        if (req.has_header("x-csrf-token") &&
            req.get_header_value("x-csrf-token") == "fetch") {
            csrf_fetch_path = req.path;
        }
        res.set_header("x-csrf-token", "tok");
        res.set_header("set-cookie",
                       "sap-contextid=SID%3ATEST%3A123; path=/sap/bc/adt");
        res.set_header("set-cookie",
                       "sap-usercontext=sap-client=001; path=/");
        // Intentionally large to reproduce ordering sensitivity in some SAP stacks.
        res.set_header("set-cookie",
                       "MYSAPSSO2=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz; path=/");
        res.set_header("set-cookie",
                       "SAP_SESSIONID_A4H_001=abc123%3d; path=/");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/oo/classes/zfoo", [&](const httplib::Request& req,
                                                  httplib::Response& res) {
        if (req.has_header("Cookie")) {
            received_cookie = req.get_header_value("Cookie");
        }
        res.status = 200;
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());
    session->SetStateful(true);

    auto result = session->Post(
        "/sap/bc/adt/oo/classes/zfoo",
        "",
        "application/xml");

    REQUIRE(result.IsOk());
    CHECK(csrf_fetch_path == "/sap/bc/adt/discovery");
    REQUIRE_FALSE(received_cookie.empty());
    CHECK(received_cookie.rfind("sap-contextid=", 0) == 0);
}

TEST_CASE("AdtSession: stateful CSRF fetch always uses discovery endpoint", "[adt][session][live]") {
    httplib::Server svr;

    int target_fetch_count = 0;
    int discovery_fetch_count = 0;

    svr.Get("/sap/bc/adt/oo/classes/zfoo", [&](const httplib::Request& req,
                                                httplib::Response& res) {
        if (req.has_header("x-csrf-token") &&
            req.get_header_value("x-csrf-token") == "fetch") {
            ++target_fetch_count;
        }
        res.set_content("<class/>", "text/xml");
    });

    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        if (req.has_header("x-csrf-token") &&
            req.get_header_value("x-csrf-token") == "fetch") {
            ++discovery_fetch_count;
        }
        res.set_header("x-csrf-token", "tok");
        res.set_header("set-cookie",
                       "sap-contextid=SID%3ATEST%3A123; path=/sap/bc/adt");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/oo/classes/zfoo", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        res.status = 200;
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());
    session->SetStateful(true);

    auto result = session->Post("/sap/bc/adt/oo/classes/zfoo", "", "application/xml");

    REQUIRE(result.IsOk());
    CHECK(target_fetch_count == 0);
    CHECK(discovery_fetch_count == 1);
}

TEST_CASE("AdtSession: ResetStatefulSession clears cookies and CSRF token", "[adt][session][live]") {
    httplib::Server svr;

    // First request: return CSRF token and a context cookie.
    int csrf_fetch_count = 0;
    svr.Get("/sap/bc/adt/discovery", [&](const httplib::Request& req,
                                          httplib::Response& res) {
        if (req.has_header("x-csrf-token") &&
            req.get_header_value("x-csrf-token") == "fetch") {
            ++csrf_fetch_count;
        }
        res.set_header("x-csrf-token", "tok-from-server");
        res.set_header("set-cookie",
                       "sap-contextid=CTX%3A123; path=/sap/bc/adt");
        res.set_content("<discovery/>", "text/xml");
    });

    svr.Post("/sap/bc/adt/test", [&](const httplib::Request&,
                                      httplib::Response& res) {
        res.status = 200;
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());
    session->SetStateful(true);

    // Warm up: POST triggers CSRF fetch which sets token and cookie.
    auto r1 = session->Post("/sap/bc/adt/test", "", "application/xml");
    REQUIRE(r1.IsOk());
    CHECK(csrf_fetch_count == 1);

    // After reset, the saved session is empty (no csrf_token, no cookies).
    session->ResetStatefulSession();

    // SaveSession into a temp file and verify no csrf_token or cookies are written.
    const std::string tmp_path =
        (std::filesystem::temp_directory_path() / "erpl_adt_reset_session.json").string();
    auto save_result = session->SaveSession(tmp_path);
    REQUIRE(save_result.IsOk());

    std::ifstream ifs(tmp_path);
    REQUIRE(ifs.is_open());
    nlohmann::json j;
    ifs >> j;

    CHECK_FALSE(j.contains("csrf_token"));
    CHECK(j.contains("context_id"));
    CHECK(j["context_id"].get<std::string>().empty());
    CHECK(j.contains("cookies"));
    CHECK(j["cookies"].empty());
}

// ===========================================================================
// Read timeout: the default, and how exceeding it is reported (#42)
// ===========================================================================

TEST_CASE("AdtSessionOptions: the default read timeout is the documented one",
          "[adt][session]") {
    // These two drifted apart once already: the session defaulted to 120s
    // while AppConfig documented 600, and because both CLI entry points only
    // assign read_timeout when --timeout is passed, 120s was what every call
    // actually got. A classrun longer than that failed while the ABAP ran on
    // to completion server-side.
    CHECK(AdtSessionOptions{}.read_timeout ==
          std::chrono::seconds(kDefaultReadTimeoutSeconds));
    CHECK(AppConfig{}.timeout_seconds == kDefaultReadTimeoutSeconds);
}

TEST_CASE("AdtSession: a read timeout is reported as a timeout, naming --timeout",
          "[adt][session][live]") {
    // "HTTP request failed: Failed to read connection" said nothing about a
    // timeout, named no flag, and was followed by an invitation to file a bug
    // — for a limit the caller can simply raise.
    httplib::Server svr;
    svr.Get("/sap/bc/adt/slow", [&](const httplib::Request&,
                                    httplib::Response& res) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        res.set_content("<ok/>", "text/xml");
    });

    LocalServer server(svr);
    auto client = SapClient::Create("001");
    REQUIRE(client.IsOk());
    AdtSessionOptions opts;
    opts.connect_timeout = std::chrono::seconds{5};
    opts.read_timeout = std::chrono::seconds{1};
    AdtSession session("127.0.0.1", static_cast<uint16_t>(server.Port()), false,
                       "testuser", "testpass", client.Value(), opts);

    auto result = session.Get("/sap/bc/adt/slow");
    REQUIRE(result.IsErr());
    const auto& error = result.Error();
    CHECK(error.category == ErrorCategory::Timeout);
    CHECK(error.message.find("timed out after 1s") != std::string::npos);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("--timeout") != std::string::npos);
}

TEST_CASE("AdtSession: an unreachable server is not blamed on the read timeout",
          "[adt][session][live]") {
    // Whether a closed port is refused (Linux) or quietly dropped until the
    // connect timeout expires (Windows), the request never reached the
    // server — so the read timeout is not the number that ran out, and
    // --timeout is not the flag that would have helped.
    auto client = SapClient::Create("001");
    REQUIRE(client.IsOk());
    AdtSessionOptions opts;
    opts.connect_timeout = std::chrono::seconds{2};
    opts.read_timeout = std::chrono::seconds{2};
    // Port 1 on loopback: nothing listens, so this is refused, not slow.
    AdtSession session("127.0.0.1", 1, false, "testuser", "testpass",
                       client.Value(), opts);

    auto result = session.Get("/sap/bc/adt/test");
    REQUIRE(result.IsErr());
    const auto& error = result.Error();
    CHECK(error.message.find("request timed out after") == std::string::npos);
    if (error.hint.has_value()) {
        CHECK(error.hint->find("--timeout") == std::string::npos);
    }
}

// ===========================================================================
// Stateful sessions are closed on the server when leaving stateful mode.
//
// Forgetting the sap-contextid only on the client leaves the ABAP session
// open until rdisp/plugin_auto_logout (30 min). Under load (CI, parallel runs
// as one user) the per-user session limit then fills up and every new
// stateful request fails with "400 Session not found" (observed on A4H).
// ===========================================================================

TEST_CASE("AdtSession: leaving stateful mode closes the server session",
          "[adt][session][stateful]") {
    httplib::Server svr;
    std::mutex mu;
    std::vector<std::string> close_cookies;
    std::vector<std::string> close_session_types;
    std::vector<std::string> later_cookies;
    bool closed = false;

    svr.Get(".*", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mu);
        if (req.has_param("sap-sessioncmd") && req.get_param_value("sap-sessioncmd") == "close") {
            close_cookies.push_back(req.get_header_value("Cookie"));
            close_session_types.push_back(req.get_header_value("X-sap-adt-sessiontype"));
            closed = true;
        } else if (closed) {
            later_cookies.push_back(req.get_header_value("Cookie"));
        } else if (req.get_header_value("X-sap-adt-sessiontype") == "stateful") {
            res.set_header("Set-Cookie", "sap-contextid=CTX-1; path=/sap/bc/adt");
        }
        res.set_content("<ok/>", "application/xml");
    });
    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    session->SetStateful(true);
    REQUIRE(session->Get("/sap/bc/adt/oo/classes/zcl_a").IsOk());
    session->SetStateful(false);

    {
        std::lock_guard<std::mutex> lock(mu);
        REQUIRE(close_cookies.size() == 1);
        // The close is sent inside the session it ends.
        CHECK(close_cookies[0].find("sap-contextid=CTX-1") != std::string::npos);
        CHECK(close_session_types[0] == "stateful");
    }

    // Afterwards the closed context is no longer sent.
    REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
    std::lock_guard<std::mutex> lock(mu);
    REQUIRE(later_cookies.size() == 1);
    CHECK(later_cookies[0].find("sap-contextid") == std::string::npos);
}

TEST_CASE("AdtSession: no close request when no stateful session was opened",
          "[adt][session][stateful]") {
    httplib::Server svr;
    std::atomic<int> closes{0};
    svr.Get(".*", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("sap-sessioncmd")) {
            ++closes;
        }
        res.set_content("<ok/>", "application/xml");
    });
    LocalServer server(svr);
    auto session = MakeTestSession(server.Port());

    // Stateless use, then switching "off" again: nothing to close.
    REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
    session->SetStateful(false);
    // Stateful mode without any server context yet: nothing to close either.
    session->SetStateful(true);
    session->SetStateful(false);

    CHECK(closes.load() == 0);
}

// ===========================================================================
// Ending the logon (security) session
//
// Every AdtSession logs on once and keeps SAP's SAP_SESSIONID_<SID>_<CLIENT>
// cookie. Left alone, SAP keeps that logon until rdisp/plugin_auto_logout
// (30 min); a CI job or test suite running a few hundred erpl-adt commands
// as one user then exhausts the per-user logon limit and every request is
// answered "429 Too Many Logons" (observed on A4H). A session that nobody
// will reuse logs off when it is destroyed.
// ===========================================================================

namespace {

struct LogoffServer {
    httplib::Server svr;
    std::mutex mu;
    std::vector<std::string> logoff_cookies;
    int requests = 0;

    explicit LogoffServer(bool set_logon_cookie) {
        svr.Get(".*", [this, set_logon_cookie](const httplib::Request& req,
                                                httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mu);
            ++requests;
            if (req.has_param("sap-sessioncmd") &&
                req.get_param_value("sap-sessioncmd") == "logoff") {
                logoff_cookies.push_back(req.get_header_value("Cookie"));
            } else if (set_logon_cookie) {
                res.set_header("Set-Cookie", "SAP_SESSIONID_A4H_001=LOGON-1; path=/");
            }
            res.set_content("<ok/>", "application/xml");
        });
    }
};

}  // namespace

TEST_CASE("AdtSession: destroying the session logs off its logon session",
          "[adt][session][logoff]") {
    LogoffServer s(true);
    LocalServer server(s.svr);
    {
        auto session = MakeTestSession(server.Port());
        REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
    }
    std::lock_guard<std::mutex> lock(s.mu);
    REQUIRE(s.logoff_cookies.size() == 1);
    // The logoff is sent inside the logon it ends.
    CHECK(s.logoff_cookies[0].find("SAP_SESSIONID_A4H_001=LOGON-1") != std::string::npos);
}

TEST_CASE("AdtSession: no logoff without a logon session",
          "[adt][session][logoff]") {
    LogoffServer s(false);
    LocalServer server(s.svr);
    {
        auto session = MakeTestSession(server.Port());
        REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
    }
    {
        // Never talked to the server: nothing to end, no request at all.
        auto unused = MakeTestSession(server.Port());
    }
    std::lock_guard<std::mutex> lock(s.mu);
    CHECK(s.logoff_cookies.empty());
    CHECK(s.requests == 1);
}

TEST_CASE("AdtSession: a session saved to or loaded from a file stays logged on",
          "[adt][session][logoff]") {
    LogoffServer s(true);
    LocalServer server(s.svr);
    const auto path = (std::filesystem::temp_directory_path() /
                       ("erpl_adt_logoff_" + std::to_string(server.Port()) + ".json"))
                          .string();
    {
        // lock --session-file: the next command continues this logon.
        auto session = MakeTestSession(server.Port());
        REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
        REQUIRE(session->SaveSession(path).IsOk());
    }
    {
        // write --session-file: still the same logon, still needed by unlock.
        auto session = MakeTestSession(server.Port());
        REQUIRE(session->LoadSession(path).IsOk());
        REQUIRE(session->Get("/sap/bc/adt/discovery").IsOk());
    }
    std::filesystem::remove(path);
    std::lock_guard<std::mutex> lock(s.mu);
    CHECK(s.logoff_cookies.empty());
}
