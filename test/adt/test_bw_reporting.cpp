#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_reporting.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

TEST_CASE("BwGetReportingMetadata: builds URL and headers", "[adt][bw][reporting]") {
    MockAdtSession mock;
    std::string xml = R"(<bicsResponse><metaData version="1"/></bicsResponse>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReportingOptions opts;
    opts.compid = "0D_FC_NW_C01_Q0007";
    opts.dbgmode = true;
    opts.metadata_only = true;
    opts.incl_metadata = true;
    opts.from_row = 1;
    opts.to_row = 10;

    auto result = BwGetReportingMetadata(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/comp/reporting?compid=0D_FC_NW_C01_Q0007&dbgmode=true");
    CHECK(mock.GetCalls()[0].headers.at("MetadataOnly") == "true");
    CHECK(mock.GetCalls()[0].headers.at("InclMetadata") == "true");
    CHECK(mock.GetCalls()[0].headers.at("FromRow") == "1");
    CHECK(mock.GetCalls()[0].headers.at("ToRow") == "10");
}

// The route needs an InfoProvider — without one the backend answers
// "Operation could not be carried out for" — and it serves
// infoprov_query_props, not the rulesQueryProperties type discovery
// advertises (that combination produced an HTTP 415 on every call).
TEST_CASE("BwGetQueryProperties: sends the InfoProvider and the served type",
          "[adt][bw][reporting]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<rules/>"}));

    auto result = BwGetQueryProperties(mock, "ZSALES", "ADSO", "M");
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/rules/qprops?infoprovider=ZSALES&objectType=ADSO&version=M");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.infoprov_query_props-v3_0_0+xml");
}

TEST_CASE("BwGetQueryProperties: an empty InfoProvider fails before the wire",
          "[adt][bw][reporting]") {
    MockAdtSession mock;
    auto result = BwGetQueryProperties(mock, "");
    REQUIRE(result.IsErr());
    CHECK(mock.GetCallCount() == 0);
}

TEST_CASE("BwGetQueryProperties: optional parameters are omitted when empty",
          "[adt][bw][reporting]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<rules/>"}));

    auto result = BwGetQueryProperties(mock, "ZSALES");
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bw/modeling/rules/qprops?infoprovider=ZSALES");
}
