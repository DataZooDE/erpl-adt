#include <catch2/catch_test_macros.hpp>

#include "../../src/workflow/abapgit_i18n.hpp"
#include "../../src/workflow/abapgit_types.hpp"

#include "../../test/mocks/mock_adt_session.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

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

std::shared_ptr<const IClassicTypeHandler> Handler(const std::string& type) {
    for (auto& h : MakeClassicTypeHandlers()) {
        if (h->Type() == type) {
            return h;
        }
    }
    FAIL("no handler for " + type);
    return nullptr;
}

std::string File(const std::string& serializer, const std::string& values) {
    AbapGitXml xml;
    xml.serializer = serializer;
    xml.serializer_version = "v1.0.0";
    xml.values_xml = values;
    return RenderAbapGitXml(xml);
}

std::string Asx(const std::string& values) {
    return "<?xml version=\"1.0\" encoding=\"utf-16\"?><asx:abap "
           "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>" +
           values + "</asx:values></asx:abap>";
}

// --- the domain of the realistic case, as abapGit serializes it -------------
const std::string kDd01v =
    "<DD01V><DOMNAME>ZERPL_D_X</DOMNAME><DDLANGUAGE>E</DDLANGUAGE><DATATYPE>CHAR</DATATYPE>"
    "<DDTEXT>Status</DDTEXT></DD01V>";
const std::string kDd07v =
    "<DD07V_TAB><DD07V><VALPOS>0001</VALPOS><DDLANGUAGE>E</DDLANGUAGE><DOMVALUE_L>N</DOMVALUE_L>"
    "<DDTEXT>New</DDTEXT></DD07V></DD07V_TAB>";
std::string Langs(const std::string& codes) {
    std::string out = "<I18N_LANGS>";
    for (char c : codes) {
        out += std::string("<LANGU>") + c + "</LANGU>";
    }
    return out + "</I18N_LANGS>";
}
std::string Dd01Texts(const std::string& lang, const std::string& text) {
    return "<DD01_TEXTS><item><DDLANGUAGE>" + lang + "</DDLANGUAGE><DDTEXT>" + text +
           "</DDTEXT></item></DD01_TEXTS>";
}
std::string Dd07Text(const std::string& lang, const std::string& text) {
    return "<item><VALPOS>0001</VALPOS><DDLANGUAGE>" + lang +
           "</DDLANGUAGE><DOMVALUE_L>N</DOMVALUE_L><DDTEXT>" + text + "</DDTEXT></item>";
}
std::string LongtextItem(const std::string& lang, const std::string& line) {
    return "<item><DOKIL><ID>DO</ID><OBJECT>ZERPL_D_X</OBJECT><LANGU>" + lang +
           "</LANGU><TYP>E</TYP></DOKIL><HEAD><TDSPRAS>" + lang +
           "</TDSPRAS></HEAD><LINES><TLINE><TDFORMAT>AS</TDFORMAT><TDLINE>" + line +
           "</TDLINE></TLINE></LINES></item>";
}
std::string Longtexts(const std::string& section, const std::string& lang,
                      const std::string& line) {
    return "<" + section + ">" + LongtextItem(lang, line) + "</" + section + ">";
}

const AbapGitObjectKey kDoma{"DOMA", "ZERPL_D_X"};
const std::string kDomaXml = "zerpl_d_x.doma.xml";

ObjectFileSet DomaFiles(const std::string& extra) {
    return {{kDomaXml, File("LCL_OBJECT_DOMA", kDd01v + kDd07v + extra)}};
}

ObjectState Existing(const ObjectFileSet& files) {
    ObjectState state;
    state.exists = true;
    state.files = files;
    return state;
}

struct Ctx {
    MockAdtSession session;
    FakeEngine engine;
    TypeContext ctx{session, engine, "$TMP", "", "E"};
};

std::vector<std::string> Calls(const std::vector<PlannedOp>& ops) {
    std::vector<std::string> out;
    for (const auto& op : ops) {
        out.push_back(op.op.call.substr(0, op.op.call.find('(')));
    }
    return out;
}

}  // namespace

// --- sections ---------------------------------------------------------------

TEST_CASE("i18n: translation and documentation sections are recognized",
          "[workflow][abapgit_i18n]") {
    for (const char* s : {"I18N_LANGS", "DD01_TEXTS", "DD07_TEXTS", "DD04_TEXTS", "I18N_TPOOL"}) {
        CHECK(IsTranslationSection(s));
        CHECK(IsI18nSection(s));
    }
    CHECK(IsLongtextSection("LONGTEXTS"));
    CHECK(IsLongtextSection("LONGTEXTS_DZ"));
    CHECK_FALSE(IsI18nSection("DD01V"));
    CHECK_FALSE(IsI18nSection("TPOOL"));
    CHECK_FALSE(IsI18nSection("DD07V_TAB"));
}

TEST_CASE("i18n: the engine's reads are appended in abapGit's order",
          "[workflow][abapgit_i18n]") {
    auto file = RenderWithI18n(Asx(kDd01v + kDd07v),
                               {Asx(Langs("D") + Dd01Texts("D", "Status DE")), "",
                                Asx(Longtexts("LONGTEXTS", "E", "Doc"))},
                               "LCL_OBJECT_DOMA");
    REQUIRE(file.IsOk());
    CHECK(file.Value() == DomaFiles(Langs("D") + Dd01Texts("D", "Status DE") +
                                    Longtexts("LONGTEXTS", "E", "Doc"))
                              .at(kDomaXml));
}

TEST_CASE("i18n: master sections compare without the translations",
          "[workflow][abapgit_i18n]") {
    auto want = FileSections(DomaFiles(Langs("D") + Dd01Texts("D", "A")).at(kDomaXml));
    auto have = FileSections(DomaFiles(Langs("D") + Dd01Texts("D", "B")).at(kDomaXml));
    CHECK(SameMasterSections(want, have));
    CHECK(ChangedSections(want, have, IsTranslationSection) ==
          std::vector<std::string>{"DD01_TEXTS"});
}

TEST_CASE("i18n: without_i18n strips translations and documentation",
          "[workflow][abapgit_i18n]") {
    auto stripped = WithoutI18nSections(
        DomaFiles(Langs("D") + Dd01Texts("D", "A") + Longtexts("LONGTEXTS", "E", "Doc")));
    CHECK(stripped == DomaFiles(""));
}

// --- scope: a deploy manages exactly the languages its files name ------------

TEST_CASE("i18n: translations only in the system are not compared",
          "[workflow][abapgit_i18n]") {
    // The files carry no translations (as before this feature): the
    // system's German texts and documentation are out of scope.
    auto current = Existing(DomaFiles(Langs("D") + Dd01Texts("D", "Status DE") +
                                      Longtexts("LONGTEXTS", "D", "Doku")));
    auto scoped = ScopeTranslations(DomaFiles(""), current);
    CHECK(scoped.files == DomaFiles(""));
}

TEST_CASE("i18n: only the languages the files name are compared",
          "[workflow][abapgit_i18n]") {
    auto desired = DomaFiles(Langs("D") + "<DD07_TEXTS>" + Dd07Text("D", "Neu") + "</DD07_TEXTS>" +
                             Longtexts("LONGTEXTS", "E", "Doc"));
    auto current = Existing(DomaFiles(Langs("DF") + "<DD07_TEXTS>" + Dd07Text("D", "Neu") +
                                      Dd07Text("F", "Nouveau") + "</DD07_TEXTS>" +
                                      "<LONGTEXTS>" + LongtextItem("D", "Doku") +
                                      LongtextItem("E", "Doc") + "</LONGTEXTS>"));
    auto scoped = ScopeTranslations(desired, current);
    CHECK(scoped.files == desired);
}

TEST_CASE("i18n: files without XML translations stay byte-identical",
          "[workflow][abapgit_i18n]") {
    ObjectState current = Existing({{"zerpl_p.prog.abap", "REPORT zerpl_p.\n"}});
    auto scoped = ScopeTranslations({}, current);
    CHECK(scoped.files == current.files);
}

// --- DOMA -------------------------------------------------------------------

TEST_CASE("DOMA: read includes the translations and the documentation",
          "[workflow][abapgit_i18n]") {
    auto ops = Handler("DOMA")->ReadOps(kDoma, "E");
    REQUIRE(ops.size() == 3);
    CHECK(ops[1].call == "i18n_doma_get( name = 'ZERPL_D_X' language = 'E' ).");
    CHECK(ops[2].call ==
          "i18n_docu_get( id = 'DO' object = 'ZERPL_D_X' section = 'LONGTEXTS' ).");
}

TEST_CASE("DOMA: state merges translations and documentation into the file",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    EngineOpResult main;
    main.rc = 0;
    main.exists = true;
    main.data["XML"] = Asx(kDd01v + kDd07v);
    EngineOpResult texts;
    texts.rc = 0;
    texts.data["XML"] = Asx(Langs("D") + Dd01Texts("D", "Status DE"));
    EngineOpResult docu;
    docu.rc = 0;
    docu.data["XML"] = Asx(Longtexts("LONGTEXTS", "E", "Doc"));
    docu.values["CHANGED"] = {"LONGTEXTS DEVELOPER"};
    auto state = Handler("DOMA")->BuildState(kDoma, {main, texts, docu}, c.ctx);
    REQUIRE(state.IsOk());
    CHECK(state.Value().files == DomaFiles(Langs("D") + Dd01Texts("D", "Status DE") +
                                           Longtexts("LONGTEXTS", "E", "Doc")));
    const auto& who = state.Value().part_changed_by;
    CHECK(who.at(kDomaXml + "#DD01_TEXTS").empty());  // SAP records no changer
    CHECK(who.at(kDomaXml + "#LONGTEXTS") == "DEVELOPER");

    // A read that scripts only the master op (older callers) has no translations.
    auto plain = Handler("DOMA")->BuildState(kDoma, {main}, c.ctx);
    REQUIRE(plain.IsOk());
    CHECK(plain.Value().files == DomaFiles(""));
}

TEST_CASE("DOMA: create writes master, translations and documentation in the DDIC run",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    auto desired = DomaFiles(Langs("D") + Dd01Texts("D", "Status DE") +
                             Longtexts("LONGTEXTS", "E", "Doc"));
    auto plan = Handler("DOMA")->Plan(kDoma, desired, ObjectState{}, c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(p.parts == std::vector<std::string>{"DD01V", "DD07V_TAB", "DD01_TEXTS", "LONGTEXTS"});
    CHECK(Calls(p.ddic) == std::vector<std::string>{"doma_put", "i18n_doma_put", "i18n_docu_put"});
    CHECK(p.writes.empty());
    // The master payload carries no translations, the translation payload
    // nothing else.
    CHECK(p.ddic[0].op.payload.find("I18N_LANGS") == std::string::npos);
    CHECK(p.ddic[1].op.payload.find("<DD01_TEXTS>") != std::string::npos);
    CHECK(p.ddic[1].op.payload.find("DD01V") == std::string::npos);
    CHECK(p.ddic[1].what == "Writing the translations of DOMA ZERPL_D_X");
    CHECK(p.ddic[2].op.call.find("corr_object = 'DOMAZERPL_D_X' corr_class = 'DICT'") !=
          std::string::npos);
    CHECK(p.ddic[2].op.payload.find("<LONGTEXTS>") != std::string::npos);
}

TEST_CASE("DOMA: a changed translation writes only the translations",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    auto desired = DomaFiles(Langs("D") + "<DD07_TEXTS>" + Dd07Text("D", "Annulliert") +
                             "</DD07_TEXTS>");
    auto current = Existing(DomaFiles(Langs("D") + "<DD07_TEXTS>" + Dd07Text("D", "Storniert") +
                                      "</DD07_TEXTS>"));
    auto plan = Handler("DOMA")->Plan(kDoma, desired, current, c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"DD07_TEXTS"});
    CHECK(Calls(plan.Value().ddic) == std::vector<std::string>{"i18n_doma_put"});
}

TEST_CASE("DOMA: files without translations plan as before", "[workflow][abapgit_i18n]") {
    Ctx c;
    auto plan = Handler("DOMA")->Plan(kDoma, DomaFiles(""), ObjectState{}, c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"DD01V", "DD07V_TAB"});
    CHECK(Calls(plan.Value().ddic) == std::vector<std::string>{"doma_put"});
}

TEST_CASE("DOMA: delete removes the documentation too", "[workflow][abapgit_i18n]") {
    Ctx c;
    auto deleted = Handler("DOMA")->Delete(kDoma, c.ctx);
    REQUIRE(deleted.IsOk());
    REQUIRE(c.engine.runs.size() == 1);
    REQUIRE(c.engine.runs[0].size() == 2);
    CHECK(c.engine.runs[0][1].call == "i18n_docu_delete( id = 'DO' object = 'ZERPL_D_X' ).");
}

// --- DTEL -------------------------------------------------------------------

TEST_CASE("DTEL: documentation and supplementary documentation are read",
          "[workflow][abapgit_i18n]") {
    auto ops = Handler("DTEL")->ReadOps({"DTEL", "ZERPL_E_X"}, "E");
    REQUIRE(ops.size() == 4);
    CHECK(ops[1].call == "i18n_dtel_get( name = 'ZERPL_E_X' language = 'E' ).");
    CHECK(ops[2].call ==
          "i18n_docu_get( id = 'DE' object = 'ZERPL_E_X' section = 'LONGTEXTS' ).");
    CHECK(ops[3].call ==
          "i18n_docu_get( id = 'DZ' object = 'ZERPL_E_X' section = 'LONGTEXTS_DZ' ).");
}

TEST_CASE("DTEL: changed documentation writes only the documentation",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    const AbapGitObjectKey key{"DTEL", "ZERPL_E_X"};
    const std::string dd04v = "<DD04V><ROLLNAME>ZERPL_E_X</ROLLNAME><DDTEXT>X</DDTEXT></DD04V>";
    const std::string texts = Langs("D") +
                              "<DD04_TEXTS><item><DDLANGUAGE>D</DDLANGUAGE><DDTEXT>X DE</DDTEXT>"
                              "</item></DD04_TEXTS>";
    ObjectFileSet desired{{"zerpl_e_x.dtel.xml",
                           File("LCL_OBJECT_DTEL", dd04v + texts +
                                                       Longtexts("LONGTEXTS", "D", "Neu"))}};
    auto current = Existing({{"zerpl_e_x.dtel.xml",
                              File("LCL_OBJECT_DTEL",
                                   dd04v + texts + Longtexts("LONGTEXTS", "D", "Alt"))}});
    auto plan = Handler("DTEL")->Plan(key, desired, current, c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"LONGTEXTS"});
    REQUIRE(Calls(plan.Value().ddic) == std::vector<std::string>{"i18n_docu_put"});
    CHECK(plan.Value().ddic[0].op.call.find("section = 'LONGTEXTS' corr_object = "
                                            "'DTELZERPL_E_X' corr_class = 'DICT'") !=
          std::string::npos);
}

// --- PROG -------------------------------------------------------------------

namespace {

const AbapGitObjectKey kProg{"PROG", "ZERPL_P"};
const std::string kProgdir = "<PROGDIR><NAME>ZERPL_P</NAME><SUBC>1</SUBC></PROGDIR>";
const std::string kTpool =
    "<TPOOL><item><ID>R</ID><ENTRY>Report</ENTRY><LENGTH>6</LENGTH></item></TPOOL>";
std::string I18nTpool(const std::string& title) {
    return "<I18N_TPOOL><item><LANGUAGE>D</LANGUAGE><TEXTPOOL><item><ID>R</ID><ENTRY>" + title +
           "</ENTRY><LENGTH>7</LENGTH></item></TEXTPOOL></item></I18N_TPOOL>";
}
ObjectFileSet ProgFiles(const std::string& extra) {
    return {{"zerpl_p.prog.xml", File("LCL_OBJECT_PROG", kProgdir + kTpool + extra)},
            {"zerpl_p.prog.abap", "REPORT zerpl_p.\n"}};
}

}  // namespace

TEST_CASE("PROG: text pool translations and documentation are deployable",
          "[workflow][abapgit_i18n]") {
    auto files = ProgFiles(I18nTpool("Bericht") + Longtexts("LONGTEXTS", "E", "Doc"));
    auto valid = Handler("PROG")->Validate(kProg, files, ObjectState{}, "E");
    CHECK(valid.IsOk());
}

TEST_CASE("PROG: a changed translation writes only the text pool translation",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    auto plan = Handler("PROG")->Plan(kProg, ProgFiles(I18nTpool("Bericht")),
                                      Existing(ProgFiles(I18nTpool("Report DE"))), c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(p.parts == std::vector<std::string>{"I18N_TPOOL"});
    CHECK_FALSE(p.adt);
    CHECK(p.activate.empty());  // translations are written active
    REQUIRE(Calls(p.writes) == std::vector<std::string>{"i18n_tpool_put"});
    CHECK(p.writes[0].op.payload.find("<I18N_TPOOL>") != std::string::npos);
    CHECK(p.writes[0].op.payload.find("PROGDIR") == std::string::npos);
}

TEST_CASE("PROG: create writes the program, then translations and documentation",
          "[workflow][abapgit_i18n]") {
    Ctx c;
    auto plan = Handler("PROG")->Plan(
        kProg, ProgFiles(I18nTpool("Bericht") + Longtexts("LONGTEXTS", "E", "Doc")),
        ObjectState{}, c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(Calls(p.writes) ==
          std::vector<std::string>{"prog_put", "i18n_tpool_put", "i18n_docu_put"});
    CHECK(p.writes[0].op.payload.find("I18N_TPOOL") == std::string::npos);
    CHECK(p.writes[2].op.call.find("corr_object = 'ZERPL_P' corr_class = 'ABAP'") !=
          std::string::npos);
    REQUIRE(p.parts.size() >= 2);
    CHECK(p.parts[p.parts.size() - 2] == "I18N_TPOOL");
    CHECK(p.parts.back() == "LONGTEXTS");
}
