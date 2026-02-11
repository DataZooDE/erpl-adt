#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/ddic.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

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

} // anonymous namespace

// ===========================================================================
// ListPackageContents
// ===========================================================================

TEST_CASE("ListPackageContents: parses node structure", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/package_contents.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = ListPackageContents(mock, "ZTEST_PKG");
    REQUIRE(result.IsOk());

    auto& entries = result.Value();
    REQUIRE(entries.size() == 3);

    CHECK(entries[0].object_type == "CLAS/OC");
    CHECK(entries[0].object_name == "ZCL_EXAMPLE");
    CHECK(entries[0].object_uri == "/sap/bc/adt/oo/classes/zcl_example");
    CHECK(entries[0].description == "Example class");
    CHECK(entries[0].expandable);

    CHECK(entries[1].object_type == "PROG/P");
    CHECK(entries[1].object_name == "ZTEST_REPORT");
    CHECK_FALSE(entries[1].expandable);

    CHECK(entries[2].object_type == "DEVC/K");
    CHECK(entries[2].object_name == "ZTEST_SUB");
}

TEST_CASE("ListPackageContents: sends POST with correct params", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
                  "<asx:values><DATA><TREE_CONTENT/></DATA></asx:values></asx:abap>"}));

    auto result = ListPackageContents(mock, "ZMYPKG");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("parent_type=DEVC/K") != std::string::npos);
    CHECK(call.path.find("parent_name=ZMYPKG") != std::string::npos);
    CHECK(call.path.find("withShortDescriptions=true") != std::string::npos);
}

TEST_CASE("ListPackageContents: HTTP error propagated", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = ListPackageContents(mock, "ZTEST");
    REQUIRE(result.IsErr());
}

TEST_CASE("ListPackageContents: empty body returns empty list", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = ListPackageContents(mock, "ZNONEXISTENT");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

// ===========================================================================
// ListPackageTree
// ===========================================================================

namespace {

// Minimal node structure XML with given entries.
std::string MakeNodeStructureXml(const std::vector<std::tuple<std::string, std::string, std::string>>& entries) {
    std::string xml = "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
                      "<asx:values><DATA><TREE_CONTENT>";
    for (const auto& [type, name, uri] : entries) {
        xml += "<SEU_ADT_REPOSITORY_OBJ_NODE>"
               "<OBJECT_TYPE>" + type + "</OBJECT_TYPE>"
               "<OBJECT_NAME>" + name + "</OBJECT_NAME>"
               "<OBJECT_URI>" + uri + "</OBJECT_URI>"
               "<DESCRIPTION>desc</DESCRIPTION>"
               "<EXPANDABLE/>"
               "</SEU_ADT_REPOSITORY_OBJ_NODE>";
    }
    xml += "</TREE_CONTENT></DATA></asx:values></asx:abap>";
    return xml;
}

} // anonymous namespace

TEST_CASE("ListPackageTree: flat package returns entries", "[adt][ddic][tree]") {
    MockAdtSession mock;
    auto xml = MakeNodeStructureXml({
        {"CLAS/OC", "ZCL_A", "/sap/bc/adt/oo/classes/zcl_a"},
        {"PROG/P", "ZREPORT", "/sap/bc/adt/programs/programs/zreport"},
    });
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    PackageTreeOptions opts;
    opts.root_package = "ZTEST";
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);
    CHECK(result.Value()[0].object_name == "ZCL_A");
    CHECK(result.Value()[0].package_name == "ZTEST");
    CHECK(result.Value()[1].object_name == "ZREPORT");
    CHECK(result.Value()[1].package_name == "ZTEST");
}

TEST_CASE("ListPackageTree: recursive into sub-packages", "[adt][ddic][tree]") {
    MockAdtSession mock;

    // Root package has a class and a sub-package.
    auto root_xml = MakeNodeStructureXml({
        {"CLAS/OC", "ZCL_ROOT", "/sap/bc/adt/oo/classes/zcl_root"},
        {"DEVC/K", "ZSUB", "/sap/bc/adt/packages/zsub"},
    });
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, root_xml}));

    // Sub-package has a program.
    auto sub_xml = MakeNodeStructureXml({
        {"PROG/P", "ZSUB_REPORT", "/sap/bc/adt/programs/programs/zsub_report"},
    });
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, sub_xml}));

    PackageTreeOptions opts;
    opts.root_package = "ZROOT";
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);

    CHECK(result.Value()[0].object_name == "ZCL_ROOT");
    CHECK(result.Value()[0].package_name == "ZROOT");
    CHECK(result.Value()[1].object_name == "ZSUB_REPORT");
    CHECK(result.Value()[1].package_name == "ZSUB");
}

TEST_CASE("ListPackageTree: type filter", "[adt][ddic][tree]") {
    MockAdtSession mock;
    auto xml = MakeNodeStructureXml({
        {"CLAS/OC", "ZCL_A", "/sap/bc/adt/oo/classes/zcl_a"},
        {"TABL/DT", "ZTABLE", "/sap/bc/adt/ddic/tables/ztable"},
        {"PROG/P", "ZREPORT", "/sap/bc/adt/programs/programs/zreport"},
    });
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    PackageTreeOptions opts;
    opts.root_package = "ZTEST";
    opts.type_filter = "CLAS";
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].object_name == "ZCL_A");
}

TEST_CASE("ListPackageTree: max depth prevents deep recursion", "[adt][ddic][tree]") {
    MockAdtSession mock;

    // Package at depth 0 has a sub-package.
    auto root_xml = MakeNodeStructureXml({
        {"DEVC/K", "ZSUB", "/sap/bc/adt/packages/zsub"},
    });
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, root_xml}));

    // max_depth=1 means we don't traverse ZSUB.
    PackageTreeOptions opts;
    opts.root_package = "ZROOT";
    opts.max_depth = 1;
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
    // Only 1 POST call (root), not 2 (would have been 2 if ZSUB was traversed).
    CHECK(mock.PostCallCount() == 1);
}

TEST_CASE("ListPackageTree: empty package returns empty", "[adt][ddic][tree]") {
    MockAdtSession mock;
    auto xml = MakeNodeStructureXml({});
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    PackageTreeOptions opts;
    opts.root_package = "ZEMPTY";
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ListPackageTree: error propagated", "[adt][ddic][tree]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    PackageTreeOptions opts;
    opts.root_package = "ZTEST";
    auto result = ListPackageTree(mock, opts);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// GetTableDefinition
// ===========================================================================

TEST_CASE("GetTableDefinition: parses SFLIGHT table", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = GetTableDefinition(mock, "SFLIGHT");
    REQUIRE(result.IsOk());

    auto& table = result.Value();
    CHECK(table.name == "SFLIGHT");
    CHECK(table.description == "Flight schedule");
    CHECK(table.delivery_class == "A");

    REQUIRE(table.fields.size() == 8);
    CHECK(table.fields[0].name == "MANDT");
    CHECK(table.fields[0].type == "CLNT");
    CHECK(table.fields[0].key_field);

    CHECK(table.fields[4].name == "PRICE");
    CHECK(table.fields[4].type == "S_PRICE");
    CHECK_FALSE(table.fields[4].key_field);
}

TEST_CASE("GetTableDefinition: 404 returns NotFound", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = GetTableDefinition(mock, "ZNONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("GetTableDefinition: sends correct URI", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<tabl:table xmlns:tabl=\"http://www.sap.com/adt/ddic/tables\" "
                  "xmlns:adtcore=\"http://www.sap.com/adt/core\" "
                  "adtcore:name=\"MARA\"/>"}));

    auto result = GetTableDefinition(mock, "MARA");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/ddic/tables/MARA");
}

// ===========================================================================
// GetCdsSource
// ===========================================================================

TEST_CASE("GetCdsSource: returns CDS source text", "[adt][ddic]") {
    MockAdtSession mock;
    std::string cds_source = "@AbapCatalog.sqlViewName: 'ZSQL_VIEW'\n"
                             "define view ZCDS_TEST as select from sflight {\n"
                             "  key carrid,\n"
                             "  key connid\n"
                             "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, cds_source}));

    auto result = GetCdsSource(mock, "ZCDS_TEST");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == cds_source);
}

TEST_CASE("GetCdsSource: sends correct URI", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "source"}));

    auto result = GetCdsSource(mock, "ZCDS_VIEW");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/ddic/ddl/sources/ZCDS_VIEW/source/main");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "text/plain");
}

TEST_CASE("GetCdsSource: 404 returns NotFound", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = GetCdsSource(mock, "ZNONEXISTENT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("GetCdsSource: HTTP error propagated", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));

    auto result = GetCdsSource(mock, "ZCDS_VIEW");
    REQUIRE(result.IsErr());
}
