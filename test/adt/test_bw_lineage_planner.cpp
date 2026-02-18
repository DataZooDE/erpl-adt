#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_lineage_planner.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string SearchFeed(const std::string& entries) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
           "xmlns:bwModel=\"http://www.sap.com/bw/modeling\">" +
           entries + "</feed>";
}

std::string Entry(const std::string& name, const std::string& version = "A",
                  const std::string& status = "ACT") {
    return "<entry>"
           "  <title>" + name + "</title>"
           "  <id>/sap/bw/modeling/dtpa/" + name + "/a</id>"
           "  <content type=\"application/xml\">"
           "    <bwModel:searchResult objectName=\"" + name + "\" objectType=\"DTPA\" "
           "      objectVersion=\"" + version + "\" objectStatus=\"" + status + "\"/>"
           "  </content>"
           "</entry>";
}

std::string Dtp(const std::string& name, const std::string& target_name,
                const std::string& target_type) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<dtpa name=\"" + name + "\" description=\"d\">"
           "  <source objectName=\"ZSRC\" objectType=\"RSDS\" sourceSystem=\"LOCAL\"/>"
           "  <target objectName=\"" + target_name + "\" objectType=\"" + target_type + "\"/>"
           "</dtpa>";
}

}  // namespace

TEST_CASE("BwPlanQueryUpstreamLineage: selects single typed DTP candidate",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_ZSALES"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_ZSALES", "ZCP_SALES", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    auto result = BwPlanQueryUpstreamLineage(mock, detail);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    REQUIRE(plan.selected_dtp.has_value());
    CHECK(*plan.selected_dtp == "DTP_ZSALES");
    CHECK_FALSE(plan.ambiguous);
    REQUIRE(plan.candidates.size() == 1);
    CHECK(plan.candidates[0].evidence == "bwSearch.depends_on_typed");
}

TEST_CASE("BwPlanQueryUpstreamLineage: fallback search without type when typed search is empty",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, SearchFeed("")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_ZFALLBACK"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_ZFALLBACK", "ZCP_SALES", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    auto result = BwPlanQueryUpstreamLineage(mock, detail);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    REQUIRE(plan.selected_dtp.has_value());
    CHECK(*plan.selected_dtp == "DTP_ZFALLBACK");
    REQUIRE(plan.candidates.size() == 1);
    CHECK(plan.candidates[0].evidence == "bwSearch.depends_on_name");

    REQUIRE(mock.GetCallCount() == 3);
    CHECK(mock.GetCalls()[0].path.find("dependsOnObjectType=HCPR") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("dependsOnObjectType") == std::string::npos);
}

TEST_CASE("BwPlanQueryUpstreamLineage: marks ambiguity for multiple candidates",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_B") + Entry("DTP_A"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_B", "ZCP_SALES", "HCPR")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_A", "ZCP_SALES", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    auto result = BwPlanQueryUpstreamLineage(mock, detail);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    CHECK_FALSE(plan.selected_dtp.has_value());
    CHECK(plan.ambiguous);
    REQUIRE(plan.candidates.size() == 2);
    CHECK(plan.candidates[0].object_name == "DTP_A");
    CHECK(plan.candidates[1].object_name == "DTP_B");
}

TEST_CASE("BwPlanQueryUpstreamLineage: retries with larger maxSize on feedIncomplete",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                  "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                  "xmlns:bwModel=\"http://www.sap.com/bw/modeling\" "
                  "bwModel:feedIncomplete=\"true\">"
                  + Entry("DTP_A") + "</feed>"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_A"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_A", "ZCP_SALES", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";
    BwUpstreamLineagePlannerOptions options;
    options.initial_max_results = 10;
    options.max_steps = 4;
    options.max_results_cap = 100;

    auto result = BwPlanQueryUpstreamLineage(mock, detail, options);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    CHECK(plan.complete == false);
    CHECK(plan.steps == 2);
    REQUIRE(mock.GetCallCount() >= 2);
    CHECK(mock.GetCalls()[0].path.find("maxSize=10") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("maxSize=20") != std::string::npos);
}

TEST_CASE("BwPlanQueryUpstreamLineage: drops structurally invalid DTP target",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_WRONG"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_WRONG", "ZCP_OTHER", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    auto result = BwPlanQueryUpstreamLineage(mock, detail);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    CHECK_FALSE(plan.selected_dtp.has_value());
    CHECK(plan.candidates.empty());
    CHECK_FALSE(plan.warnings.empty());
}

TEST_CASE("BwPlanQueryUpstreamLineage: non-ADT version labels still read active DTP by fallback",
          "[adt][bw][lineage][planner]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchFeed(Entry("DTP_LIVE", "active", "active"))}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Dtp("DTP_LIVE", "ZCP_SALES", "HCPR")}));

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    auto result = BwPlanQueryUpstreamLineage(mock, detail);
    REQUIRE(result.IsOk());
    const auto& plan = result.Value();
    REQUIRE(plan.selected_dtp.has_value());
    CHECK(*plan.selected_dtp == "DTP_LIVE");
}
