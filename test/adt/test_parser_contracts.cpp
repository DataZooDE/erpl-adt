#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_nodes.hpp>
#include <erpl_adt/adt/bw_search.hpp>
#include <erpl_adt/adt/bw_system.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

TEST_CASE("BW parser contracts: search parse errors include line diagnostics", "[adt][bw][parser]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed>\n  <entry>\n</feed>"}));

    BwSearchOptions options;
    options.query = "Z*";
    auto result = BwSearchObjects(mock, options);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("line") != std::string::npos);
}

TEST_CASE("BW parser contracts: nodes parse errors include line diagnostics", "[adt][bw][parser]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed>\n  <entry>\n</feed>"}));

    BwNodesOptions options;
    options.object_type = "ADSO";
    options.object_name = "ZSALES";
    auto result = BwGetNodes(mock, options);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("line") != std::string::npos);
}

TEST_CASE("BW parser contracts: system parse errors include line diagnostics", "[adt][bw][parser]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed>\n  <entry>\n</feed>"}));

    auto result = BwGetSystemInfo(mock);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("line") != std::string::npos);
}
