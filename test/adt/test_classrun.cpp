#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/classrun.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// RunClass
// ===========================================================================

TEST_CASE("RunClass: happy path returns console output", "[adt][classrun]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "Flights generated: 42\n"}));

    auto result = RunClass(mock, "ZCL_MY_CONSOLE");
    REQUIRE(result.IsOk());

    CHECK(result.Value().class_name == "ZCL_MY_CONSOLE");
    CHECK(result.Value().output == "Flights generated: 42\n");

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bc/adt/oo/classrun/ZCL_MY_CONSOLE");
}

TEST_CASE("RunClass: namespaced name encodes slashes as %2F", "[adt][classrun]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "done\n"}));

    auto result = RunClass(mock, "/DMO/CL_FOO");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    // Leading slash + namespace slash both encoded
    CHECK(mock.PostCalls()[0].path == "/sap/bc/adt/oo/classrun/%2FDMO%2FCL_FOO");
    CHECK(result.Value().class_name == "/DMO/CL_FOO");
}

TEST_CASE("RunClass: full ADT URI extracts class name", "[adt][classrun]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "ok\n"}));

    // Caller passes a full object URI — we strip to the last segment.
    auto result = RunClass(mock, "/sap/bc/adt/oo/classes/ZCL_FOO");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bc/adt/oo/classrun/ZCL_FOO");
}

TEST_CASE("RunClass: non-200 HTTP status returns error", "[adt][classrun]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {404, {}, "Class not found"}));

    auto result = RunClass(mock, "ZNONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
}

TEST_CASE("RunClass: network error propagates as Err", "[adt][classrun]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "/sap/bc/adt/oo/classrun/ZCL_FOO", std::nullopt,
              "connection refused", std::nullopt, ErrorCategory::Connection}));

    auto result = RunClass(mock, "ZCL_FOO");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Connection);
}
