#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_build.hpp>
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

    REQUIRE(feed.entities.size() == 2);

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

    bool has_sflight_field = false;
    bool has_cds_field = false;
    for (const auto& f : feed.fields) {
        if (f.entity_id.Value() == tabl_entity->id.Value()) has_sflight_field = true;
        if (f.entity_id.Value() == cds_entity->id.Value()) has_cds_field = true;
    }
    CHECK(has_sflight_field);
    CHECK(has_cds_field);
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

    REQUIRE(feed.entities.size() == 2);  // both entities still created
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
