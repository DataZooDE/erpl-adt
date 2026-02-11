#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/activation.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

#include <chrono>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::vector<InactiveObject> SampleObjects() {
    return {
        {"CLAS", "ZCL_TEST", "/sap/bc/adt/oo/classes/ZCL_TEST"},
        {"INTF", "ZIF_TEST", "/sap/bc/adt/oo/interfaces/ZIF_TEST"},
    };
}

} // namespace

// ===========================================================================
// GetInactiveObjects
// ===========================================================================

TEST_CASE("GetInactiveObjects: returns parsed objects on 200", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<inactive-xml/>"}));
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(SampleObjects()));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().size() == 2);
    CHECK(result.Value()[0].type == "CLAS");
    CHECK(result.Value()[0].name == "ZCL_TEST");
    CHECK(result.Value()[1].type == "INTF");

    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/activation/inactive");
}

TEST_CASE("GetInactiveObjects: propagates HTTP error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "connection failed", std::nullopt}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "connection failed");
}

TEST_CASE("GetInactiveObjects: returns error on non-200", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {401, {}, "Unauthorized"}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 401);
}

TEST_CASE("GetInactiveObjects: returns empty vector when none inactive", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<empty/>"}));
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(std::vector<InactiveObject>{}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

// ===========================================================================
// ActivateAll
// ===========================================================================

TEST_CASE("ActivateAll: returns zero-count result for empty object list", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    auto result = ActivateAll(session, codec, {});

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 0);
    CHECK(result.Value().activated == 0);
    CHECK(result.Value().failed == 0);
    // No HTTP calls should be made.
    CHECK(session.PostCallCount() == 0);
    CHECK(session.CsrfCallCount() == 0);
}

TEST_CASE("ActivateAll: handles sync 200 response", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<activation-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<result/>"}));

    ActivationResult expected{2, 2, 0, {}};
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(expected));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 2);
    CHECK(result.Value().activated == 2);
    CHECK(result.Value().failed == 0);
    CHECK(result.Value().error_messages.empty());

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path == "/sap/bc/adt/activation");
    CHECK(session.PostCalls()[0].headers.at("x-csrf-token") == "csrf-tok");
}

TEST_CASE("ActivateAll: handles async 202 with poll", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/activation/789"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<activation-result/>",
                   std::chrono::milliseconds{4000}}));

    ActivationResult expected{5, 4, 1, {"CLAS ZCL_BROKEN: syntax error"}};
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(expected));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 5);
    CHECK(result.Value().activated == 4);
    CHECK(result.Value().failed == 1);
    CHECK(result.Value().error_messages.size() == 1);

    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/activation/789");
}

TEST_CASE("ActivateAll: returns error when poll fails", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/123"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Failed, "", std::chrono::milliseconds{1000}}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "async activation operation failed");
}

TEST_CASE("ActivateAll: propagates CSRF error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}

TEST_CASE("ActivateAll: propagates XML build error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Err(
            Error{"BuildActivationXml", "", std::nullopt,
                  "xml build failed", std::nullopt}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "xml build failed");
}

TEST_CASE("ActivateAll: returns error on unexpected status", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {500, {}, "Error"}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}
