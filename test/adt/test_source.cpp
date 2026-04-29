#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/source.hpp>
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
// ReadSource
// ===========================================================================

TEST_CASE("ReadSource: returns plain text source", "[adt][source]") {
    MockAdtSession mock;
    std::string source = "CLASS zcl_test DEFINITION PUBLIC.\nENDCLASS.\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, source}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == source);
}

TEST_CASE("ReadSource: sends GET with text/plain accept", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "source code"}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    auto& call = mock.GetCalls()[0];
    CHECK(call.path.find("version=active") != std::string::npos);
    CHECK(call.headers.at("Accept") == "text/plain");
}

TEST_CASE("ReadSource: custom version parameter", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "inactive source"}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main", "inactive");
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("version=inactive") != std::string::npos);
}

TEST_CASE("ReadSource: 404 returns NotFound error", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_missing/source/main");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("ReadSource: HTTP error propagated", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsErr());
}

TEST_CASE("ReadSource: URI with existing query string uses & separator", "[adt][source]") {
    // Bug fix: ReadSource must not append ?version= if URI already has ? from caller.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "source"}));

    const std::string uri_with_query =
        "/sap/bc/adt/oo/classes/zcl_test/source/main?other=foo";
    auto result = ReadSource(mock, uri_with_query, "active");
    REQUIRE(result.IsOk());

    const auto& path = mock.GetCalls()[0].path;
    // Must use & not ? for the version parameter.
    CHECK(path.find("&version=active") != std::string::npos);
    CHECK(path.find("?version=active") == std::string::npos);
    // Original query param must still be present.
    CHECK(path.find("other=foo") != std::string::npos);
}

TEST_CASE("ReadSource: URI without existing query string uses ? separator", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "source"}));

    auto result = ReadSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main", "active");
    REQUIRE(result.IsOk());

    const auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("?version=active") != std::string::npos);
}

// ===========================================================================
// WriteSource
// ===========================================================================

TEST_CASE("WriteSource: sends PUT with source and lock handle", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("lock123").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = WriteSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                              "CLASS zcl_test DEFINITION.\nENDCLASS.\n", handle);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PutCallCount() == 1);
    auto& call = mock.PutCalls()[0];
    CHECK(call.path.find("lockHandle=lock123") != std::string::npos);
    CHECK(call.body == "CLASS zcl_test DEFINITION.\nENDCLASS.\n");
    CHECK(call.content_type == "text/plain; charset=utf-8");
}

TEST_CASE("WriteSource: includes transport number", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = WriteSource(mock, "/sap/bc/adt/programs/programs/ztest/source/main",
                              "REPORT ztest.\n", handle, "NPLK900001");
    REQUIRE(result.IsOk());

    CHECK(mock.PutCalls()[0].path.find("corrNr=NPLK900001") != std::string::npos);
}

TEST_CASE("WriteSource: HTTP error propagated", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Err(
        Error{"Put", "", std::nullopt, "connection refused", std::nullopt}));

    auto result = WriteSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                              "source", handle);
    REQUIRE(result.IsErr());
}

TEST_CASE("WriteSource: unexpected status returns error", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = WriteSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                              "source", handle);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}

// ===========================================================================
// CheckSyntax
// ===========================================================================

TEST_CASE("CheckSyntax: clean result returns empty messages", "[adt][source]") {
    MockAdtSession mock;
    auto xml = LoadFixture("source/check_clean.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = CheckSyntax(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("CheckSyntax: parses error messages", "[adt][source]") {
    MockAdtSession mock;
    auto xml = LoadFixture("source/check_errors.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = CheckSyntax(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsOk());

    auto& msgs = result.Value();
    REQUIRE(msgs.size() == 3);

    CHECK(msgs[0].type == "E");
    CHECK(msgs[0].text == "Variable LV_UNDEFINED is not defined");
    CHECK(msgs[0].line == 10);
    CHECK(msgs[0].offset == 5);

    CHECK(msgs[1].type == "W");
    CHECK(msgs[1].text == "Variable LV_UNUSED is never used");
    CHECK(msgs[1].line == 25);

    CHECK(msgs[2].type == "I");
    CHECK(msgs[2].text == "Consider using inline declaration");
}

TEST_CASE("CheckSyntax: sends POST to checkruns endpoint", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<chkrun:checkRunReports xmlns:chkrun=\"http://www.sap.com/adt/checkrun\"/>"}));

    auto result = CheckSyntax(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path == "/sap/bc/adt/checkruns?reporters=abapCheckRun");
    CHECK(call.body.find("zcl_test/source/main") != std::string::npos);
}

TEST_CASE("CheckSyntax: HTTP error propagated", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = CheckSyntax(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main");
    REQUIRE(result.IsErr());
}

TEST_CASE("WriteSource: 400 Session not found adds actionable hint", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok(
        {400, {}, "<html><body>Session not found</body></html>"}));

    auto result = WriteSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                              "CLASS zcl_test DEFINITION.", handle, std::nullopt);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 400);
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("--session-file") != std::string::npos);
}

TEST_CASE("WriteSource: 423 invalid lock handle hints at stateful session enhancement", "[adt][source]") {
    MockAdtSession mock;
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok(
        {423, {}, "Resource CLASS ZCL_TEST is not locked (invalid lock handle: ABCD)"}));

    auto result = WriteSource(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                              "CLASS zcl_test DEFINITION.", handle, std::nullopt);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 423);
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("abapfs_extensions") != std::string::npos);
}

// ---------------------------------------------------------------------------
// WriteSourceOptimistic
// ---------------------------------------------------------------------------
TEST_CASE("WriteSourceOptimistic: succeeds on 200 without lock handle", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = WriteSourceOptimistic(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                                        "CLASS zcl_test DEFINITION.", std::nullopt);
    REQUIRE(result.IsOk());
    REQUIRE(mock.PutCallCount() == 1);
    CHECK(mock.PutCalls()[0].path.find("lockHandle") == std::string::npos);
}

TEST_CASE("WriteSourceOptimistic: succeeds on 204", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = WriteSourceOptimistic(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                                        "CLASS zcl_test DEFINITION.", std::nullopt);
    REQUIRE(result.IsOk());
}

TEST_CASE("WriteSourceOptimistic: includes transport in URL", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = WriteSourceOptimistic(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                                        "source", "NPLK900001");
    REQUIRE(result.IsOk());
    CHECK(mock.PutCalls()[0].path.find("corrNr=NPLK900001") != std::string::npos);
}

TEST_CASE("WriteSourceOptimistic: returns error on 423 without hint", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok(
        {423, {}, "Object is locked by another user"}));

    auto result = WriteSourceOptimistic(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                                        "source", std::nullopt);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 423);
}

TEST_CASE("WriteSourceOptimistic: returns error on 400", "[adt][source]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({400, {}, "Lock handle required"}));

    auto result = WriteSourceOptimistic(mock, "/sap/bc/adt/oo/classes/zcl_test/source/main",
                                        "source", std::nullopt);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 400);
}
