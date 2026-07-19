#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/ddic_domain.hpp>
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

// Fixtures are real captured Eclipse ADT / SAP ABAP Cloud Trial responses
// (localhost:50000, client 001, DEVELOPER) — not hand-written.

TEST_CASE("GetDomain: parses a domain with no fixed values (LANGU)", "[adt][ddic][domain]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, LoadFixture("ddic/domain_langu.xml")}));

    auto result = GetDomain(mock, "LANGU");
    REQUIRE(result.IsOk());
    const auto& info = result.Value();
    CHECK(info.name == "LANGU");
    CHECK(info.description == "Sprachenschlüssel");
    CHECK(info.data_type == "LANG");
    REQUIRE(info.length.has_value());
    CHECK(*info.length == 1);
    CHECK(info.conversion_exit == "ISOLA");
    CHECK(info.fix_values.empty());
}

TEST_CASE("GetDomain: parses a domain with fixed values (XFELD)", "[adt][ddic][domain]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, LoadFixture("ddic/domain_xfeld.xml")}));

    auto result = GetDomain(mock, "XFELD");
    REQUIRE(result.IsOk());
    const auto& info = result.Value();
    CHECK(info.name == "XFELD");
    CHECK(info.data_type == "CHAR");
    REQUIRE(info.fix_values.size() == 2);
    CHECK(info.fix_values[0].low == "X");
    CHECK(info.fix_values[0].text == "Ja");
    CHECK(info.fix_values[1].text == "Nein");
}

TEST_CASE("GetDomain: 404 maps to NotFound", "[adt][ddic][domain]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = GetDomain(mock, "ZDOES_NOT_EXIST");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}
