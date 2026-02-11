#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/search.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// Helper: load test data files
// ===========================================================================

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.rfind('/');
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.rfind('/'));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

SearchOptions MakeSearchOptions(const std::string& query,
                                int max_results = 100,
                                std::optional<std::string> object_type = std::nullopt) {
    SearchOptions opts;
    opts.query = query;
    opts.max_results = max_results;
    opts.object_type = std::move(object_type);
    return opts;
}

} // anonymous namespace

// ===========================================================================
// SearchObjects — success cases
// ===========================================================================

TEST_CASE("SearchObjects: parses results from XML", "[adt][search]") {
    MockAdtSession mock;
    auto xml = LoadFixture("search/search_results.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = SearchObjects(mock, MakeSearchOptions("ZCL_*"));
    REQUIRE(result.IsOk());

    auto& results = result.Value();
    REQUIRE(results.size() == 3);

    CHECK(results[0].name == "ZCL_EXAMPLE");
    CHECK(results[0].type == "CLAS/OC");
    CHECK(results[0].uri == "/sap/bc/adt/oo/classes/zcl_example");
    CHECK(results[0].description == "Example class");
    CHECK(results[0].package_name == "ZTEST_PKG");

    CHECK(results[1].name == "ZCL_HELPER");
    CHECK(results[2].name == "ZTEST_PROG");
    CHECK(results[2].type == "PROG/P");
}

TEST_CASE("SearchObjects: empty results", "[adt][search]") {
    MockAdtSession mock;
    auto xml = LoadFixture("search/search_empty.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = SearchObjects(mock, MakeSearchOptions("NONEXISTENT_*"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("SearchObjects: sends correct URL", "[adt][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<adtcore:objectReferences xmlns:adtcore=\"http://www.sap.com/adt/core\"/>"}));

    auto result = SearchObjects(mock, MakeSearchOptions("ZCL_*", 50, "CLAS"));
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("operation=quickSearch") != std::string::npos);
    CHECK(path.find("query=ZCL_*") != std::string::npos);
    CHECK(path.find("maxResults=50") != std::string::npos);
    CHECK(path.find("objectType=CLAS") != std::string::npos);
}

TEST_CASE("SearchObjects: default max_results", "[adt][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<adtcore:objectReferences xmlns:adtcore=\"http://www.sap.com/adt/core\"/>"}));

    auto result = SearchObjects(mock, MakeSearchOptions("Z*"));
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find("maxResults=100") != std::string::npos);
}

// ===========================================================================
// SearchObjects — error cases
// ===========================================================================

TEST_CASE("SearchObjects: empty query returns error", "[adt][search]") {
    MockAdtSession mock;
    auto result = SearchObjects(mock, MakeSearchOptions(""));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("empty") != std::string::npos);
}

TEST_CASE("SearchObjects: HTTP error propagated", "[adt][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/repository/informationsystem/search",
              std::nullopt, "Connection refused", std::nullopt}));

    auto result = SearchObjects(mock, MakeSearchOptions("ZCL_*"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Connection refused") != std::string::npos);
}

TEST_CASE("SearchObjects: non-200 status returns error", "[adt][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = SearchObjects(mock, MakeSearchOptions("ZCL_*"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
}

TEST_CASE("SearchObjects: invalid XML returns error", "[adt][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "not xml at all"}));

    auto result = SearchObjects(mock, MakeSearchOptions("ZCL_*"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("parse") != std::string::npos);
}
