#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_discovery.hpp>
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
// BwDiscover — success cases
// ===========================================================================

TEST_CASE("BwDiscover: parses discovery document", "[adt][bw][discovery]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_discovery.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwDiscover(mock);
    REQUIRE(result.IsOk());

    const auto& disc = result.Value();
    REQUIRE(disc.services.size() > 0);
}

TEST_CASE("BwDiscover: sends correct path and Accept header", "[adt][bw][discovery]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<service/>"}));

    auto result = BwDiscover(mock);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/discovery");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atomsvc+xml");
}

TEST_CASE("BwDiscover: HTTP error propagated", "[adt][bw][discovery]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not Found"}));

    auto result = BwDiscover(mock);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.has_value());
}

TEST_CASE("BwDiscover: connection error propagated", "[adt][bw][discovery]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/discovery",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwDiscover(mock);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "Connection refused");
}

// ===========================================================================
// BwResolveEndpoint
// ===========================================================================

TEST_CASE("BwResolveEndpoint: finds matching service", "[adt][bw][discovery]") {
    BwDiscoveryResult disc;
    disc.services.push_back({"http://www.sap.com/bw/modeling/adso", "adso",
                              "/sap/bw/modeling/adso/{adsonm}/{version}", ""});
    disc.services.push_back({"http://www.sap.com/bw/modeling/repo", "bwSearch",
                              "/sap/bw/modeling/repo/is/bwsearch", ""});

    auto result = BwResolveEndpoint(disc, "http://www.sap.com/bw/modeling/repo", "bwSearch");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "/sap/bw/modeling/repo/is/bwsearch");
}

TEST_CASE("BwDiscover: parses real SAP BW/4HANA discovery XML", "[adt][bw][discovery]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_discovery_real.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwDiscover(mock);
    REQUIRE(result.IsOk());

    const auto& disc = result.Value();
    REQUIRE(disc.services.size() > 10);

    // Verify key services are found
    bool has_adso = false;
    bool has_search = false;
    bool has_iobj = false;
    for (const auto& s : disc.services) {
        if (s.term == "adso") has_adso = true;
        if (s.term == "bwSearch") has_search = true;
        if (s.term == "iobj") has_iobj = true;
    }
    CHECK(has_adso);
    CHECK(has_search);
    CHECK(has_iobj);
}

TEST_CASE("BwResolveEndpoint: finds service in real discovery", "[adt][bw][discovery]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_discovery_real.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto disc_result = BwDiscover(mock);
    REQUIRE(disc_result.IsOk());

    auto search_result = BwResolveEndpoint(
        disc_result.Value(),
        "http://www.sap.com/bw/modeling/repo", "bwSearch");
    REQUIRE(search_result.IsOk());
    CHECK(search_result.Value().find("bwsearch") != std::string::npos);
}

TEST_CASE("BwResolveEndpoint: not found returns error", "[adt][bw][discovery]") {
    BwDiscoveryResult disc;
    disc.services.push_back({"http://www.sap.com/bw/modeling/adso", "adso",
                              "/sap/bw/modeling/adso", ""});

    auto result = BwResolveEndpoint(disc, "http://www.sap.com/bw/modeling/iobj", "iobj");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}
