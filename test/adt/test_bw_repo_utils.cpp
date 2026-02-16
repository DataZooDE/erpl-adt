#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_repo_utils.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

TEST_CASE("BwGetSearchMetadata: parses atom entries", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Object Type</title>
                <content type="application/xml">
                    <properties name="objectType" value="ADSO" category="basic"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetSearchMetadata(mock);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].name == "objectType");
    CHECK(result.Value()[0].value == "ADSO");
    CHECK(result.Value()[0].category == "basic");
}

TEST_CASE("BwGetSearchMetadata: uses metadata endpoint", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetSearchMetadata(mock);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/bwsearch/metadata");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwListBackendFavorites: parses favorites", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Sales ADSO</title>
                <id>/sap/bw/modeling/adso/ZSALES/a</id>
                <content type="application/xml">
                    <properties objectName="ZSALES" objectType="ADSO"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwListBackendFavorites(mock);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].name == "ZSALES");
    CHECK(result.Value()[0].type == "ADSO");
    CHECK(result.Value()[0].uri == "/sap/bw/modeling/adso/ZSALES/a");
}

TEST_CASE("BwDeleteAllBackendFavorites: uses delete endpoint", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwDeleteAllBackendFavorites(mock);
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    CHECK(mock.DeleteCalls()[0].path == "/sap/bw/modeling/repo/backendfavorites");
}

TEST_CASE("BwGetNodePath: encodes objectUri query", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    std::string xml = R"(
        <nodepath>
            <node objectName="BW" objectType="AREA" objectUri="/sap/bw/modeling/area/BW/a"/>
            <node objectName="ZSALES" objectType="ADSO" objectUri="/sap/bw/modeling/adso/ZSALES/a"/>
        </nodepath>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetNodePath(mock, "/sap/bw/modeling/adso/ZSALES/a");
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);

    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/repo/nodepath?objectUri=%2Fsap%2Fbw%2Fmodeling%2Fadso%2FZSALES%2Fa");
}

TEST_CASE("BwGetApplicationLog: supports filters", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Activation warning</title>
                <content type="application/xml">
                    <properties identifier="A1" username="DEVELOPER" severity="W"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwApplicationLogOptions opts;
    opts.username = "DEVELOPER";
    opts.start_timestamp = "20260101000000";
    opts.end_timestamp = "20260131235959";

    auto result = BwGetApplicationLog(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].identifier == "A1");

    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/repo/is/applicationlog?username=DEVELOPER&starttimestamp=20260101000000&endtimestamp=20260131235959");
}

TEST_CASE("BwGetMessageText: builds message URL with variables", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "Resolved message"}));

    BwMessageTextOptions opts;
    opts.identifier = "RSDHA";
    opts.text_type = "001";
    opts.msgv1 = "ZSALES";
    opts.msgv2 = "ADSO";

    auto result = BwGetMessageText(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().text == "Resolved message");

    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/repo/is/message/RSDHA/001?msgv1=ZSALES&msgv2=ADSO");
}

TEST_CASE("BwGetMessageText: validates required parameters", "[adt][bw][repo-utils]") {
    MockAdtSession mock;
    BwMessageTextOptions opts;
    opts.identifier = "";
    opts.text_type = "001";

    auto result = BwGetMessageText(mock, opts);
    REQUIRE(result.IsErr());
}
