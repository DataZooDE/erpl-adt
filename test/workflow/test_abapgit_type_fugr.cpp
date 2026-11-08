#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/function_group.hpp>
#include <erpl_adt/workflow/abapgit_fugr.hpp>

#include "../../src/workflow/abapgit_types.hpp"
#include "../../test/mocks/mock_adt_session.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

namespace fs = std::filesystem;

fs::path TestRoot() {
    std::string this_file = __FILE__;
    auto test_dir = this_file.substr(0, this_file.find_last_of("/\\"));
    return fs::path(test_dir.substr(0, test_dir.find_last_of("/\\")));
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

FugrDefinition ParseFile(const fs::path& path) {
    auto xml = ParseAbapGitXml(ReadBytes(path));
    REQUIRE(xml.IsOk());
    auto def = ParseFugrValues(xml.Value().values_xml);
    REQUIRE(def.IsOk());
    return def.Value();
}

const fs::path kAbapGitTests = TestRoot() / "testdata" / "abapgit" / "FUGR_with_parameters" / "src";
const fs::path kErplDemo =
    TestRoot() / "integration_py" / "fixtures" / "abapgit" / "erpl_demo" / "src";

class FakeEngine final : public IEngineRunner {
public:
    std::vector<std::vector<EngineOp>> runs;
    Result<std::vector<EngineOpResult>, Error> Run(const std::vector<EngineOp>& ops) override {
        runs.push_back(ops);
        std::vector<EngineOpResult> ok(ops.size());
        for (auto& r : ok) {
            r.rc = 0;
        }
        return Result<std::vector<EngineOpResult>, Error>::Ok(ok);
    }
};

}  // namespace

TEST_CASE("FUGR: abapGit-tests function group XML is parsed", "[workflow][abapgit_fugr]") {
    auto def = ParseFile(kAbapGitTests / "zagtest_function_group.fugr.xml");
    CHECK(def.areat == "test");
    CHECK(def.includes ==
          std::vector<std::string>{"LZAGTEST_FUNCTION_GROUPTOP", "SAPLZAGTEST_FUNCTION_GROUP"});
    REQUIRE(def.functions.size() == 1);
    const auto& fm = def.functions[0];
    CHECK(fm.name == "ZAGTEST_FUNCTION_MODULE");
    CHECK(fm.short_text == "test");
    REQUIRE(fm.importing.size() == 1);
    CHECK(fm.importing[0].reference);
    CHECK(fm.importing[0].typ == "C");
    REQUIRE(fm.tables.size() == 1);
    CHECK(fm.tables[0].typ == "STRING_TABLE");
    CHECK(fm.item_xml.rfind("<item><FUNCNAME>ZAGTEST_FUNCTION_MODULE</FUNCNAME>", 0) == 0);
}

TEST_CASE("FUGR: abapGit-tests function module becomes ADT signature source",
          "[workflow][abapgit_fugr]") {
    auto def = ParseFile(kAbapGitTests / "zagtest_function_group.fugr.xml");
    auto source = ReadBytes(kAbapGitTests / "zagtest_function_group.fugr.zagtest_function_module.abap");

    auto sig = BuildFmSignatureSource(def.functions[0], source);
    REQUIRE(sig.IsOk());
    // 12 header lines (FUNCTION + 11 `*"` lines) replaced by 9 signature
    // lines + 3 padding lines; the body keeps its own empty first line.
    CHECK(sig.Value() ==
          "FUNCTION zagtest_function_module\n"
          "  IMPORTING\n"
          "    import_parameter TYPE c\n"
          "  EXPORTING\n"
          "    export_parameter TYPE c\n"
          "  CHANGING\n"
          "    changing_parameter TYPE c\n"
          "  TABLES\n"
          "    tables_parameter TYPE string_table.\n"
          "\n"
          "\n"
          "\n"
          "\n"
          "  WRITE: import_parameter, export_parameter, changing_parameter.\n"
          "\n"
          "ENDFUNCTION.\n");
}

TEST_CASE("FUGR: erpl demo read-order module (VALUE, STRUCTURE, EXCEPTIONS)",
          "[workflow][abapgit_fugr]") {
    auto def = ParseFile(kErplDemo / "zerpl_fg.fugr.xml");
    CHECK(def.areat == "erpl demo: order access");
    REQUIRE(def.functions.size() == 2);
    const auto& fm = def.functions[0];
    REQUIRE(fm.name == "Z_ERPL_READ_ORDER");

    CHECK(BuildFmSignatureLines(fm) == std::vector<std::string>{
                                           "FUNCTION z_erpl_read_order",
                                           "  IMPORTING",
                                           "    VALUE(iv_so_id) TYPE snwd_so_id",
                                           "  EXPORTING",
                                           "    VALUE(es_order) TYPE snwd_so",
                                           "  TABLES",
                                           "    et_items LIKE snwd_so_i",
                                           "  EXCEPTIONS",
                                           "    not_found.",
                                       });

    auto source = ReadBytes(kErplDemo / "zerpl_fg.fugr.z_erpl_read_order.abap");
    auto sig = BuildFmSignatureSource(fm, source);
    REQUIRE(sig.IsOk());
    // 12 header lines vs. 9 signature lines: 3 padding lines, then the
    // body's own empty first line.
    CHECK(sig.Value().find("    not_found.\n\n\n\n\n  SELECT SINGLE * FROM snwd_so") !=
          std::string::npos);
    CHECK(sig.Value().size() > 0);
    CHECK(sig.Value().substr(sig.Value().size() - 13) == "ENDFUNCTION.\n");
}

TEST_CASE("FUGR: erpl demo read-partner module (REFERENCE, DEFAULT, OPTIONAL, LIKE, RAISING)",
          "[workflow][abapgit_fugr]") {
    auto def = ParseFile(kErplDemo / "zerpl_fg.fugr.xml");
    const auto& fm = def.functions[1];
    REQUIRE(fm.name == "Z_ERPL_READ_PARTNER");
    CHECK(fm.class_based_exceptions);
    CHECK(BuildFmSignatureLines(fm) ==
          std::vector<std::string>{
              "FUNCTION z_erpl_read_partner",
              "  IMPORTING",
              "    VALUE(iv_bp_id) TYPE snwd_partner_id",
              "    iv_with_contacts TYPE abap_bool DEFAULT ABAP_FALSE",
              "    iv_langu LIKE sy-langu OPTIONAL",
              "  EXPORTING",
              "    VALUE(es_partner) TYPE snwd_bpa",
              "  CHANGING",
              "    ct_log TYPE string_table OPTIONAL",
              "  RAISING",
              "    cx_sy_itab_line_not_found.",
          });
    auto source = ReadBytes(kErplDemo / "zerpl_fg.fugr.z_erpl_read_partner.abap");
    auto sig = BuildFmSignatureSource(fm, source);
    REQUIRE(sig.IsOk());
    // 14 header lines vs. 11 signature lines; the body starts right after.
    CHECK(sig.Value().find("    cx_sy_itab_line_not_found.\n\n\n\n  \" Reads one business") !=
          std::string::npos);
}

TEST_CASE("FUGR: rare parameter forms follow SAP's signature builder", "[workflow][abapgit_fugr]") {
    FmInterface fm;
    fm.name = "Z_RARE";
    FmParameter untyped;
    untyped.name = "IV_ANY";
    untyped.reference = true;
    fm.importing.push_back(untyped);
    FmParameter ref;
    ref.name = "IO_OBJ";
    ref.typ = "CL_ABAP_TYPEDESCR";
    ref.ref_class = true;
    fm.importing.push_back(ref);
    FmParameter old_ref;
    old_ref.name = "IO_OLD";
    old_ref.typ = "REF TO OBJECT";
    fm.importing.push_back(old_ref);
    FmParameter long_like;
    long_like.name = "IV_LONG";
    long_like.typ = "ZSOME_VERY_LONG_STRUCTURE-COMPONENT";
    long_like.types = "O";
    fm.exporting.push_back(long_like);
    FmParameter tab;
    tab.name = "T_ANY";
    tab.optional = true;
    fm.tables.push_back(tab);
    FmException ex;
    ex.name = "CX_RESUME";
    ex.resumable = true;
    fm.exceptions.push_back(ex);
    fm.class_based_exceptions = true;

    CHECK(BuildFmSignatureLines(fm) ==
          std::vector<std::string>{
              "FUNCTION z_rare",
              "  IMPORTING",
              "    iv_any TYPE ANY ##ADT_PARAMETER_UNTYPED",
              "    VALUE(io_obj) TYPE REF TO cl_abap_typedescr",
              "    VALUE(io_old) TYPE REF TO object",
              "  EXPORTING",
              "    VALUE(iv_long) LIKE zsome_very_long_structure-component",
              "  TABLES",
              "    t_any TYPE STANDARD TABLE OPTIONAL ##ADT_PARAMETER_UNTYPED",
              "  RAISING",
              "    RESUMABLE(cx_resume).",
          });
}

TEST_CASE("FUGR: function module without interface", "[workflow][abapgit_fugr]") {
    FmInterface fm;
    fm.name = "Z_EMPTY";
    auto sig = BuildFmSignatureSource(fm,
                                      "FUNCTION z_empty.\n"
                                      "*\"--------\n"
                                      "*\"*\"Local Interface:\n"
                                      "*\"--------\n"
                                      "  WRITE 'x'.\n"
                                      "ENDFUNCTION.\n");
    REQUIRE(sig.IsOk());
    CHECK(sig.Value() == "FUNCTION z_empty.\n\n\n\n  WRITE 'x'.\nENDFUNCTION.\n");
}

TEST_CASE("FUGR: source without FUNCTION statement is rejected", "[workflow][abapgit_fugr]") {
    FmInterface fm;
    fm.name = "Z_BROKEN";
    CHECK(BuildFmSignatureSource(fm, "WRITE 'x'.\n").IsErr());
}

TEST_CASE("FUGR: sub-object file name parts", "[workflow][abapgit_fugr]") {
    CHECK(FugrPartName("LZERPL_FGTOP") == "lzerpl_fgtop");
    CHECK(FugrPartName("/ACME/SAPLFG") == "#acme#saplfg");
}

TEST_CASE("FUGR handler: engine data becomes abapGit files", "[workflow][abapgit_fugr]") {
    auto handlers = DefaultTypeHandlers();
    REQUIRE(handlers.count("FUGR") == 1);
    const auto& handler = *handlers.at("FUGR");
    AbapGitObjectKey key{"FUGR", "ZERPL_FG"};

    auto ops = handler.ReadOps(key, "E");
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].call == "fugr_get( name = 'ZERPL_FG' language = 'E' ).");

    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    r.changed_by = "DEVELOPER";
    r.data["XML"] =
        "<?xml version=\"1.0\" encoding=\"utf-16\"?><asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" "
        "version=\"1.0\"><asx:values><AREAT>demo</AREAT></asx:values></asx:abap>";
    r.data["FM:Z_ERPL_READ_ORDER"] = "FUNCTION z_erpl_read_order.\nENDFUNCTION.\n";
    r.data["SRC:LZERPL_FGTOP"] = "FUNCTION-POOL ZERPL_FG.\n";
    r.data["DIR:LZERPL_FGTOP"] =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
        "<PROGDIR><NAME>LZERPL_FGTOP</NAME></PROGDIR></asx:values></asx:abap>";

    MockAdtSession session;
    FakeEngine engine;
    TypeContext ctx{session, engine, "$TMP", "", "E"};
    auto state = handler.BuildState(key, {r}, ctx);
    REQUIRE(state.IsOk());
    const auto& files = state.Value().files;
    REQUIRE(files.size() == 4);
    CHECK(files.at("zerpl_fg.fugr.z_erpl_read_order.abap") ==
          "FUNCTION z_erpl_read_order.\nENDFUNCTION.\n");
    CHECK(files.at("zerpl_fg.fugr.lzerpl_fgtop.abap") == "FUNCTION-POOL ZERPL_FG.\n");
    CHECK(files.at("zerpl_fg.fugr.xml").find("serializer=\"LCL_OBJECT_FUGR\"") != std::string::npos);
    const auto& incl = files.at("zerpl_fg.fugr.lzerpl_fgtop.xml");
    CHECK(incl.find("<abapGit version=\"v1.0.0\">") != std::string::npos);
    CHECK(incl.find("<NAME>LZERPL_FGTOP</NAME>") != std::string::npos);
}

TEST_CASE("FUGR handler: RFC modules are refused before anything is written",
          "[workflow][abapgit_fugr]") {
    auto handlers = DefaultTypeHandlers();
    const auto& handler = *handlers.at("FUGR");
    AbapGitObjectKey key{"FUGR", "ZERPL_FG"};
    AbapGitXml xml;
    xml.serializer = "LCL_OBJECT_FUGR";
    xml.serializer_version = "v1.0.0";
    xml.values_xml =
        "<AREAT>demo</AREAT><FUNCTIONS><item><FUNCNAME>Z_RFC</FUNCNAME>"
        "<REMOTE_CALL>R</REMOTE_CALL></item></FUNCTIONS>";
    ObjectFileSet desired{{"zerpl_fg.fugr.xml", RenderAbapGitXml(xml)},
                          {"zerpl_fg.fugr.z_rfc.abap", "FUNCTION z_rfc.\nENDFUNCTION.\n"}};
    MockAdtSession session;
    FakeEngine engine;
    TypeContext ctx{session, engine, "$TMP", "", "E"};
    auto applied = handler.Apply(key, desired, ObjectState{}, ctx);
    REQUIRE(applied.IsErr());
    CHECK(applied.Error().message.find("RFC") != std::string::npos);
    CHECK(session.PostCallCount() == 0);
    CHECK(session.PutCallCount() == 0);
}

TEST_CASE("DTEL handler: reads and writes through the DDIF function modules",
          "[workflow][abapgit_fugr]") {
    auto handlers = DefaultTypeHandlers();
    const auto& handler = *handlers.at("DTEL");
    AbapGitObjectKey key{"DTEL", "ZERPL_E_SO_STATUS"};
    auto ops = handler.ReadOps(key, "E");
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].call == "dtel_get( name = 'ZERPL_E_SO_STATUS' language = 'E' ).");

    MockAdtSession session;
    FakeEngine engine;
    TypeContext ctx{session, engine, "$TMP", "", "E"};
    ObjectFileSet desired{{"zerpl_e_so_status.dtel.xml",
                           ReadBytes(kErplDemo / "zerpl_e_so_status.dtel.xml")}};
    auto applied = handler.Apply(key, desired, ObjectState{}, ctx);
    REQUIRE(applied.IsOk());
    CHECK(applied.Value() == std::vector<std::string>{"DD04V"});
    REQUIRE(engine.runs.size() == 1);
    CHECK(engine.runs[0][0].call ==
          "dtel_put( name = 'ZERPL_E_SO_STATUS' language = 'E' devclass = '$TMP' trkorr = '' ).");
    CHECK(engine.runs[0][0].payload.find("<ROLLNAME>ZERPL_E_SO_STATUS</ROLLNAME>") !=
          std::string::npos);
}

TEST_CASE("ActivateObjects: one request holds every object reference", "[workflow][abapgit_fugr]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok("token"));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<chkl:messages xmlns:chkl=\"http://www.sap.com/abapxml/checklist\">"
                  "<chkl:properties activationExecuted=\"true\"/></chkl:messages>"}));
    ActivateObjectParams include;
    include.uri = "/sap/bc/adt/functions/groups/zerpl_fg/includes/lzerpl_fgtop";
    include.type = "FUGR/I";
    ActivateObjectParams fm;
    fm.uri = "/sap/bc/adt/functions/groups/zerpl_fg/fmodules/z_erpl_read_order";
    fm.type = "FUGR/FF";
    fm.parent_uri = "/sap/bc/adt/functions/groups/zerpl_fg";

    auto result = ActivateObjects(session, {include, fm});

    REQUIRE(result.IsOk());
    CHECK(result.Value().activation_executed);
    REQUIRE(session.PostCallCount() == 1);
    const auto& body = session.PostCalls()[0].body;
    CHECK(body.find("lzerpl_fgtop\" adtcore:type=\"FUGR/I\"") != std::string::npos);
    CHECK(body.find("adtcore:parentUri=\"/sap/bc/adt/functions/groups/zerpl_fg\"") !=
          std::string::npos);
}

TEST_CASE("Function group URIs escape namespaces", "[workflow][abapgit_fugr]") {
    CHECK(FunctionModuleUri("ZERPL_FG", "Z_ERPL_READ_ORDER") ==
          "/sap/bc/adt/functions/groups/zerpl_fg/fmodules/z_erpl_read_order");
    CHECK(FunctionGroupIncludeUri("/ACME/FG", "/ACME/LFGTOP") ==
          "/sap/bc/adt/functions/groups/%2facme%2ffg/includes/%2facme%2flfgtop");
}
