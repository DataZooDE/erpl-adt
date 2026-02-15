#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_search.hpp>
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

BwSearchOptions MakeSearchOptions(const std::string& query, int max = 100) {
    BwSearchOptions opts;
    opts.query = query;
    opts.max_results = max;
    return opts;
}

} // anonymous namespace

// ===========================================================================
// BwSearchObjects — success cases
// ===========================================================================

TEST_CASE("BwSearchObjects: parses search results", "[adt][bw][search]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_search.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwSearchObjects(mock, MakeSearchOptions("Z*"));
    REQUIRE(result.IsOk());

    const auto& items = result.Value();
    REQUIRE(items.size() == 3);
    CHECK(items[0].name == "ZSALES_DATA");
    CHECK(items[0].type == "ADSO");
    CHECK(items[0].status == "ACT");
    CHECK(items[0].description == "Sales DataStore Object");
    CHECK(items[1].name == "0MATERIAL");
    CHECK(items[1].type == "IOBJ");
    CHECK(items[2].name == "ZINACTIVE");
    CHECK(items[2].status == "INA");
}

TEST_CASE("BwSearchObjects: sends correct URL with type filter", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "Z*";
    opts.max_results = 50;
    opts.object_type = "ADSO";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("searchTerm=Z%2A") != std::string::npos);
    CHECK(path.find("maxSize=50") != std::string::npos);
    CHECK(path.find("objectType=ADSO") != std::string::npos);
}

TEST_CASE("BwSearchObjects: sends Accept atom+xml header", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwSearchObjects(mock, MakeSearchOptions("*"));
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwSearchObjects: sends status filter", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.object_status = "INA";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("objectStatus=INA") != std::string::npos);
}

TEST_CASE("BwSearchObjects: sends changed-by filter", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.changed_by = "DEVELOPER";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("changedBy=DEVELOPER") != std::string::npos);
}

TEST_CASE("BwSearchObjects: empty query returns error", "[adt][bw][search]") {
    MockAdtSession mock;
    auto result = BwSearchObjects(mock, MakeSearchOptions(""));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("must not be empty") != std::string::npos);
}

TEST_CASE("BwSearchObjects: empty feed returns empty vector", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwSearchObjects(mock, MakeSearchOptions("NONEXIST"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwSearchObjects: HTTP error propagated", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Internal Error"}));

    auto result = BwSearchObjects(mock, MakeSearchOptions("Z*"));
    REQUIRE(result.IsErr());
}

TEST_CASE("BwSearchObjects: connection error propagated", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/repo/is/bwsearch",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwSearchObjects(mock, MakeSearchOptions("Z*"));
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Extended search parameters (spec 7.2)
// ===========================================================================

TEST_CASE("BwSearchObjects: sends objectSubType filter", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.object_sub_type = "REP";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("objectSubType=REP") != std::string::npos);
}

TEST_CASE("BwSearchObjects: sends date range filters", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.changed_on_from = "2026-01-01";
    opts.changed_on_to = "2026-12-31";
    opts.created_on_from = "2025-06-01";
    opts.created_on_to = "2025-12-31";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("changedOnFrom=2026-01-01") != std::string::npos);
    CHECK(path.find("changedOnTo=2026-12-31") != std::string::npos);
    CHECK(path.find("createdOnFrom=2025-06-01") != std::string::npos);
    CHECK(path.find("createdOnTo=2025-12-31") != std::string::npos);
}

TEST_CASE("BwSearchObjects: sends createdBy filter", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.created_by = "DEVELOPER";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("createdBy=DEVELOPER") != std::string::npos);
}

TEST_CASE("BwSearchObjects: sends dependency filters", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "*";
    opts.depends_on_name = "0MATERIAL";
    opts.depends_on_type = "IOBJ";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("dependsOnObjectName=0MATERIAL") != std::string::npos);
    CHECK(path.find("dependsOnObjectType=IOBJ") != std::string::npos);
}

TEST_CASE("BwSearchObjects: searchInName=false is sent", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "SALES";
    opts.search_in_name = false;
    opts.search_in_description = true;
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("searchInName=false") != std::string::npos);
    CHECK(path.find("searchInDescription=true") != std::string::npos);
}

TEST_CASE("BwSearchObjects: all filters combined", "[adt][bw][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwSearchOptions opts;
    opts.query = "Z*";
    opts.max_results = 50;
    opts.object_type = "ADSO";
    opts.object_sub_type = "SOB";
    opts.object_status = "ACT";
    opts.object_version = "A";
    opts.changed_by = "ADMIN";
    opts.created_by = "DEVELOPER";
    auto result = BwSearchObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("objectType=ADSO") != std::string::npos);
    CHECK(path.find("objectSubType=SOB") != std::string::npos);
    CHECK(path.find("objectStatus=ACT") != std::string::npos);
    CHECK(path.find("objectVersion=A") != std::string::npos);
    CHECK(path.find("changedBy=ADMIN") != std::string::npos);
    CHECK(path.find("createdBy=DEVELOPER") != std::string::npos);
}
