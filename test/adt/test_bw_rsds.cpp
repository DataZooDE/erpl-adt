#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_rsds.hpp>
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

}  // namespace

TEST_CASE("BwReadRsdsDetail: parses metadata and fields", "[adt][bw][rsds]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));

    auto result = BwReadRsdsDetail(mock, "ZSRC_SALES", "ECLCLNT100");
    REQUIRE(result.IsOk());

    const auto& rsds = result.Value();
    CHECK(rsds.name == "ZSRC_SALES");
    CHECK(rsds.source_system == "ECLCLNT100");
    CHECK(rsds.description == "Sales DataSource");
    CHECK(rsds.package_name == "ZPKG");
    REQUIRE(rsds.fields.size() == 3);
    CHECK(rsds.fields[0].name == "MATNR");
    CHECK(rsds.fields[0].segment_id == "SEG_MAIN");
    CHECK(rsds.fields[0].key);
    CHECK(rsds.fields[2].name == "QUANTITY");
    CHECK(rsds.fields[2].decimals == 3);
}

TEST_CASE("BwReadRsdsDetail: builds RSDS path", "[adt][bw][rsds]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));

    auto result = BwReadRsdsDetail(mock, "ZSRC_SALES", "ECLCLNT100", "m");
    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find(
              "/sap/bw/modeling/rsds/ZSRC_SALES/ECLCLNT100/m") !=
          std::string::npos);
}

TEST_CASE("BwReadRsdsDetail: 404 returns NotFound", "[adt][bw][rsds]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not Found"}));
    auto result = BwReadRsdsDetail(mock, "NOPE", "ECLCLNT100");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

