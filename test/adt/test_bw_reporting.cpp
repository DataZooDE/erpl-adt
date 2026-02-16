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

TEST_CASE("BwGetQueryProperties: sends endpoint", "[adt][bw][reporting]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<rules/>"}));

    auto result = BwGetQueryProperties(mock);
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/rules/qprops");
}
