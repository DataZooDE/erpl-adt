#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/xml_codec.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;

// ===========================================================================
// Helper: path to test data files
// ===========================================================================

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.rfind('/');
    auto test_dir = this_file.substr(0, last_slash);          // .../test/adt
    auto test_root = test_dir.substr(0, test_dir.rfind('/'));  // .../test
    return test_root + "/testdata/" + filename;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string LoadFixture(const std::string& filename) {
    return ReadFile(TestDataPath(filename));
}

} // anonymous namespace

// ===========================================================================
// Build: BuildPackageCreateXml
// ===========================================================================

TEST_CASE("BuildPackageCreateXml: produces valid XML with correct structure", "[xml][build]") {
    XmlCodec codec;

    auto pkg = PackageName::Create("ZTEST_PKG");
    REQUIRE(pkg.IsOk());

    auto result = codec.BuildPackageCreateXml(pkg.Value(), "Test Package for erpl-adt", "LOCAL");
    REQUIRE(result.IsOk());

    const auto& xml = result.Value();

    // Structural checks: root element, namespaces, key attributes.
    CHECK(xml.find("pak:package") != std::string::npos);
    CHECK(xml.find("xmlns:pak=\"http://www.sap.com/adt/packages\"") != std::string::npos);
    CHECK(xml.find("xmlns:adtcore=\"http://www.sap.com/adt/core\"") != std::string::npos);
    CHECK(xml.find("adtcore:name=\"ZTEST_PKG\"") != std::string::npos);
    CHECK(xml.find("adtcore:description=\"Test Package for erpl-adt\"") != std::string::npos);
    CHECK(xml.find("adtcore:type=\"DEVC/K\"") != std::string::npos);
    CHECK(xml.find("adtcore:version=\"active\"") != std::string::npos);

    // Child elements.
    CHECK(xml.find("adtcore:packageRef") != std::string::npos);
    CHECK(xml.find("pak:attributes") != std::string::npos);
    CHECK(xml.find("pak:superPackage") != std::string::npos);
    CHECK(xml.find("pak:transport") != std::string::npos);
    CHECK(xml.find("pak:softwareComponent") != std::string::npos);
    CHECK(xml.find("pak:name=\"LOCAL\"") != std::string::npos);
}

TEST_CASE("BuildPackageCreateXml: uses custom software component", "[xml][build]") {
    XmlCodec codec;

    auto pkg = PackageName::Create("ZTEST_PKG");
    REQUIRE(pkg.IsOk());

    auto result = codec.BuildPackageCreateXml(pkg.Value(), "desc", "CUSTOM_COMP");
    REQUIRE(result.IsOk());

    CHECK(result.Value().find("pak:name=\"CUSTOM_COMP\"") != std::string::npos);
}

TEST_CASE("BuildPackageCreateXml: empty software component defaults to LOCAL", "[xml][build]") {
    XmlCodec codec;

    auto pkg = PackageName::Create("ZTEST_PKG");
    REQUIRE(pkg.IsOk());

    auto result = codec.BuildPackageCreateXml(pkg.Value(), "desc", "");
    REQUIRE(result.IsOk());

    CHECK(result.Value().find("pak:name=\"LOCAL\"") != std::string::npos);
}

// ===========================================================================
// Build: BuildRepoCloneXml
// ===========================================================================

TEST_CASE("BuildRepoCloneXml: produces valid XML with correct structure", "[xml][build]") {
    XmlCodec codec;

    auto url = RepoUrl::Create("https://github.com/SAP-samples/abap-platform-refscen-flight.git");
    REQUIRE(url.IsOk());
    auto branch = BranchRef::Create("refs/heads/main");
    REQUIRE(branch.IsOk());
    auto pkg = PackageName::Create("ZTEST_PKG");
    REQUIRE(pkg.IsOk());

    auto result = codec.BuildRepoCloneXml(url.Value(), branch.Value(), pkg.Value());
    REQUIRE(result.IsOk());

    const auto& xml = result.Value();

    CHECK(xml.find("abapgitrepo:repository") != std::string::npos);
    CHECK(xml.find("xmlns:abapgitrepo=\"http://www.sap.com/adt/abapgit/repositories\"") != std::string::npos);
    CHECK(xml.find("<abapgitrepo:package>ZTEST_PKG</abapgitrepo:package>") != std::string::npos);
    CHECK(xml.find("<abapgitrepo:url>https://github.com/SAP-samples/abap-platform-refscen-flight.git</abapgitrepo:url>") != std::string::npos);
    CHECK(xml.find("<abapgitrepo:branchName>refs/heads/main</abapgitrepo:branchName>") != std::string::npos);
    CHECK(xml.find("abapgitrepo:transportRequest") != std::string::npos);
    CHECK(xml.find("abapgitrepo:remoteUser") != std::string::npos);
    CHECK(xml.find("abapgitrepo:remotePassword") != std::string::npos);
}

// ===========================================================================
// Build: BuildActivationXml
// ===========================================================================

TEST_CASE("BuildActivationXml: produces valid XML with object references", "[xml][build]") {
    XmlCodec codec;

    std::vector<InactiveObject> objects = {
        {"CLAS/OC", "ZCL_TEST_FLIGHT", "/sap/bc/adt/oo/classes/zcl_test_flight"},
        {"TABL/DT", "ZTEST_FLIGHT_T", "/sap/bc/adt/ddic/tables/ztest_flight_t"},
    };

    auto result = codec.BuildActivationXml(objects);
    REQUIRE(result.IsOk());

    const auto& xml = result.Value();

    CHECK(xml.find("adtcore:objectReferences") != std::string::npos);
    CHECK(xml.find("xmlns:adtcore=\"http://www.sap.com/adt/core\"") != std::string::npos);
    CHECK(xml.find("adtcore:objectReference") != std::string::npos);
    CHECK(xml.find("adtcore:uri=\"/sap/bc/adt/oo/classes/zcl_test_flight\"") != std::string::npos);
    CHECK(xml.find("adtcore:type=\"CLAS/OC\"") != std::string::npos);
    CHECK(xml.find("adtcore:name=\"ZCL_TEST_FLIGHT\"") != std::string::npos);
    CHECK(xml.find("adtcore:name=\"ZTEST_FLIGHT_T\"") != std::string::npos);
}

TEST_CASE("BuildActivationXml: empty objects list produces empty references", "[xml][build]") {
    XmlCodec codec;

    auto result = codec.BuildActivationXml({});
    REQUIRE(result.IsOk());

    const auto& xml = result.Value();
    CHECK(xml.find("adtcore:objectReferences") != std::string::npos);
    // No individual objectReference elements (only the container).
    CHECK(xml.find("adtcore:objectReference ") == std::string::npos);
}

// ===========================================================================
// Parse: ParseDiscoveryResponse (from fixture)
// ===========================================================================

TEST_CASE("ParseDiscoveryResponse: parses fixture correctly", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("discovery_response.xml");

    auto result = codec.ParseDiscoveryResponse(xml);
    REQUIRE(result.IsOk());

    const auto& discovery = result.Value();

    // Should find multiple workspaces.
    CHECK(discovery.workspaces.size() == 5);

    // Should find multiple services across all workspaces.
    auto all_services = discovery.AllServices();
    CHECK(all_services.size() > 5);

    // Capability flags.
    CHECK(discovery.has_abapgit_support);
    CHECK(discovery.has_packages_support);
    CHECK(discovery.has_activation_support);

    // Check workspace titles.
    CHECK(discovery.workspaces[0].title == "Discovery");
    CHECK(discovery.workspaces[1].title == "Object Repository");
    CHECK(discovery.workspaces[2].title == "Sources");
    CHECK(discovery.workspaces[3].title == "Activation");
    CHECK(discovery.workspaces[4].title == "abapGit");

    // Check some known services are present.
    bool found_packages = false;
    bool found_abapgit = false;
    bool found_activation = false;
    for (const auto& svc : all_services) {
        if (svc.href == "/sap/bc/adt/packages") found_packages = true;
        if (svc.href == "/sap/bc/adt/abapgit/repos") found_abapgit = true;
        if (svc.href == "/sap/bc/adt/activation") found_activation = true;
    }
    CHECK(found_packages);
    CHECK(found_abapgit);
    CHECK(found_activation);

    // Check that packages service has type from templateLink.
    for (const auto& svc : discovery.workspaces[1].services) {
        if (svc.href == "/sap/bc/adt/packages") {
            CHECK(svc.type == "application/vnd.sap.adt.packages.v1+xml");
        }
    }
}

TEST_CASE("ParseDiscoveryResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParseDiscoveryResponse("not xml at all <>");
    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "ParseDiscoveryResponse");
}

// ===========================================================================
// Parse: ParsePackageResponse (from fixture)
// ===========================================================================

TEST_CASE("ParsePackageResponse: parses 200 response fixture", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("package_get_200.xml");

    auto result = codec.ParsePackageResponse(xml);
    REQUIRE(result.IsOk());

    const auto& pkg = result.Value();
    CHECK(pkg.name == "ZTEST_PKG");
    CHECK(pkg.description == "Test Package for erpl-adt");
    CHECK(pkg.uri == "/sap/bc/adt/packages/ztest_pkg");
    CHECK(pkg.super_package == "$TMP");
    CHECK(pkg.software_component == "LOCAL");
}

TEST_CASE("ParsePackageResponse: parses create response fixture", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("package_create_response.xml");

    auto result = codec.ParsePackageResponse(xml);
    REQUIRE(result.IsOk());

    const auto& pkg = result.Value();
    CHECK(pkg.name == "ZTEST_PKG");
    CHECK(pkg.description == "Test Package for erpl-adt");
    CHECK(pkg.uri == "/sap/bc/adt/packages/ztest_pkg");
}

TEST_CASE("ParsePackageResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParsePackageResponse("<broken");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Parse: ParseRepoListResponse (from fixture)
// ===========================================================================

TEST_CASE("ParseRepoListResponse: parses fixture with two repos", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("repo_list_response.xml");

    auto result = codec.ParseRepoListResponse(xml);
    REQUIRE(result.IsOk());

    const auto& repos = result.Value();
    REQUIRE(repos.size() == 2);

    // First repo.
    CHECK(repos[0].key == "0242AC1100021EDEB4B4BD0C4F2B8C30");
    CHECK(repos[0].package == "ZTEST_PKG");
    CHECK(repos[0].url == "https://github.com/SAP-samples/abap-platform-refscen-flight.git");
    CHECK(repos[0].branch == "refs/heads/main");
    CHECK(repos[0].status == RepoStatusEnum::Active);
    CHECK(repos[0].status_text == "Active");

    // Second repo.
    CHECK(repos[1].key == "0242AC1100021EDEB4B4BD0C4F2B9D41");
    CHECK(repos[1].package == "ZTEST_SMALL");
    CHECK(repos[1].url == "https://github.com/example/small-abap-repo.git");
    CHECK(repos[1].branch == "refs/heads/main");
    CHECK(repos[1].status == RepoStatusEnum::Inactive);  // "C" maps to Inactive
    CHECK(repos[1].status_text == "Cloned");
}

TEST_CASE("ParseRepoListResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParseRepoListResponse("not xml");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Parse: ParseRepoStatusResponse (from fixture)
// ===========================================================================

TEST_CASE("ParseRepoStatusResponse: parses fixture correctly", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("repo_status_response.xml");

    auto result = codec.ParseRepoStatusResponse(xml);
    REQUIRE(result.IsOk());

    const auto& status = result.Value();
    CHECK(status.key == "0242AC1100021EDEB4B4BD0C4F2B8C30");
    CHECK(status.status == RepoStatusEnum::Active);
    CHECK(status.message == "Active");
}

TEST_CASE("ParseRepoStatusResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParseRepoStatusResponse("");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Parse: ParseActivationResponse (from fixture)
// ===========================================================================

TEST_CASE("ParseActivationResponse: parses fixture with warning", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("activation_response.xml");

    auto result = codec.ParseActivationResponse(xml);
    REQUIRE(result.IsOk());

    const auto& activation = result.Value();
    CHECK(activation.total == 1);
    CHECK(activation.activated == 1);  // Warning counts as activated (not error).
    CHECK(activation.failed == 0);
    REQUIRE(activation.error_messages.size() == 1);
    CHECK(activation.error_messages[0] == "Warning: Some method implementations are empty");
}

TEST_CASE("ParseActivationResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParseActivationResponse("garbage");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Parse: ParseInactiveObjectsResponse (from fixture)
// ===========================================================================

TEST_CASE("ParseInactiveObjectsResponse: parses fixture with 3 objects", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("inactive_objects_response.xml");

    auto result = codec.ParseInactiveObjectsResponse(xml);
    REQUIRE(result.IsOk());

    const auto& objects = result.Value();
    REQUIRE(objects.size() == 3);

    CHECK(objects[0].type == "CLAS/OC");
    CHECK(objects[0].name == "ZCL_TEST_FLIGHT");
    CHECK(objects[0].uri == "/sap/bc/adt/oo/classes/zcl_test_flight");

    CHECK(objects[1].type == "TABL/DT");
    CHECK(objects[1].name == "ZTEST_FLIGHT_T");
    CHECK(objects[1].uri == "/sap/bc/adt/ddic/tables/ztest_flight_t");

    CHECK(objects[2].type == "DDLS/DF");
    CHECK(objects[2].name == "ZTEST_I_FLIGHT");
    CHECK(objects[2].uri == "/sap/bc/adt/ddic/ddl/sources/ztest_i_flight");
}

TEST_CASE("ParseInactiveObjectsResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParseInactiveObjectsResponse("<wrong>");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Parse: ParsePollResponse (from fixtures)
// ===========================================================================

TEST_CASE("ParsePollResponse: running state", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("poll_running.xml");

    auto result = codec.ParsePollResponse(xml);
    REQUIRE(result.IsOk());

    const auto& info = result.Value();
    CHECK(info.state == XmlPollState::Running);
    CHECK(info.message == "Pull repository ZTEST_PKG");
}

TEST_CASE("ParsePollResponse: completed state", "[xml][parse]") {
    XmlCodec codec;
    auto xml = LoadFixture("poll_completed.xml");

    auto result = codec.ParsePollResponse(xml);
    REQUIRE(result.IsOk());

    const auto& info = result.Value();
    CHECK(info.state == XmlPollState::Completed);
    CHECK(info.message == "Pull repository ZTEST_PKG");
}

TEST_CASE("ParsePollResponse: invalid XML returns error", "[xml][parse]") {
    XmlCodec codec;
    auto result = codec.ParsePollResponse("invalid");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Round-trip: Build -> Parse
// ===========================================================================

TEST_CASE("Round-trip: BuildPackageCreateXml -> ParsePackageResponse", "[xml][roundtrip]") {
    XmlCodec codec;

    auto pkg = PackageName::Create("ZTEST_RT");
    REQUIRE(pkg.IsOk());

    auto build_result = codec.BuildPackageCreateXml(
        pkg.Value(), "Round-trip test", "LOCAL");
    REQUIRE(build_result.IsOk());

    auto parse_result = codec.ParsePackageResponse(build_result.Value());
    REQUIRE(parse_result.IsOk());

    const auto& info = parse_result.Value();
    CHECK(info.name == "ZTEST_RT");
    CHECK(info.description == "Round-trip test");
    CHECK(info.super_package == "$TMP");
    CHECK(info.software_component == "LOCAL");
}

TEST_CASE("Round-trip: BuildRepoCloneXml -> ParseRepoListResponse (single repo wrapper)", "[xml][roundtrip]") {
    XmlCodec codec;

    auto url = RepoUrl::Create("https://github.com/test/repo.git");
    REQUIRE(url.IsOk());
    auto branch = BranchRef::Create("refs/heads/main");
    REQUIRE(branch.IsOk());
    auto pkg = PackageName::Create("ZROUND");
    REQUIRE(pkg.IsOk());

    auto build_result = codec.BuildRepoCloneXml(url.Value(), branch.Value(), pkg.Value());
    REQUIRE(build_result.IsOk());

    // The built XML is a single <abapgitrepo:repository> — which is the same
    // element name used in repo status responses.
    auto parse_result = codec.ParseRepoStatusResponse(build_result.Value());

    // ParseRepoStatusResponse can parse a single repo element.
    // The built XML doesn't contain <key> or <status> elements, so those
    // fields will be empty / default.
    REQUIRE(parse_result.IsOk());
    CHECK(parse_result.Value().key.empty());  // Not present in clone request.
}

TEST_CASE("Round-trip: BuildActivationXml -> parse back with tinyxml2", "[xml][roundtrip]") {
    XmlCodec codec;

    std::vector<InactiveObject> input = {
        {"CLAS/OC", "ZCL_A", "/sap/bc/adt/oo/classes/zcl_a"},
        {"TABL/DT", "ZTAB_B", "/sap/bc/adt/ddic/tables/ztab_b"},
        {"DDLS/DF", "ZDDL_C", "/sap/bc/adt/ddic/ddl/sources/zddl_c"},
    };

    auto build_result = codec.BuildActivationXml(input);
    REQUIRE(build_result.IsOk());

    const auto& xml = build_result.Value();

    // Verify all three objects are in the output.
    CHECK(xml.find("adtcore:name=\"ZCL_A\"") != std::string::npos);
    CHECK(xml.find("adtcore:name=\"ZTAB_B\"") != std::string::npos);
    CHECK(xml.find("adtcore:name=\"ZDDL_C\"") != std::string::npos);
    CHECK(xml.find("adtcore:type=\"CLAS/OC\"") != std::string::npos);
    CHECK(xml.find("adtcore:type=\"TABL/DT\"") != std::string::npos);
    CHECK(xml.find("adtcore:type=\"DDLS/DF\"") != std::string::npos);
    CHECK(xml.find("adtcore:uri=\"/sap/bc/adt/oo/classes/zcl_a\"") != std::string::npos);
}

// ===========================================================================
// IXmlCodec polymorphism
// ===========================================================================

TEST_CASE("IXmlCodec: XmlCodec usable through base pointer", "[xml][interface]") {
    auto codec = std::make_unique<XmlCodec>();

    IXmlCodec& iface = *codec;

    auto xml = LoadFixture("poll_completed.xml");
    auto result = iface.ParsePollResponse(xml);
    REQUIRE(result.IsOk());
    CHECK(result.Value().state == XmlPollState::Completed);
}
