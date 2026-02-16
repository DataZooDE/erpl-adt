#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_discovery.hpp>
#include <erpl_adt/adt/bw_endpoint_resolver.hpp>
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

} // namespace

TEST_CASE("BwExpandUriTemplate: expands path and query variables", "[adt][bw][resolver]") {
    BwTemplateParams path{
        {"objectType", "ADSO"},
        {"objectName", "ZSALES"},
    };
    BwTemplateParams query{
        {"childName", "TRFN_ZSALES"},
        {"childType", "TRFN"},
    };

    auto out = BwExpandUriTemplate(
        "/sap/bw/modeling/repo/infoproviderstructure/{objectType}/{objectName}{?childName,childType}",
        path, query);

    CHECK(out == "/sap/bw/modeling/repo/infoproviderstructure/ADSO/ZSALES?childName=TRFN_ZSALES&childType=TRFN");
}

TEST_CASE("BwResolveAndExpandEndpoint: resolves and expands from discovery", "[adt][bw][resolver]") {
    BwDiscoveryResult disc;
    disc.services.push_back({
        "http://www.sap.com/bw/modeling/repo",
        "bwSearch",
        "/sap/bw/modeling/repo/is/bwsearch{?searchTerm,maxSize,objectType}",
        "application/atom+xml",
    });

    BwTemplateParams path;
    BwTemplateParams query{
        {"searchTerm", "Z*"},
        {"maxSize", "25"},
        {"objectType", "ADSO"},
    };
    auto result = BwResolveAndExpandEndpoint(
        disc, "http://www.sap.com/bw/modeling/repo", "bwSearch", path, query);

    REQUIRE(result.IsOk());
    CHECK(result.Value() == "/sap/bw/modeling/repo/is/bwsearch?searchTerm=Z%2A&maxSize=25&objectType=ADSO");
}

TEST_CASE("BwDiscoverResolveAndExpandEndpoint: works with real discovery fixture", "[adt][bw][resolver]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_discovery_real.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwTemplateParams path{
        {"objectType", "ADSO"},
        {"objectName", "ZSALES"},
    };
    BwTemplateParams query{
        {"childType", "TRFN"},
    };

    auto result = BwDiscoverResolveAndExpandEndpoint(
        mock,
        "http://www.sap.com/bw/modeling/repo",
        "nodes",
        path,
        query);

    REQUIRE(result.IsOk());
    CHECK(result.Value().find("/sap/bw/modeling/repo/infoproviderstructure/ADSO/ZSALES") != std::string::npos);
}
