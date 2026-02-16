#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_valuehelp.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

TEST_CASE("BwGetValueHelp: builds valuehelp URL", "[adt][bw][valuehelp]") {
    MockAdtSession mock;
    std::string xml = R"(<valueHelp><row key="BW" text="BW Area"/></valueHelp>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwValueHelpOptions opts;
    opts.domain = "infoareas";
    opts.max_rows = 100;
    opts.pattern = "Z*";

    auto result = BwGetValueHelp(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/is/values/infoareas?maxrows=100&pattern=Z%2A");
    REQUIRE_FALSE(result.Value().empty());
}

TEST_CASE("BwGetVirtualFolders: sends endpoint", "[adt][bw][valuehelp]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<virtualFoldersResult/>"}));

    auto result = BwGetVirtualFolders(mock, std::optional<std::string>{"ZPKG"},
                                      std::nullopt, std::nullopt);
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/virtualfolders?package=ZPKG");
}

TEST_CASE("BwGetDataVolumes: sends endpoint", "[adt][bw][valuehelp]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<dataVolumes/>"}));

    auto result = BwGetDataVolumes(mock, std::optional<std::string>{"ZADSO"},
                                   std::optional<int>{50});
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/datavolumes?infoprovider=ZADSO&maxrows=50");
}
