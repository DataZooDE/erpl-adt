#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_validation.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// CL_RSO_RES_VALIDATION implements post() only: a GET answers HTTP 405
// "Resource controller does not support method GET", and the action must be
// one of exists / new / standard_transport / is_plannable — the old default
// "validate" was rejected as an invalid action even when the verb was right.
// ===========================================================================

TEST_CASE("BwValidateObject: posts and parses entries", "[adt][bw][validation]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Validation warning</title>
                <content type="application/xml">
                    <properties severity="W" objectType="ADSO" objectName="ZSALES" code="BW123"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwValidationOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";

    auto result = BwValidateObject(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].severity == "W");
    CHECK(result.Value()[0].object_type == "ADSO");
    CHECK(result.Value()[0].object_name == "ZSALES");
    CHECK(result.Value()[0].code == "BW123");

    CHECK(mock.GetCallCount() == 0);
    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bw/modeling/validation?objectType=ADSO&objectName=ZSALES&action=exists");
}

TEST_CASE("BwValidateObject: the action is one the backend accepts",
          "[adt][bw][validation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, R"(<feed xmlns="http://www.w3.org/2005/Atom"/>)"}));

    BwValidationOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZNEW";
    opts.action = "new";

    auto result = BwValidateObject(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].path.find("action=new") != std::string::npos);
}

TEST_CASE("BwValidateObject: validates required args", "[adt][bw][validation]") {
    MockAdtSession mock;
    BwValidationOptions opts;
    opts.object_type = "";
    opts.object_name = "X";

    auto result = BwValidateObject(mock, opts);
    REQUIRE(result.IsErr());
}

// The move endpoint executes moves and never listed anything: GET answered
// HTTP 405 on every system.
TEST_CASE("BwListMoveRequests: explains that the endpoint has no listing",
          "[adt][bw][validation]") {
    MockAdtSession mock;

    auto result = BwListMoveRequests(mock);
    REQUIRE(result.IsErr());
    CHECK(mock.GetCallCount() == 0);
    CHECK(result.Error().message.find("no listing") != std::string::npos);
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("POST") != std::string::npos);
}

TEST_CASE("BwListMoveRequests: propagates HTTP error", "[adt][bw][validation]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwListMoveRequests(mock);
    REQUIRE(result.IsErr());
}


// A clean validation answers 200 with an empty body — reporting that as a
// parse failure made every successful validation look broken.
TEST_CASE("BwValidateObject: an empty body means nothing to report",
          "[adt][bw][validation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwValidationOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";

    auto result = BwValidateObject(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}
