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

} // anonymous namespace

// ===========================================================================
// ListTransports
// ===========================================================================

TEST_CASE("ListTransports: parses transport list", "[adt][transport]") {
    MockAdtSession mock;
    auto xml = LoadFixture("transport/transport_list.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());

    auto& transports = result.Value();
    REQUIRE(transports.size() == 3);

    CHECK(transports[0].number == "NPLK900001");
    CHECK(transports[0].description == "Implement feature X");
    CHECK(transports[0].owner == "DEVELOPER");
    CHECK(transports[0].status == "modifiable");
    CHECK(transports[0].target == "NPL");

    CHECK(transports[1].number == "NPLK900002");
    CHECK(transports[1].status == "released");
}

TEST_CASE("ListTransports: sends GET with user parameter", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<tm:root xmlns:tm=\"http://www.sap.com/cts/transports\"/>"}));

    auto result = ListTransports(mock, "ADMIN");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find("user=ADMIN") != std::string::npos);
}

TEST_CASE("ListTransports: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
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
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/NPLK900001/newreleasejobs");
}

TEST_CASE("ReleaseTransport: accepts 204", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900002");
    REQUIRE(result.IsOk());
}

TEST_CASE("ReleaseTransport: unexpected status returns error", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 409);
}

TEST_CASE("ReleaseTransport: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "connection refused", std::nullopt}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
}
