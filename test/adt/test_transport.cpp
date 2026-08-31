#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/transport.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.find_last_of("/\\");
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ListTransports does two GETs: (1) search-configurations, (2) transport tree.
// This helper enqueues both responses on the mock in that order.
void EnqueueListTransportsResponses(MockAdtSession& mock,
                                    const std::string& configs_xml,
                                    const std::string& transports_xml) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, configs_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, transports_xml}));
}

} // anonymous namespace

// ===========================================================================
// ListTransports
// ===========================================================================

TEST_CASE("ListTransports: parses nested transport tree", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());

    auto& transports = result.Value();
    REQUIRE(transports.size() == 3);

    // Order is iteration order of the tree (modifiable first, then released).
    CHECK(transports[0].number == "NPLK900001");
    CHECK(transports[0].description == "Implement feature X");
    CHECK(transports[0].owner == "DEVELOPER");
    CHECK(transports[0].status == "modifiable");
    CHECK(transports[0].target == "NPL");

    CHECK(transports[1].number == "NPLK900003");
    CHECK(transports[1].status == "modifiable");

    CHECK(transports[2].number == "NPLK900002");
    CHECK(transports[2].status == "released");
}

TEST_CASE("ListTransports: queries configUri when configuration exists", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/searchconfiguration/configurations");
    CHECK(mock.GetCalls()[1].path.find("configUri=") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("targets=true") != std::string::npos);
}

TEST_CASE("ListTransports: filters by owner when using configUri", "[adt][transport]") {
    MockAdtSession mock;
    // Real response has DEVELOPER as owner. We pass a different user.
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "ADMIN");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ListTransports: falls back to user=USER when no configuration exists",
          "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations_empty.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "ADMIN");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[1].path.find("user=ADMIN") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("configUri=") == std::string::npos);
    // No owner filter applied in fallback mode — all 3 entries pass through.
    CHECK(result.Value().size() == 3);
}

TEST_CASE("ListTransports: tolerates failing search-config lookup", "[adt][transport]") {
    MockAdtSession mock;
    // Search-config call fails — fall back to legacy ?user= query.
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_list.xml")}));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[1].path.find("user=DEVELOPER") != std::string::npos);
}

TEST_CASE("ListTransports: HTTP error on tree fetch propagated", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/search_configurations.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// CreateTransport
// ===========================================================================

TEST_CASE("CreateTransport: returns transport number", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900005\n"}));

    auto result = CreateTransport(mock, "My new transport", "ZTEST_PKG");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "NPLK900005");
}

TEST_CASE("CreateTransport: extracts number from URI path", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "/sap/bc/adt/cts/transports/NPLK900010"}));

    auto result = CreateTransport(mock, "Another transport", "ZDEV");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "NPLK900010");
}

TEST_CASE("CreateTransport: sends correct body", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900001"}));

    auto result = CreateTransport(mock, "Test transport", "ZPKG");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path == "/sap/bc/adt/cts/transports");
    CHECK(call.body.find("ZPKG") != std::string::npos);
    CHECK(call.body.find("Test transport") != std::string::npos);
}

TEST_CASE("CreateTransport: escapes XML in body fields", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900001"}));

    auto result = CreateTransport(mock, "Fix <bug> & \"quote\"", "Z&PKG");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("<DEVCLASS>Z&amp;PKG</DEVCLASS>") != std::string::npos);
    CHECK(body.find("<REQUEST_TEXT>Fix &lt;bug&gt; &amp; &quot;quote&quot;</REQUEST_TEXT>") != std::string::npos);
}

TEST_CASE("CreateTransport: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = CreateTransport(mock, "Fail", "ZPKG");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// ReleaseTransport
// ===========================================================================

TEST_CASE("ReleaseTransport: sends POST to release endpoint", "[adt][transport]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/NPLK900001/newreleasejobs");
}

TEST_CASE("ReleaseTransport: accepts 204", "[adt][transport]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900002");
    REQUIRE(result.IsOk());
}

TEST_CASE("ReleaseTransport: unexpected status returns error", "[adt][transport]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 409);
}

TEST_CASE("ReleaseTransport: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "connection refused", std::nullopt}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
}


// Releasing a transport that does not exist answered HTTP 200 with an empty
// body, and the CLI reported "Released transport: X" — a claim about a
// one-way operation that never happened.
TEST_CASE("ReleaseTransport: a missing transport is not reported as released",
          "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "not found"}));

    auto result = ReleaseTransport(mock, "ZZZK900099");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(mock.PostCallCount() == 0);
}
