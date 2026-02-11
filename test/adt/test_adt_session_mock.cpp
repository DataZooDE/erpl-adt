#include <catch2/catch_test_macros.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <chrono>
#include <memory>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// HttpResponse / PollResult value types
// ===========================================================================

TEST_CASE("HttpResponse default-constructs to zero status", "[adt][types]") {
    HttpResponse resp;
    CHECK(resp.status_code == 0);
    CHECK(resp.headers.empty());
    CHECK(resp.body.empty());
}

TEST_CASE("PollResult default-constructs to Running", "[adt][types]") {
    PollResult pr;
    CHECK(pr.status == PollStatus::Running);
    CHECK(pr.body.empty());
    CHECK(pr.elapsed.count() == 0);
}

// ===========================================================================
// MockAdtSession — Get
// ===========================================================================

TEST_CASE("Mock Get: returns enqueued response", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {{"content-type", "text/xml"}}, "<ok/>"}));

    auto result = mock.Get("/sap/bc/adt/discovery", {{"x-csrf-token", "fetch"}});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(result.Value().body == "<ok/>");
    CHECK(result.Value().headers.at("content-type") == "text/xml");
}

TEST_CASE("Mock Get: records call path and headers", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    HttpHeaders hdrs = {{"x-csrf-token", "fetch"}, {"sap-client", "001"}};
    mock.Get("/sap/bc/adt/packages/ZTEST", hdrs);

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/packages/ZTEST");
    CHECK(mock.GetCalls()[0].headers.at("sap-client") == "001");
}

TEST_CASE("Mock Get: FIFO ordering of multiple responses", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "first"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "second"}));

    auto r1 = mock.Get("/path1", {});
    auto r2 = mock.Get("/path2", {});

    REQUIRE(r1.IsOk());
    CHECK(r1.Value().status_code == 200);
    CHECK(r1.Value().body == "first");

    REQUIRE(r2.IsOk());
    CHECK(r2.Value().status_code == 404);
    CHECK(r2.Value().body == "second");

    CHECK(mock.GetCallCount() == 2);
}

TEST_CASE("Mock Get: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.Get("/any", {});

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "Get");
    CHECK(result.Error().endpoint == "/any");
}

TEST_CASE("Mock Get: can enqueue error results", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/fail", 500, "server error", std::nullopt}));

    auto result = mock.Get("/fail", {});

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
    CHECK(result.Error().message == "server error");
}

// ===========================================================================
// MockAdtSession — Post
// ===========================================================================

TEST_CASE("Mock Post: returns enqueued response and records call", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "<created/>"}));

    auto result = mock.Post(
        "/sap/bc/adt/packages",
        "<package-xml/>",
        "application/xml",
        {{"x-csrf-token", "abc123"}});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 201);

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bc/adt/packages");
    CHECK(mock.PostCalls()[0].body == "<package-xml/>");
    CHECK(mock.PostCalls()[0].content_type == "application/xml");
    CHECK(mock.PostCalls()[0].headers.at("x-csrf-token") == "abc123");
}

TEST_CASE("Mock Post: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.Post("/any", "body", "text/plain", {});

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "Post");
}

// ===========================================================================
// MockAdtSession — Delete
// ===========================================================================

TEST_CASE("Mock Delete: returns enqueued response and records call", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = mock.Delete("/sap/bc/adt/abapgit/repos/KEY1", {});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 204);

    REQUIRE(mock.DeleteCallCount() == 1);
    CHECK(mock.DeleteCalls()[0].path == "/sap/bc/adt/abapgit/repos/KEY1");
}

TEST_CASE("Mock Delete: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.Delete("/any", {});

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "Delete");
}

// ===========================================================================
// MockAdtSession — FetchCsrfToken
// ===========================================================================

TEST_CASE("Mock FetchCsrfToken: returns enqueued token", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("token-abc-123")));

    auto result = mock.FetchCsrfToken();

    REQUIRE(result.IsOk());
    CHECK(result.Value() == "token-abc-123");
    CHECK(mock.CsrfCallCount() == 1);
}

TEST_CASE("Mock FetchCsrfToken: multiple calls consume FIFO", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("first")));
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("second")));

    auto r1 = mock.FetchCsrfToken();
    auto r2 = mock.FetchCsrfToken();

    REQUIRE(r1.IsOk());
    CHECK(r1.Value() == "first");
    REQUIRE(r2.IsOk());
    CHECK(r2.Value() == "second");
    CHECK(mock.CsrfCallCount() == 2);
}

TEST_CASE("Mock FetchCsrfToken: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.FetchCsrfToken();

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "FetchCsrfToken");
}

// ===========================================================================
// MockAdtSession — PollUntilComplete
// ===========================================================================

TEST_CASE("Mock PollUntilComplete: returns enqueued result", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<result/>", std::chrono::milliseconds{1500}}));

    auto result = mock.PollUntilComplete("/poll/location", std::chrono::seconds{30});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Completed);
    CHECK(result.Value().body == "<result/>");
    CHECK(result.Value().elapsed.count() == 1500);

    REQUIRE(mock.PollCallCount() == 1);
    CHECK(mock.PollCalls()[0].location_url == "/poll/location");
    CHECK(mock.PollCalls()[0].timeout == std::chrono::seconds{30});
}

TEST_CASE("Mock PollUntilComplete: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.PollUntilComplete("/any", std::chrono::seconds{10});

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "PollUntilComplete");
    CHECK(result.Error().endpoint == "/any");
}

// ===========================================================================
// MockAdtSession — Reset
// ===========================================================================

TEST_CASE("Mock Reset: clears all state", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, ""}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    mock.EnqueuePoll(Result<PollResult, Error>::Ok(PollResult{}));

    mock.Get("/a", {});
    mock.Post("/b", "", "", {});
    mock.Put("/c", "", "", {});
    mock.Delete("/d", {});
    mock.FetchCsrfToken();
    mock.PollUntilComplete("/e", std::chrono::seconds{1});
    mock.SetStateful(true);

    mock.Reset();

    CHECK(mock.GetCallCount() == 0);
    CHECK(mock.PostCallCount() == 0);
    CHECK(mock.PutCallCount() == 0);
    CHECK(mock.DeleteCallCount() == 0);
    CHECK(mock.CsrfCallCount() == 0);
    CHECK(mock.PollCallCount() == 0);
    CHECK_FALSE(mock.IsStateful());

    // Queues are also empty
    CHECK(mock.Get("/x", {}).IsErr());
    CHECK(mock.Post("/x", "", "", {}).IsErr());
    CHECK(mock.Put("/x", "", "", {}).IsErr());
    CHECK(mock.Delete("/x", {}).IsErr());
    CHECK(mock.FetchCsrfToken().IsErr());
    CHECK(mock.PollUntilComplete("/x", std::chrono::seconds{1}).IsErr());
}

// ===========================================================================
// IAdtSession — polymorphic usage via base pointer
// ===========================================================================

// ===========================================================================
// MockAdtSession — Put
// ===========================================================================

TEST_CASE("Mock Put: returns enqueued response and records call", "[adt][mock]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, "<updated/>"}));

    auto result = mock.Put(
        "/sap/bc/adt/oo/classes/ZCL_TEST/source/main",
        "CLASS zcl_test DEFINITION.",
        "text/plain",
        {{"x-csrf-token", "tok123"}});

    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(result.Value().body == "<updated/>");

    REQUIRE(mock.PutCallCount() == 1);
    CHECK(mock.PutCalls()[0].path == "/sap/bc/adt/oo/classes/ZCL_TEST/source/main");
    CHECK(mock.PutCalls()[0].body == "CLASS zcl_test DEFINITION.");
    CHECK(mock.PutCalls()[0].content_type == "text/plain");
    CHECK(mock.PutCalls()[0].headers.at("x-csrf-token") == "tok123");
}

TEST_CASE("Mock Put: empty queue returns error", "[adt][mock]") {
    MockAdtSession mock;
    auto result = mock.Put("/any", "body", "text/plain", {});

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "Put");
}

// ===========================================================================
// MockAdtSession — Stateful session
// ===========================================================================

TEST_CASE("Mock: stateful defaults to false", "[adt][mock]") {
    MockAdtSession mock;
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("Mock: SetStateful toggles state", "[adt][mock]") {
    MockAdtSession mock;
    mock.SetStateful(true);
    CHECK(mock.IsStateful());
    mock.SetStateful(false);
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("Mock: stateful via base pointer", "[adt][interface]") {
    auto mock = std::make_unique<MockAdtSession>();
    IAdtSession& session = *mock;
    CHECK_FALSE(session.IsStateful());
    session.SetStateful(true);
    CHECK(session.IsStateful());
}

// ===========================================================================
// IAdtSession — polymorphic usage via base pointer
// ===========================================================================

TEST_CASE("IAdtSession: mock usable through base pointer", "[adt][interface]") {
    auto mock = std::make_unique<MockAdtSession>();
    mock->EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "discovery-xml"}));

    IAdtSession& session = *mock;
    auto result = session.Get("/sap/bc/adt/discovery", {});

    REQUIRE(result.IsOk());
    CHECK(result.Value().body == "discovery-xml");
}
