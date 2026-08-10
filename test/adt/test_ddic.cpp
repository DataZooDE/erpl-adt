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

TEST_CASE("ListPackageContents: empty-body + proven absence = NotFound",
          "[adt][ddic]") {
    // SAP's nodestructure returns HTTP 200 with an empty body for BOTH
    // "package empty" and "package missing". When empty, existence is checked
    // separately: 404 from the package resource AND no search hit = missing.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {},
         R"(<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core"/>)"}));

    auto result = ListPackageContents(mock, "ZGHOST_PKG");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(result.Error().message.find("does not exist") != std::string::npos);
    CHECK(result.Error().message.find("ZGHOST_PKG") != std::string::npos);
}

TEST_CASE("ListPackageContents: empty package on 7.40 is empty, not missing",
          "[adt][ddic]") {
    // GitHub issue #35: SAP_BASIS 7.40 has no /sap/bc/adt/packages/{name}
    // resource, so its 404 must not turn a childless package into
    // "does not exist". The search hit proves the package is there.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {},
         R"(<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">)"
         R"(<adtcore:objectReference adtcore:type="DEVC/K" adtcore:name="ZEMPTY_PKG"/>)"
         R"(</adtcore:objectReferences>)"}));

    auto result = ListPackageContents(mock, "ZEMPTY_PKG");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ListPackageContents: existence check skipped when not requested",
          "[adt][ddic]") {
    // Packages discovered from a parent listing provably exist; re-verifying
    // each empty leaf would cost a round trip per package.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = ListPackageContents(mock, "ZKNOWN_PKG", /*verify_existence=*/false);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
    CHECK(mock.GetCallCount() == 0);
}

TEST_CASE("ListPackageContents: empty-body + 200 on package probe = empty list",
          "[adt][ddic]") {
    // Genuinely empty but existing package — must still return [].
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<package/>"}));

    auto result = ListPackageContents(mock, "ZEMPTY_PKG");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ListPackageContents: empty-body + probe error = best-effort empty list",
          "[adt][ddic]") {
    // If the probe fails (transient network), don't pretend the package is
    // missing — fall through and return the empty list we already parsed.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "transient", std::nullopt}));

    auto result = ListPackageContents(mock, "ZSOME_PKG");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
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

TEST_CASE("GetTableDefinition: parses length and decimals", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = GetTableDefinition(mock, "SFLIGHT");
    REQUIRE(result.IsOk());

    auto& table = result.Value();
    REQUIRE(table.fields.size() == 8);

    // MANDT: length=3, no decimals
    REQUIRE(table.fields[0].length.has_value());
    CHECK(*table.fields[0].length == 3);
    CHECK_FALSE(table.fields[0].decimals.has_value());

    // PRICE: length=15, decimals=2
    REQUIRE(table.fields[4].length.has_value());
    CHECK(*table.fields[4].length == 15);
    REQUIRE(table.fields[4].decimals.has_value());
    CHECK(*table.fields[4].decimals == 2);
}

TEST_CASE("GetTableDefinition: fields without length have empty optional", "[adt][ddic]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<tabl:table xmlns:tabl=\"http://www.sap.com/adt/ddic/tables\" "
                  "xmlns:adtcore=\"http://www.sap.com/adt/core\" "
                  "adtcore:name=\"MARA\">"
                  "<tabl:field adtcore:name=\"MATNR\" tabl:type=\"MATNR\"/>"
                  "</tabl:table>"}));

    auto result = GetTableDefinition(mock, "MARA");
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().fields.size() == 1);
    CHECK_FALSE(result.Value().fields[0].length.has_value());
    CHECK_FALSE(result.Value().fields[0].decimals.has_value());
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

// ===========================================================================
// GetTableDefinition: blue:blueSource fallback (TABL/DT DDL source format)
// ===========================================================================

TEST_CASE("GetTableDefinition: blueSource falls back to DDL source", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    auto ddl = LoadFixture("ddic/table_sflight_source.abap");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "SFLIGHT", /*resolve_types=*/false);
    REQUIRE(result.IsOk());

    auto& table = result.Value();
    CHECK(table.name == "SFLIGHT");
    CHECK(table.description == "Flug");

    // 14 fields: mandt carrid connid fldate price currency planetype
    //            seatsmax seatsocc paymentsum seatsmax_b seatsocc_b seatsmax_f seatsocc_f
    REQUIRE(table.fields.size() == 14);

    CHECK(table.fields[0].name == "mandt");
    CHECK(table.fields[0].type == "s_mandt");
    CHECK(table.fields[0].key_field);

    CHECK(table.fields[1].name == "carrid");
    CHECK(table.fields[1].key_field);

    CHECK(table.fields[3].name == "fldate");
    CHECK(table.fields[3].key_field);

    CHECK(table.fields[4].name == "price");
    CHECK(table.fields[4].type == "s_price");
    CHECK_FALSE(table.fields[4].key_field);

    // Second GET goes to /source/main
    REQUIRE(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[1].path == "/sap/bc/adt/ddic/tables/SFLIGHT/source/main");
    CHECK(mock.GetCalls()[1].headers.at("Accept") == "text/plain");
}

TEST_CASE("GetTableDefinition: blueSource DDL source fetch failure -> empty fields returned", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = GetTableDefinition(mock, "SFLIGHT", /*resolve_types=*/false);
    REQUIRE(result.IsOk());
    CHECK(result.Value().name == "SFLIGHT");
    CHECK(result.Value().fields.empty());
}

// ===========================================================================
// GetTableDefinition: data element type resolution (resolve_types=true)
// ===========================================================================

namespace {

std::string MakeDataElementXml(const std::string& name,
                                const std::string& description,
                                int length,
                                int decimals = 0,
                                const std::string& abap_type = "") {
    std::string dtel_body =
        "<dtel:dataTypeLength>" + std::to_string(length) + "</dtel:dataTypeLength>"
        "<dtel:dataTypeDecimals>" + std::to_string(decimals) + "</dtel:dataTypeDecimals>";
    if (!abap_type.empty())
        dtel_body += "<dtel:dataType>" + abap_type + "</dtel:dataType>";
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<blue:wbobj adtcore:name=\"" + name + "\""
           " adtcore:description=\"" + description + "\""
           " xmlns:blue=\"http://www.sap.com/wbobj/dictionary/dtel\""
           " xmlns:adtcore=\"http://www.sap.com/adt/core\">"
           "<dtel:dataElement xmlns:dtel=\"http://www.sap.com/adt/dictionary/dataelements\">"
           + dtel_body +
           "</dtel:dataElement>"
           "</blue:wbobj>";
}

} // anonymous namespace

TEST_CASE("GetTableDefinition: resolve_types fetches data elements for DDL fields",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt  : s_mandt not null;\n"
        "      carrid : s_carr_id not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));       // table XML
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));       // DDL source
    // Data elements are fetched in alphabetical order (std::set iteration).
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(                         // s_carr_id (alpha-first)
        {200, {}, MakeDataElementXml("S_CARR_ID", "Airline code", 3)}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(                         // s_mandt (alpha-second)
        {200, {}, MakeDataElementXml("S_MANDT", "Client", 3)}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/true);
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 2);

    // mandt: length and description from data element
    REQUIRE(fields[0].length.has_value());
    CHECK(*fields[0].length == 3);
    CHECK(fields[0].description == "Client");

    // carrid: same
    REQUIRE(fields[1].length.has_value());
    CHECK(*fields[1].length == 3);
    CHECK(fields[1].description == "Airline code");

    // 4 GET calls total: table + DDL source + 2 data elements
    CHECK(mock.GetCallCount() == 4);
}

TEST_CASE("GetTableDefinition: resolve_types resolves description from a real captured "
          "data-element response",
          "[adt][ddic][resolve]") {
    // Regression test: the synthetic MakeDataElementXml fixture above passed
    // even when EnrichFieldsFromDataElements' live requests were silently
    // failing end-to-end (missing/wrong Accept header — see ddic.cpp) — a
    // real captured response is what actually caught it.
    MockAdtSession mock;
    auto table_xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt : as4local not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, table_xml}));  // table XML
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));        // DDL source
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/dataelement_as4local.xml")}));           // real captured

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/true);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().fields.size() == 1);
    CHECK(result.Value().fields[0].description == "Aktivierungsstand eines Repository-Objektes");
    REQUIRE(result.Value().fields[0].length.has_value());
    CHECK(*result.Value().fields[0].length == 1);
}

TEST_CASE("GetTableDefinition: resolve_types=false skips data element fetches",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt  : s_mandt not null;\n"
        "      carrid : s_carr_id not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/false);
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 2);
    CHECK_FALSE(fields[0].length.has_value());
    CHECK(fields[0].description.empty());
    CHECK_FALSE(fields[1].length.has_value());

    // Only 2 GET calls: table + DDL source, no data element lookups
    CHECK(mock.GetCallCount() == 2);
}

TEST_CASE("GetTableDefinition: resolve_types extracts length from abap.* built-in types",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt  : s_mandt not null;\n"
        "      amount : abap.curr(15,2);\n"
        "      descr  : abap.sstring(255);\n"
        "      cnt    : abap.int4;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));
    // Only s_mandt needs a data element lookup; abap.* types don't
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, MakeDataElementXml("S_MANDT", "Client", 3)}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/true);
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 4);

    // abap.curr(15,2) → length=15, decimals=2
    REQUIRE(fields[1].length.has_value());
    CHECK(*fields[1].length == 15);
    REQUIRE(fields[1].decimals.has_value());
    CHECK(*fields[1].decimals == 2);

    // abap.sstring(255) → length=255
    REQUIRE(fields[2].length.has_value());
    CHECK(*fields[2].length == 255);

    // abap.int4 (no params) → length stays empty
    CHECK_FALSE(fields[3].length.has_value());

    // 3 GET calls: table + DDL + s_mandt only (no calls for abap.* types)
    CHECK(mock.GetCallCount() == 3);
}

TEST_CASE("GetTableDefinition: resolve_types deduplicates repeated data element types",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt  : s_mandt not null;\n"
        "      f1     : s_mandt not null;\n"  // same type as mandt
        "      f2     : s_mandt not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));
    // Only one data element call for s_mandt (deduplicated)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, MakeDataElementXml("S_MANDT", "Client", 3)}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/true);
    REQUIRE(result.IsOk());

    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 3);
    // All three fields share the same data element → all get enriched
    for (const auto& f : fields) {
        REQUIRE(f.length.has_value());
        CHECK(*f.length == 3);
        CHECK(f.description == "Client");
    }

    // 3 GET calls: table + DDL + s_mandt once (not 3 times)
    CHECK(mock.GetCallCount() == 3);
}

TEST_CASE("GetTableDefinition: resolve_types is default (no third arg = resolve)",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl = "define table ztest {\n  key mandt : s_mandt not null;\n}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, MakeDataElementXml("S_MANDT", "Client", 3)}));

    // No third argument → should default to resolve_types=true
    auto result = GetTableDefinition(mock, "ZTEST");
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().fields.size() == 1);
    REQUIRE(result.Value().fields[0].length.has_value());
    CHECK(*result.Value().fields[0].length == 3);
}

TEST_CASE("GetTableDefinition: blueSource DDL with abap built-in dotted types", "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    // DDL with abap.* built-in types — previously truncated to "abap"; now
    // resolve_types=false so no data element calls, just type parsing.
    std::string ddl =
        "@AbapCatalog.tableCategory : #TRANSPARENT\n"
        "define table ztypes_test {\n"
        "  key mandt   : s_mandt not null;\n"
        "      descr   : abap.sstring(255);\n"
        "      raw_val : abap.rawstring(0);\n"
        "      amount  : abap.curr(15,2);\n"
        "      counter : abap.int4;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "ZTYPES_TEST", /*resolve_types=*/false);
    REQUIRE(result.IsOk());

    auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 5);

    CHECK(fields[0].name == "mandt");
    CHECK(fields[0].type == "s_mandt");
    CHECK(fields[0].key_field);

    CHECK(fields[1].name == "descr");
    CHECK(fields[1].type == "abap.sstring(255)");
    CHECK_FALSE(fields[1].key_field);

    CHECK(fields[2].name == "raw_val");
    CHECK(fields[2].type == "abap.rawstring(0)");

    CHECK(fields[3].name == "amount");
    CHECK(fields[3].type == "abap.curr(15,2)");

    CHECK(fields[4].name == "counter");
    CHECK(fields[4].type == "abap.int4");
}

TEST_CASE("GetTableDefinition: blueSource delivery_class parsed from DDL annotation",
          "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    auto ddl = LoadFixture("ddic/table_sflight_source.abap");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "SFLIGHT", /*resolve_types=*/false);
    REQUIRE(result.IsOk());
    CHECK(result.Value().delivery_class == "A");
}

TEST_CASE("GetTableDefinition: blueSource DDL without deliveryClass leaves it empty",
          "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key mandt : s_mandt not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/false);
    REQUIRE(result.IsOk());
    CHECK(result.Value().delivery_class.empty());
}

TEST_CASE("GetTableDefinition: blueSource DDL source parses check_table from FK clause",
          "[adt][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    auto ddl = LoadFixture("ddic/table_sflight_source.abap");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));

    auto result = GetTableDefinition(mock, "SFLIGHT", /*resolve_types=*/false);
    REQUIRE(result.IsOk());
    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 14);

    CHECK(fields[0].name == "mandt");
    CHECK(fields[0].check_table == "T000");   // with foreign key [0..*,1] t000

    CHECK(fields[1].name == "carrid");
    CHECK(fields[1].check_table == "SCARR");  // with foreign key scarr (no brackets)

    CHECK(fields[2].name == "connid");
    CHECK(fields[2].check_table == "SPFLI");

    CHECK(fields[3].name == "fldate");
    CHECK(fields[3].check_table.empty());     // no FK

    CHECK(fields[5].name == "currency");
    CHECK(fields[5].check_table == "SCURX");

    CHECK(fields[6].name == "planetype");
    CHECK(fields[6].check_table == "SAPLANE");
}

TEST_CASE("GetTableDefinition: resolve_types extracts abap_type from data element",
          "[adt][ddic][resolve]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight_bluesource.xml");
    std::string ddl =
        "define table ztest {\n"
        "  key carrid : s_carr_id not null;\n"
        "}\n";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ddl}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, MakeDataElementXml("S_CARR_ID", "Airline code", 3, 0, "CHAR")}));

    auto result = GetTableDefinition(mock, "ZTEST", /*resolve_types=*/true);
    REQUIRE(result.IsOk());
    const auto& fields = result.Value().fields;
    REQUIRE(fields.size() == 1);
    CHECK(fields[0].abap_type == "CHAR");
    CHECK(fields[0].description == "Airline code");
}
