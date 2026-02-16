#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_validation.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

TEST_CASE("BwValidateObject: builds URL and parses entries", "[adt][bw][validation]") {
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
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

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

    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/validation?objectType=ADSO&objectName=ZSALES&action=validate");
}

TEST_CASE("BwValidateObject: validates required args", "[adt][bw][validation]") {
    MockAdtSession mock;
    BwValidationOptions opts;
    opts.object_type = "";
    opts.object_name = "X";

    auto result = BwValidateObject(mock, opts);
    REQUIRE(result.IsErr());
}

TEST_CASE("BwListMoveRequests: parses move requests", "[adt][bw][validation]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Move Request 1</title>
                <content type="application/xml">
                    <properties request="MOVE0001" owner="DEVELOPER" status="OPEN"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwListMoveRequests(mock);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].request == "MOVE0001");
    CHECK(result.Value()[0].owner == "DEVELOPER");
    CHECK(result.Value()[0].status == "OPEN");
    CHECK(result.Value()[0].description == "Move Request 1");

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/move_requests");
}

TEST_CASE("BwListMoveRequests: propagates HTTP error", "[adt][bw][validation]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwListMoveRequests(mock);
    REQUIRE(result.IsErr());
}
