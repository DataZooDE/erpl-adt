#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_lineage.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.find_last_of("/\\");
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // anonymous namespace

// ===========================================================================
// BwReadTransformation
// ===========================================================================

TEST_CASE("BwReadTransformation: parses source and target", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_trfn.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadTransformation(mock, "ZTRFN_SALES");
    REQUIRE(result.IsOk());

    const auto& detail = result.Value();
    CHECK(detail.name == "ZTRFN_SALES");
    CHECK(detail.description == "Sales Transformation");
    CHECK(detail.source_name == "ZSRC_SALES");
    CHECK(detail.source_type == "RSDS");
    CHECK(detail.target_name == "ZSALES_DATA");
    CHECK(detail.target_type == "ADSO");
}

TEST_CASE("BwReadTransformation: parses source fields", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_trfn.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadTransformation(mock, "ZTRFN_SALES");
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().source_fields;
    REQUIRE(fields.size() == 4);
    CHECK(fields[0].name == "MATNR");
    CHECK(fields[0].type == "CHAR");
    CHECK(fields[0].key == true);
    CHECK(fields[2].name == "QUANTITY");
    CHECK(fields[2].aggregation == "SUM");
    CHECK(fields[2].key == false);
}

TEST_CASE("BwReadTransformation: parses target fields", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_trfn.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadTransformation(mock, "ZTRFN_SALES");
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().target_fields;
    REQUIRE(fields.size() == 4);
    CHECK(fields[0].name == "MATERIAL");
    CHECK(fields[1].name == "PLANT");
}

TEST_CASE("BwReadTransformation: parses rules", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_trfn.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadTransformation(mock, "ZTRFN_SALES");
    REQUIRE(result.IsOk());

    const auto& rules = result.Value().rules;
    REQUIRE(rules.size() == 4);
    CHECK(rules[0].source_field == "MATNR");
    CHECK(rules[0].target_field == "MATERIAL");
    CHECK(rules[0].rule_type == "StepDirect");
    CHECK(rules[3].target_field == "AMOUNT");
    CHECK(rules[3].rule_type == "StepFormula");
    CHECK(rules[3].formula.find("AMOUNT") != std::string::npos);
}

TEST_CASE("BwReadTransformation: sends correct URL", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<trfn:transformation xmlns:trfn=\"x\"/>"}));

    auto result = BwReadTransformation(mock, "ZTRFN_SALES", "m");
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/trfn/ztrfn_sales/m") != std::string::npos);
}

TEST_CASE("BwReadTransformation: 404 returns NotFound error", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not Found"}));

    auto result = BwReadTransformation(mock, "NONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("BwReadTransformation: connection error propagated", "[adt][bw][lineage][trfn]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/trfn/TEST", std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwReadTransformation(mock, "TEST");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwReadAdsoDetail
// ===========================================================================

TEST_CASE("BwReadAdsoDetail: parses metadata", "[adt][bw][lineage][adso]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_adso.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadAdsoDetail(mock, "ZSALES_DATA");
    REQUIRE(result.IsOk());

    const auto& detail = result.Value();
    CHECK(detail.name == "ZSALES_DATA");
    CHECK(detail.description == "Sales DataStore Object");
    CHECK(detail.package_name == "ZTEST");
}

TEST_CASE("BwReadAdsoDetail: parses fields", "[adt][bw][lineage][adso]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_adso.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadAdsoDetail(mock, "ZSALES_DATA");
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 4);
    CHECK(fields[0].name == "MATERIAL");
    CHECK(fields[0].data_type == "CHAR");
    CHECK(fields[0].length == 18);
    CHECK(fields[0].key == true);
    CHECK(fields[1].name == "PLANT");
    CHECK(fields[1].key == true);
    CHECK(fields[2].name == "QUANTITY");
    CHECK(fields[2].data_type == "DEC");
    CHECK(fields[2].length == 13);
    CHECK(fields[2].decimals == 3);
    CHECK(fields[2].key == false);
    CHECK(fields[3].name == "AMOUNT");
    CHECK(fields[3].data_type == "CURR");
    CHECK(fields[3].decimals == 2);
}

TEST_CASE("BwReadAdsoDetail: sends correct URL", "[adt][bw][lineage][adso]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<adso:adso xmlns:adso=\"x\"/>"}));

    auto result = BwReadAdsoDetail(mock, "ZSALES_DATA", "a");
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/adso/zsales_data/a") != std::string::npos);
}

TEST_CASE("BwReadAdsoDetail: 404 returns NotFound", "[adt][bw][lineage][adso]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not Found"}));

    auto result = BwReadAdsoDetail(mock, "NONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

// ===========================================================================
// BwReadDtpDetail
// ===========================================================================

TEST_CASE("BwReadDtpDetail: parses source and target", "[adt][bw][lineage][dtp]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_dtp.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwReadDtpDetail(mock, "ZDTP_SALES");
    REQUIRE(result.IsOk());

    const auto& detail = result.Value();
    CHECK(detail.name == "ZDTP_SALES");
    CHECK(detail.description == "Sales Data Transfer");
    CHECK(detail.source_name == "ZSRC_SALES");
    CHECK(detail.source_type == "RSDS");
    CHECK(detail.target_name == "ZSALES_DATA");
    CHECK(detail.target_type == "ADSO");
    CHECK(detail.source_system == "ECLCLNT100");
}

TEST_CASE("BwReadDtpDetail: sends correct URL", "[adt][bw][lineage][dtp]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<dtpa:dtp xmlns:dtpa=\"x\"/>"}));

    auto result = BwReadDtpDetail(mock, "ZDTP_SALES", "a");
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/dtpa/zdtp_sales/a") != std::string::npos);
}

TEST_CASE("BwReadDtpDetail: 404 returns NotFound", "[adt][bw][lineage][dtp]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not Found"}));

    auto result = BwReadDtpDetail(mock, "NONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("BwReadDtpDetail: connection error propagated", "[adt][bw][lineage][dtp]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/dtpa/TEST", std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwReadDtpDetail(mock, "TEST");
    REQUIRE(result.IsErr());
}
