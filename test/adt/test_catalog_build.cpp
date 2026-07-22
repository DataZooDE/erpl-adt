#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_build.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <algorithm>
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

const char* kEmptyTree =
    "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
    "<asx:values><DATA><TREE_CONTENT></TREE_CONTENT></DATA></asx:values></asx:abap>";

const char* kMixedDomainTree =
    "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
    "<asx:values><DATA><TREE_CONTENT>"
    "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>TABL/DT</OBJECT_TYPE>"
    "<OBJECT_NAME>SFLIGHT</OBJECT_NAME><OBJECT_URI>/sap/bc/adt/ddic/tables/sflight</OBJECT_URI>"
    "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Flight schedule</DESCRIPTION></SEU_ADT_REPOSITORY_OBJ_NODE>"
    "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>DDLS/DF</OBJECT_TYPE>"
    "<OBJECT_NAME>I_ABAPAPPLCOMPTEXT</OBJECT_NAME>"
    "<OBJECT_URI>/sap/bc/adt/ddic/ddl/sources/i_abapapplcomptext</OBJECT_URI>"
    "<EXPANDABLE></EXPANDABLE><DESCRIPTION>ABAP Application Component Text</DESCRIPTION>"
    "</SEU_ADT_REPOSITORY_OBJ_NODE>"
    "</TREE_CONTENT></DATA></asx:values></asx:abap>";

} // anonymous namespace

TEST_CASE("CatalogBuild: ABAP-only package produces entities with no fields",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/package_contents.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTree}));  // ZTEST_SUB

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    REQUIRE(feed.entities.size() == 2);
    CHECK(feed.fields.empty());
    CHECK(feed.warnings.empty());

    bool found_clas = false, found_prog = false;
    for (const auto& e : feed.entities) {
        CHECK(e.domain == CatalogDomain::Abap);
        CHECK(e.system_sid == "A4H");
        if (e.technical_name == "ZCL_EXAMPLE") {
            found_clas = true;
            CHECK(e.display_name == "Example class");
        }
        if (e.technical_name == "ZTEST_REPORT") found_prog = true;
    }
    CHECK(found_clas);
    CHECK(found_prog);
}

TEST_CASE("CatalogBuild: same scope produces stable entity IDs across two builds",
          "[adt][catalog][build]") {
    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    MockAdtSession mock1;
    mock1.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/package_contents.xml")}));
    mock1.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTree}));
    auto r1 = CatalogBuild(mock1, options);
    REQUIRE(r1.IsOk());

    MockAdtSession mock2;
    mock2.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/package_contents.xml")}));
    mock2.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTree}));
    auto r2 = CatalogBuild(mock2, options);
    REQUIRE(r2.IsOk());

    REQUIRE(r1.Value().entities.size() == r2.Value().entities.size());
    for (size_t i = 0; i < r1.Value().entities.size(); ++i) {
        CHECK(r1.Value().entities[i].id.Value() == r2.Value().entities[i].id.Value());
    }
}

const char* kSflightDdlTree =
    "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
    "<asx:values><DATA><TREE_CONTENT>"
    "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>TABL/DT</OBJECT_TYPE>"
    "<OBJECT_NAME>SFLIGHT</OBJECT_NAME><OBJECT_URI>/sap/bc/adt/ddic/tables/sflight</OBJECT_URI>"
    "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Flight schedule</DESCRIPTION></SEU_ADT_REPOSITORY_OBJ_NODE>"
    "</TREE_CONTENT></DATA></asx:values></asx:abap>";

TEST_CASE("CatalogBuild: DDL-sourced table gets is_key fields and fk edges to check tables",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kSflightDdlTree}));
    // GetTableDefinition(SFLIGHT): blueSource XML then DDL source fallback.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight_bluesource.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    const CatalogEntity* sflight = nullptr;
    for (const auto& e : feed.entities) {
        if (e.technical_name == "SFLIGHT") sflight = &e;
    }
    REQUIRE(sflight != nullptr);

    bool found_key_field = false;
    bool found_fk_field = false;
    for (const auto& f : feed.fields) {
        if (f.entity_id.Value() != sflight->id.Value()) continue;
        if (f.name == "mandt") {
            found_key_field = true;
            CHECK(f.is_key);
        }
        if (f.name == "carrid") {
            found_fk_field = true;
            REQUIRE(f.check_table.has_value());
            CHECK(*f.check_table == "SCARR");
        }
    }
    CHECK(found_key_field);
    CHECK(found_fk_field);

    bool found_fk_edge = false;
    for (const auto& edge : feed.edges) {
        if (edge.kind == "fk" && edge.from_id.Value() == sflight->id.Value()) {
            found_fk_edge = true;
        }
    }
    CHECK(found_fk_edge);
}

TEST_CASE("CatalogBuild: --resolve-ddic-types surfaces a domain's fixed values on the field",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kSflightDdlTree}));
    // GetTableDefinition(SFLIGHT): blueSource XML then DDL source fallback.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight_bluesource.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "define table sflight {\n  flag_field : zflag_de;\n}"}));
    // EnrichFieldsFromDataElements(ZFLAG_DE) — shape confirmed live against
    // A4H (typeKind=domain, typeName=<domain>), reused here with XFELD as
    // the referenced domain so the domain fetch below can use the real
    // captured ddic/domain_xfeld.xml fixture.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {},
         "<?xml version=\"1.0\"?><blue:wbobj xmlns:blue=\"http://www.sap.com/wbobj/dictionary/dtel\" "
         "xmlns:adtcore=\"http://www.sap.com/adt/core\" adtcore:description=\"Test flag\">"
         "<dtel:dataElement xmlns:dtel=\"http://www.sap.com/adt/dictionary/dataelements\">"
         "<dtel:typeKind>domain</dtel:typeKind><dtel:typeName>XFELD</dtel:typeName>"
         "<dtel:dataType>CHAR</dtel:dataType><dtel:dataTypeLength>000001</dtel:dataTypeLength>"
         "</dtel:dataElement></blue:wbobj>"}));
    // GetDomain(XFELD) — real captured fixture with 2 fixed values (X=Ja, ""=Nein).
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/domain_xfeld.xml")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};
    options.resolve_ddic_types = true;

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    bool found = false;
    for (const auto& f : feed.fields) {
        if (f.name != "flag_field") continue;
        found = true;
        REQUIRE(f.fixed_values_json.has_value());
        CHECK(f.fixed_values_json->find(R"("low":"X")") != std::string::npos);
        CHECK(f.fixed_values_json->find(R"("text":"Ja")") != std::string::npos);
        CHECK(f.fixed_values_json->find(R"("text":"Nein")") != std::string::npos);
    }
    CHECK(found);
}

TEST_CASE("CatalogBuild: mixed TABL/DDLS package resolves fields for both domains",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    // GetTableDefinition(SFLIGHT)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));
    // GetCdsStructure(I_ABAPAPPLCOMPTEXT)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    // 2 real entities (SFLIGHT, I_ABAPAPPLCOMPTEXT) + 2 stub entities for
    // the CDS view's out-of-scope association targets
    // (I_ABAPApplicationComponent, I_Language).
    REQUIRE(feed.entities.size() == 4);

    const CatalogEntity* tabl_entity = nullptr;
    const CatalogEntity* cds_entity = nullptr;
    for (const auto& e : feed.entities) {
        if (e.technical_name == "SFLIGHT") tabl_entity = &e;
        if (e.technical_name == "I_ABAPAPPLCOMPTEXT") cds_entity = &e;
    }
    REQUIRE(tabl_entity != nullptr);
    REQUIRE(cds_entity != nullptr);
    CHECK(tabl_entity->domain == CatalogDomain::Ddic);
    CHECK(cds_entity->domain == CatalogDomain::Cds);
    REQUIRE(cds_entity->source_table.has_value());
    CHECK(*cds_entity->source_table == "df14t");

    bool has_sflight_field = false;
    bool has_cds_field = false;
    for (const auto& f : feed.fields) {
        if (f.entity_id.Value() == tabl_entity->id.Value()) has_sflight_field = true;
        if (f.entity_id.Value() == cds_entity->id.Value()) has_cds_field = true;
    }
    CHECK(has_sflight_field);
    CHECK(has_cds_field);
}

TEST_CASE("CatalogBuild: CDS fields carry source_expression/annotations, "
          "associations become edges",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    const CatalogEntity* cds_entity = nullptr;
    for (const auto& e : feed.entities) {
        if (e.technical_name == "I_ABAPAPPLCOMPTEXT") cds_entity = &e;
    }
    REQUIRE(cds_entity != nullptr);

    bool found_field_with_expression = false;
    bool found_field_with_annotations = false;
    for (const auto& f : feed.fields) {
        if (f.entity_id.Value() != cds_entity->id.Value()) continue;
        if (f.name == "ABAPApplicationComponentName") {
            found_field_with_expression = true;
            REQUIRE(f.source_expression.has_value());
            CHECK(*f.source_expression == "name");
        }
        if (f.name == "Language") {
            found_field_with_annotations = true;
            REQUIRE(f.annotations_json.has_value());
            CHECK(f.annotations_json->find("Semantics.language") != std::string::npos);
        }
    }
    CHECK(found_field_with_expression);
    CHECK(found_field_with_annotations);

    int association_edges = 0;
    bool found_language_edge = false;
    for (const auto& edge : feed.edges) {
        if (edge.kind != "association" || edge.from_id.Value() != cds_entity->id.Value()) continue;
        association_edges++;
        REQUIRE(edge.detail_json.has_value());
        if (edge.detail_json->find(R"("cardinality":"[0..1]")") != std::string::npos) {
            found_language_edge = true;
        }
    }
    CHECK(association_edges == 2);
    CHECK(found_language_edge);
}

TEST_CASE("CatalogBuild: include_raw_source stashes each object's raw source into raw_json",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    // MaybeAttachRawSource(SFLIGHT) — fetched before GetTableDefinition.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "table ztest_sflight { ... }"}));
    // GetTableDefinition(SFLIGHT)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));
    // MaybeAttachRawSource(I_ABAPAPPLCOMPTEXT) — fetched before GetCdsStructure.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "define view I_AbapApplComp ..."}));
    // GetCdsStructure(I_ABAPAPPLCOMPTEXT)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};
    options.include_raw_source = true;

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();
    REQUIRE(feed.entities.size() == 4);  // 2 real + 2 CDS association stubs

    for (const auto& e : feed.entities) {
        if (e.technical_name == "SFLIGHT") {
            CHECK(e.raw_json == "table ztest_sflight { ... }");
        }
        if (e.technical_name == "I_ABAPAPPLCOMPTEXT") {
            CHECK(e.raw_json == "define view I_AbapApplComp ...");
        }
    }
}

TEST_CASE("CatalogBuild: include_raw_source off by default leaves raw_json empty",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    for (const auto& e : result.Value().entities) {
        CHECK(e.raw_json.empty());
    }
}

TEST_CASE("CatalogBuild: an unresolvable object is recorded as a warning, not a hard failure",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    // GetTableDefinition(SFLIGHT) fails
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    // GetCdsStructure(I_ABAPAPPLCOMPTEXT) still succeeds
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    // 2 real entities (both still created despite the TABL failure) + 2 CDS
    // association stubs.
    REQUIRE(feed.entities.size() == 4);
    CHECK_FALSE(feed.warnings.empty());
}

TEST_CASE("CatalogBuild: a connection failure aborts the whole build instead of "
          "degrading to a warning",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(Error{
        "ListPackageContents", "/sap/bc/adt/repository/nodestructure", std::nullopt,
        "Connection refused", std::nullopt, ErrorCategory::Connection}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Connection);
}

TEST_CASE("CatalogBuild: fields get a stable, deterministic id",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kMixedDomainTree}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    for (const auto& field : result.Value().fields) {
        CHECK_FALSE(field.id.empty());
        CHECK(field.id == field.entity_id.Value() + "#" + field.name);
    }
}

TEST_CASE("CatalogBuild: overlapping package scopes do not duplicate entities",
          "[adt][catalog][build]") {
    MockAdtSession mock;
    // Same package listed twice as an explicit scope (e.g. via a package and
    // one of its ancestors both being passed) — each walk independently
    // rediscovers the same 2 objects (+ empty sub-package), but the final
    // feed must contain each entity only once.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/package_contents.xml")}));  // ZTEST, pass 1
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTree}));  // ZTEST_SUB, pass 1
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/package_contents.xml")}));  // ZTEST, pass 2
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTree}));  // ZTEST_SUB, pass 2

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST", "ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().entities.size() == 2);  // not 4
}

TEST_CASE("CatalogBuild: a function-module 'uses' edge reconciles onto the real entity "
          "instead of a duplicate stub, when both are discovered in the same build",
          "[adt][catalog][build]") {
    const char* kTableAndFugrTree =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
        "<asx:values><DATA><TREE_CONTENT>"
        "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>TABL/DT</OBJECT_TYPE>"
        "<OBJECT_NAME>SFLIGHT</OBJECT_NAME>"
        "<OBJECT_URI>/sap/bc/adt/ddic/tables/sflight</OBJECT_URI>"
        "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Flight schedule</DESCRIPTION>"
        "</SEU_ADT_REPOSITORY_OBJ_NODE>"
        "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>FUGR/F</OBJECT_TYPE>"
        "<OBJECT_NAME>ZFG</OBJECT_NAME>"
        "<OBJECT_URI>/sap/bc/adt/functions/groups/zfg</OBJECT_URI>"
        "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Test function group</DESCRIPTION>"
        "</SEU_ADT_REPOSITORY_OBJ_NODE>"
        "</TREE_CONTENT></DATA></asx:values></asx:abap>";
    const char* kZfgFmodules =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
        "<asx:values><DATA><TREE_CONTENT>"
        "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>FUGR/FF</OBJECT_TYPE>"
        "<OBJECT_NAME>Z_FM</OBJECT_NAME>"
        "<OBJECT_URI>/sap/bc/adt/functions/groups/zfg/fmodules/z_fm</OBJECT_URI>"
        "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Test FM</DESCRIPTION>"
        "</SEU_ADT_REPOSITORY_OBJ_NODE>"
        "</TREE_CONTENT></DATA></asx:values></asx:abap>";
    const std::string kFmSource =
        "function z_fm\n"
        "  importing\n"
        "    value(x) like sflight-carrid\n"
        "  .\n";

    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kTableAndFugrTree}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/table_sflight.xml")}));       // GetTableDefinition(SFLIGHT)
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kZfgFmodules}));  // ListFunctionModules
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kFmSource}));      // FM source

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    // SFLIGHT (real) + ZFG (function group) + Z_FM (function module) — no
    // leftover "unknown" stub for SFLIGHT once reconciled.
    REQUIRE(feed.entities.size() == 3);
    CHECK_FALSE(std::any_of(feed.entities.begin(), feed.entities.end(),
                            [](const CatalogEntity& e) { return e.object_type == "unknown"; }));

    const CatalogEntity* sflight = nullptr;
    for (const auto& e : feed.entities) {
        if (e.technical_name == "SFLIGHT") sflight = &e;
    }
    REQUIRE(sflight != nullptr);
    CHECK(sflight->object_type != "unknown");

    REQUIRE(feed.edges.size() == 1);
    CHECK(feed.edges[0].kind == "uses");
    CHECK(feed.edges[0].to_id.Value() == sflight->id.Value());
}

TEST_CASE("CatalogBuild: RFC parameter default value shows up on the field's description",
          "[adt][catalog][build]") {
    const char* kFugrTree =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
        "<asx:values><DATA><TREE_CONTENT>"
        "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>FUGR/F</OBJECT_TYPE>"
        "<OBJECT_NAME>ZFG</OBJECT_NAME>"
        "<OBJECT_URI>/sap/bc/adt/functions/groups/zfg</OBJECT_URI>"
        "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Test function group</DESCRIPTION>"
        "</SEU_ADT_REPOSITORY_OBJ_NODE>"
        "</TREE_CONTENT></DATA></asx:values></asx:abap>";
    const char* kZfgFmodules =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
        "<asx:values><DATA><TREE_CONTENT>"
        "<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>FUGR/FF</OBJECT_TYPE>"
        "<OBJECT_NAME>Z_FM</OBJECT_NAME>"
        "<OBJECT_URI>/sap/bc/adt/functions/groups/zfg/fmodules/z_fm</OBJECT_URI>"
        "<EXPANDABLE></EXPANDABLE><DESCRIPTION>Test FM</DESCRIPTION>"
        "</SEU_ADT_REPOSITORY_OBJ_NODE>"
        "</TREE_CONTENT></DATA></asx:values></asx:abap>";
    const std::string kFmSource =
        "function z_fm\n"
        "  importing\n"
        "    value(cache_results) type flag_x default 'X'\n"
        "  .\n";

    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kFugrTree}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kZfgFmodules}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kFmSource}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogBuild(mock, options);
    REQUIRE(result.IsOk());
    const auto& feed = result.Value();

    bool found = false;
    for (const auto& f : feed.fields) {
        if (f.name != "CACHE_RESULTS") continue;
        found = true;
        REQUIRE(f.description.has_value());
        CHECK(*f.description == "default: 'X'");
    }
    CHECK(found);
}
