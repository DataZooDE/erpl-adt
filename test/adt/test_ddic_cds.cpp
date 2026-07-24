#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/ddic_cds.hpp>
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

// Fixtures are real captured DDL source text (localhost:50000, client 001,
// DEVELOPER, SAP ABAP Cloud Developer Trial) — not hand-written.

TEST_CASE("ParseCdsSource: I_ABAPApplCompText - keys, text semantics, exposed associations",
          "[adt][ddic][cds]") {
    auto ddl = LoadFixture("ddic/cds_i_abapapplcomptext_source.abap");
    auto info = ParseCdsSource(ddl);

    CHECK(info.entity_name == "I_ABAPApplCompText");
    CHECK(info.source_table == "df14t");

    REQUIRE(info.associations.size() == 2);
    CHECK(info.associations[0].alias == "_ABAPApplicationComponent");
    CHECK(info.associations[0].target == "I_ABAPApplicationComponent");
    CHECK(info.associations[0].to_parent);
    CHECK(info.associations[0].on_condition.find("$projection.ABAPApplicationComponent") !=
          std::string::npos);

    CHECK(info.associations[1].alias == "_Language");
    CHECK(info.associations[1].target == "I_Language");
    CHECK(info.associations[1].cardinality == "[0..1]");
    CHECK_FALSE(info.associations[1].to_parent);

    // Fields: langu (key), fctr_id (key), name, plus 2 exposed associations
    REQUIRE(info.fields.size() == 5);

    const auto& f0 = info.fields[0];
    CHECK(f0.name == "Language");
    CHECK(f0.source_expression == "langu");
    CHECK(f0.is_key);
    CHECK_FALSE(f0.is_association);
    REQUIRE(f0.annotations.size() == 2);
    CHECK(f0.annotations[0].find("@ObjectModel.foreignKey.association") != std::string::npos);
    CHECK(f0.annotations[1].find("@Semantics.language") != std::string::npos);

    const auto& f1 = info.fields[1];
    CHECK(f1.name == "ABAPApplicationComponent");
    CHECK(f1.source_expression == "fctr_id");
    CHECK(f1.is_key);

    const auto& f2 = info.fields[2];
    CHECK(f2.name == "ABAPApplicationComponentName");
    CHECK(f2.source_expression == "name");
    CHECK_FALSE(f2.is_key);
    REQUIRE(f2.annotations.size() == 1);

    const auto& f3 = info.fields[3];
    CHECK(f3.name == "_ABAPApplicationComponent");
    CHECK(f3.is_association);
    CHECK(f3.source_expression.empty());

    const auto& f4 = info.fields[4];
    CHECK(f4.name == "_Language");
    CHECK(f4.is_association);
}

TEST_CASE("ParseCdsSource: I_ABAPPackage - root entity, composition, no annotations on most fields",
          "[adt][ddic][cds]") {
    auto ddl = LoadFixture("ddic/cds_i_abappackage_source.abap");
    auto info = ParseCdsSource(ddl);

    CHECK(info.entity_name == "I_ABAPPackage");
    CHECK(info.source_table == "tdevc");

    REQUIRE(info.associations.size() == 3);
    CHECK(info.associations[0].alias == "_ABAPSoftwareComponent");
    CHECK(info.associations[0].cardinality == "[1..1]");
    CHECK_FALSE(info.associations[0].is_composition);
    CHECK(info.associations[2].alias == "_Text");
    CHECK(info.associations[2].is_composition);
    CHECK(info.associations[2].target == "I_ABAPPackageText");
    CHECK(info.associations[2].cardinality == "[0..*]");

    // 11 plain fields + 3 exposed associations = 14
    REQUIRE(info.fields.size() == 14);

    const auto& key_field = info.fields[0];
    CHECK(key_field.name == "ABAPPackage");
    CHECK(key_field.source_expression == "devclass");
    CHECK(key_field.is_key);
    REQUIRE(key_field.annotations.size() == 1);

    const auto& plain_field = info.fields[1];
    CHECK(plain_field.name == "ABAPPackageResponsibleUser");
    CHECK(plain_field.source_expression == "as4user");
    CHECK_FALSE(plain_field.is_key);
    CHECK(plain_field.annotations.empty());

    CHECK(info.fields[11].name == "_ABAPSoftwareComponent");
    CHECK(info.fields[11].is_association);
    CHECK(info.fields[13].name == "_Text");
    CHECK(info.fields[13].is_association);
}

TEST_CASE("ParseCdsSource: projection view with a namespaced name and source",
          "[adt][ddic][cds]") {
    const std::string ddl =
        "@AccessControl.authorizationCheck: #NOT_REQUIRED\n"
        "@EndUserText.label: 'Flight Projection'\n"
        "define view entity /DMO/C_FLIGHTPROJECTION\n"
        "  as projection on /DMO/I_FLIGHT\n"
        "{\n"
        "  key AirlineID,\n"
        "      ConnectionID,\n"
        "      FlightDate\n"
        "}\n";

    auto info = ParseCdsSource(ddl);
    CHECK(info.entity_name == "/DMO/C_FLIGHTPROJECTION");
    CHECK(info.source_table == "/DMO/I_FLIGHT");
    REQUIRE(info.fields.size() == 3);
    CHECK(info.fields[0].name == "AirlineID");
    CHECK(info.fields[0].is_key);
}

TEST_CASE("ParseCdsSource: a bare cast expression with no top-level alias is not "
          "mistaken for one",
          "[adt][ddic][cds]") {
    const std::string ddl =
        "define view entity Z_CAST_TEST\n"
        "  as select from mara\n"
        "{\n"
        "  key matnr,\n"
        "      cast( matnr as abap.char( 10 ) ) as MatnrChar\n"
        "}\n";

    auto info = ParseCdsSource(ddl);
    REQUIRE(info.fields.size() == 2);
    CHECK(info.fields[1].name == "MatnrChar");
    CHECK(info.fields[1].source_expression == "cast( matnr as abap.char( 10 ) )");
}

TEST_CASE("ParseCdsSource: a cast with no top-level alias keeps the whole expression",
          "[adt][ddic][cds]") {
    // No " as " outside the cast(...) parens — must not misparse the cast's
    // internal target type as a field alias.
    const std::string ddl =
        "define view entity Z_CAST_TEST2\n"
        "  as select from mara\n"
        "{\n"
        "  key matnr,\n"
        "      cast( matnr as abap.char( 10 ) )\n"
        "}\n";

    auto info = ParseCdsSource(ddl);
    REQUIRE(info.fields.size() == 2);
    CHECK(info.fields[1].name == "cast( matnr as abap.char( 10 ) )");
    CHECK_FALSE(info.fields[1].name == "abap.char( 10 ) )");
}

TEST_CASE("ParseCdsSource: extracts a field's @EndUserText.label as description",
          "[adt][ddic][cds]") {
    const std::string ddl =
        "define view entity Z_LABEL_TEST\n"
        "  as select from mara\n"
        "{\n"
        "  key matnr,\n"
        "      @EndUserText.label: 'Material Group'\n"
        "      matkl as MaterialGroup,\n"
        "      @Semantics.quantity.unitOfMeasure: 'Unit'\n"
        "      meins as Unit\n"
        "}\n";

    auto info = ParseCdsSource(ddl);
    REQUIRE(info.fields.size() == 3);
    CHECK_FALSE(info.fields[0].description.has_value());  // matnr — no annotation at all

    REQUIRE(info.fields[1].description.has_value());
    CHECK(*info.fields[1].description == "Material Group");

    // A field with annotations but no @EndUserText.label has no description.
    CHECK_FALSE(info.fields[2].description.has_value());
}

TEST_CASE("ParseCdsSource: an array-valued annotation's nested commas don't "
          "fragment the field list",
          "[adt][ddic][cds]") {
    // Real captured DDL (SABAPDEMOS, DEMO_CDS_ANNOTATION_ARRAY) — the
    // annotation's `[ {...}, {...} ]` array value has commas nested two
    // levels deep ([...] then {...}). SplitTopLevel previously tracked
    // only paren depth, so those nested commas were treated as top-level
    // field separators, splitting one annotation into several garbage
    // pseudo-fields — including a field literally named "value: '...'",
    // duplicated once per array element (which crashed a real sync with a
    // duplicate-field-id write failure once this package was ever synced).
    auto ddl = LoadFixture("ddic/cds_demo_annotation_array_source.abap");
    auto info = ParseCdsSource(ddl);

    CHECK(info.entity_name == "demo_cds_annotation_array");
    CHECK(info.source_table == "demo_expressions");

    // Exactly one real field ("id"), not several garbage fragments.
    REQUIRE(info.fields.size() == 1);
    CHECK(info.fields[0].name == "id");
    REQUIRE(info.fields[0].annotations.size() == 1);
    CHECK(info.fields[0].annotations[0].find("Consumption.filter.hierarchyBinding") !=
          std::string::npos);
    // The whole multi-line array value stays part of the one annotation
    // line, not split across several.
    CHECK(info.fields[0].annotations[0].find("variableSequence: 2") != std::string::npos);
}

TEST_CASE("ParseCdsSource: a header-level object-valued annotation before "
          "'define view entity' doesn't hijack the field-list brace",
          "[adt][ddic][cds]") {
    // Real captured DDL (/DMO/E_AGENCY) — `@AbapCatalog.extensibility: {...}`
    // appears before `define view entity` with its own object-literal value
    // (including a nested `quota: {...}`). ParseHeader previously searched
    // for the field-list brace via `clean_source.find('{')` from position 0,
    // so it locked onto this annotation's opening brace instead of the real
    // one after "as select from ... {" — parsing the annotation's own
    // key/value pairs as garbage pseudo-fields ("extensible: true",
    // "quota: { ... }", etc.) while losing the one real field entirely.
    auto ddl = LoadFixture("ddic/cds_e_agency_extension_include_source.abap");
    auto info = ParseCdsSource(ddl);

    CHECK(info.entity_name == "/DMO/E_Agency");
    CHECK(info.source_table == "/dmo/agency");

    REQUIRE(info.fields.size() == 1);
    CHECK(info.fields[0].name == "AgencyId");
    CHECK(info.fields[0].is_key);
}

TEST_CASE("GetCdsStructure: fetches source and parses it", "[adt][ddic][cds]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("ddic/cds_i_abapapplcomptext_source.abap")}));

    auto result = GetCdsStructure(mock, "I_ABAPAPPLCOMPTEXT");
    REQUIRE(result.IsOk());
    CHECK(result.Value().entity_name == "I_ABAPApplCompText");
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bc/adt/ddic/ddl/sources/I_ABAPAPPLCOMPTEXT/source/main");
}

TEST_CASE("GetCdsStructure: 404 maps to NotFound", "[adt][ddic][cds]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = GetCdsStructure(mock, "ZDOES_NOT_EXIST");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}
