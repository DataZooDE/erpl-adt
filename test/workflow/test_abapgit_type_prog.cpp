#include <catch2/catch_test_macros.hpp>

#include "../../src/workflow/abapgit_types.hpp"

#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Hand-written fake engine: records every run, answers from a script.
class FakeEngine final : public IEngineRunner {
public:
    std::vector<std::vector<EngineOp>> runs;
    std::vector<std::vector<EngineOpResult>> scripted;

    Result<std::vector<EngineOpResult>, Error> Run(const std::vector<EngineOp>& ops) override {
        runs.push_back(ops);
        if (!scripted.empty()) {
            auto next = scripted.front();
            scripted.erase(scripted.begin());
            return Result<std::vector<EngineOpResult>, Error>::Ok(next);
        }
        std::vector<EngineOpResult> ok(ops.size());
        for (auto& r : ok) {
            r.rc = 0;
        }
        return Result<std::vector<EngineOpResult>, Error>::Ok(ok);
    }
};

std::string LoadTestData(const std::string& relative) {
    std::string this_file = __FILE__;
    auto dir = this_file.substr(0, this_file.find_last_of("/\\"));
    auto root = dir.substr(0, dir.find_last_of("/\\"));
    std::ifstream in(root + "/testdata/" + relative, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const AbapGitObjectKey kKey{"PROG", "ZERPL_T_PROG"};

std::string Screen(const std::string& nr, const std::string& vislength) {
    return "<item><HEADER><PROGRAM>ZERPL_T_PROG</PROGRAM><SCREEN>" + nr +
           "</SCREEN><LANGUAGE>E</LANGUAGE><DESCRIPT>Screen " + nr +
           "</DESCRIPT><TYPE>N</TYPE></HEADER><FIELDS><RPY_DYFATC><NAME>F</NAME><VISLENGTH>" +
           vislength + "</VISLENGTH></RPY_DYFATC></FIELDS></item>";
}

struct ProgFiles {
    std::string progdir = "<PROGDIR><NAME>ZERPL_T_PROG</NAME><SUBC>M</SUBC></PROGDIR>";
    std::vector<std::string> screens = {Screen("0100", "010"), Screen("0200", "020")};
    std::string cua = "<CUA><ADM><PFKCODE>000001</PFKCODE></ADM><TIT><RSMPE_TITT><CODE>T0100</CODE>"
                      "<TEXT>Display</TEXT></RSMPE_TITT></TIT></CUA>";
    std::string tpool = "<TPOOL><item><ID>R</ID><ENTRY>Test</ENTRY><LENGTH>4</LENGTH></item></TPOOL>";
    std::string extra;
    std::string source = "PROGRAM zerpl_t_prog.\n";
    std::map<std::string, std::string> flows = {
        {"0100", "PROCESS BEFORE OUTPUT.\nPROCESS AFTER INPUT.\n"},
        {"0200", "PROCESS BEFORE OUTPUT.\nPROCESS AFTER INPUT.\n"}};

    ObjectFileSet Build() const {
        AbapGitXml xml;
        xml.serializer = "LCL_OBJECT_PROG";
        xml.serializer_version = "v1.0.0";
        xml.values_xml = progdir;
        if (!screens.empty()) {
            xml.values_xml += "<DYNPROS>";
            for (const auto& s : screens) {
                xml.values_xml += s;
            }
            xml.values_xml += "</DYNPROS>";
        }
        xml.values_xml += cua + extra + tpool;
        ObjectFileSet files;
        files["zerpl_t_prog.prog.xml"] = RenderAbapGitXml(xml);
        files["zerpl_t_prog.prog.abap"] = source;
        for (const auto& [nr, flow] : flows) {
            files["zerpl_t_prog.prog.screen_" + nr + ".abap"] = flow;
        }
        return files;
    }
};

ObjectState Existing(const ProgFiles& files) {
    ObjectState state;
    state.exists = true;
    state.files = files.Build();
    return state;
}

std::shared_ptr<const IClassicTypeHandler> Handler() {
    for (auto& h : MakeClassicTypeHandlers()) {
        if (h->Type() == "PROG") {
            return h;
        }
    }
    FAIL("no PROG handler registered");
    return nullptr;
}

struct Ctx {
    MockAdtSession session;
    FakeEngine engine;
    TypeContext ctx{session, engine, "$TMP", "", "E"};
};

}  // namespace

TEST_CASE("PROG: read is one prog_get operation", "[workflow][abapgit_prog]") {
    auto ops = Handler()->ReadOps(kKey, "E");
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].call == "prog_get( name = 'ZERPL_T_PROG' language = 'E' ).");
}

TEST_CASE("PROG: state holds xml, source and one file per screen", "[workflow][abapgit_prog]") {
    Ctx c;
    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    r.changed_by = "DEVELOPER";
    r.data["XML"] = "<?xml version=\"1.0\" encoding=\"utf-16\"?><asx:abap "
                    "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
                    "<PROGDIR><NAME>ZERPL_T_PROG</NAME></PROGDIR></asx:values></asx:abap>";
    r.data["SOURCE"] = "REPORT zerpl_t_prog.\n";
    r.data["SCREEN_0100"] = "PROCESS BEFORE OUTPUT.\n";
    auto state = Handler()->BuildState(kKey, {r}, c.ctx);
    REQUIRE(state.IsOk());
    const auto& files = state.Value().files;
    REQUIRE(files.size() == 3);
    CHECK(files.at("zerpl_t_prog.prog.abap") == "REPORT zerpl_t_prog.\n");
    CHECK(files.at("zerpl_t_prog.prog.screen_0100.abap") == "PROCESS BEFORE OUTPUT.\n");
    CHECK(files.at("zerpl_t_prog.prog.xml").find("serializer=\"LCL_OBJECT_PROG\"") !=
          std::string::npos);
    CHECK(state.Value().changed_by == "DEVELOPER");
}

TEST_CASE("PROG: a program that only exists inactive has no files", "[workflow][abapgit_prog]") {
    Ctx c;
    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    auto state = Handler()->BuildState(kKey, {r}, c.ctx);
    REQUIRE(state.IsOk());
    CHECK(state.Value().exists);
    CHECK(state.Value().files.empty());
}

namespace {

// The plan's working objects in short form: "REPS", "DYNP0100", "CUAD", "REPT".
std::string Activation(const DeployPlan& plan) {
    std::string out;
    for (const auto& w : plan.activate) {
        CHECK(w.owner.empty());  // set by the workflow
        std::string item = w.object;
        if (w.object == "DYNP") {
            CHECK(w.obj_name.size() == 44);
            CHECK(w.obj_name.rfind("ZERPL_T_PROG ", 0) == 0);
            item += w.obj_name.substr(40);
        } else {
            CHECK(w.obj_name == "ZERPL_T_PROG");
        }
        out += (out.empty() ? "" : " ") + item;
    }
    return out;
}

}  // namespace

TEST_CASE("PROG: create is one engine write plus its working objects",
          "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles files;
    auto plan = Handler()->Plan(kKey, files.Build(), ObjectState{}, c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(p.parts == std::vector<std::string>{"source", "PROGDIR", "screen 0100",
                                               "screen 0200", "CUA", "TPOOL"});
    CHECK_FALSE(p.adt);  // no ADT write for a new program
    CHECK(p.ddic.empty());
    REQUIRE(p.writes.size() == 1);
    const auto& put = p.writes[0].op;
    CHECK(p.writes[0].what == "Creating program ZERPL_T_PROG");
    CHECK(put.call.find("prog_put( name = 'ZERPL_T_PROG'") == 0);
    CHECK(put.call.find("create = abap_true") != std::string::npos);
    CHECK(put.call.find("write_cua = abap_true") != std::string::npos);
    CHECK(put.payload.find("<SOURCE>PROGRAM zerpl_t_prog.\n</SOURCE>") != std::string::npos);
    CHECK(put.payload.find("<FLOWS><item><SCREEN>0100</SCREEN>") != std::string::npos);
    CHECK(Activation(p) == "REPS DYNP0100 DYNP0200 CUAD REPT");
    CHECK(c.engine.runs.empty());  // planning writes nothing
    CHECK(c.session.PutCallCount() == 0);
}

TEST_CASE("PROG: only the changed screen is written and activated", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles current;
    ProgFiles desired;
    desired.screens[1] = Screen("0200", "015");
    auto plan = Handler()->Plan(kKey, desired.Build(), Existing(current), c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(p.parts == std::vector<std::string>{"screen 0200"});
    CHECK_FALSE(p.adt);
    REQUIRE(p.writes.size() == 1);
    const auto& put = p.writes[0].op;
    CHECK(put.call.find("create = abap_false") != std::string::npos);
    CHECK(put.call.find("write_cua = abap_false") != std::string::npos);
    CHECK(put.call.find("write_tpool = abap_false") != std::string::npos);
    CHECK(put.payload.find("<SCREEN>0100</SCREEN>") == std::string::npos);
    CHECK(put.payload.find("<VISLENGTH>015</VISLENGTH>") != std::string::npos);
    CHECK(Activation(p) == "DYNP0200");
}

TEST_CASE("PROG: changed flow logic rewrites that screen", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles current;
    ProgFiles desired;
    desired.flows["0100"] = "PROCESS BEFORE OUTPUT.\n  MODULE status_0100.\nPROCESS AFTER INPUT.\n";
    auto plan = Handler()->Plan(kKey, desired.Build(), Existing(current), c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"screen 0100"});
    CHECK(plan.Value().writes.at(0).op.payload.find("MODULE status_0100.") != std::string::npos);
}

TEST_CASE("PROG: a title change writes the CUA only", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles current;
    ProgFiles desired;
    desired.cua = "<CUA><ADM><PFKCODE>000001</PFKCODE></ADM><TIT><RSMPE_TITT><CODE>T0100</CODE>"
                  "<TEXT>Show</TEXT></RSMPE_TITT></TIT></CUA>";
    auto plan = Handler()->Plan(kKey, desired.Build(), Existing(current), c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"CUA"});
    CHECK(Activation(plan.Value()) == "CUAD");
}

TEST_CASE("PROG: a source change goes over ADT with lock and unlock",
          "[workflow][abapgit_prog]") {
    Ctx c;
    c.session.EnqueuePost(
        Result<HttpResponse, Error>::Ok({200, {}, LoadTestData("object/lock_response.xml")}));
    c.session.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    c.session.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    ProgFiles current;
    ProgFiles desired;
    desired.source = "PROGRAM zerpl_t_prog.\n* changed\n";
    auto plan = Handler()->Plan(kKey, desired.Build(), Existing(current), c.ctx);
    REQUIRE(plan.IsOk());
    const auto& p = plan.Value();
    CHECK(p.parts == std::vector<std::string>{"source"});
    REQUIRE(p.adt);
    CHECK(c.session.PutCallCount() == 0);  // only when the workflow runs the ADT phase
    auto written = p.adt(c.ctx);
    REQUIRE(written.IsOk());
    REQUIRE(c.session.PutCallCount() == 1);
    CHECK(c.session.PutCalls()[0].path.find(
              "/sap/bc/adt/programs/programs/zerpl_t_prog/source/main") == 0);
    CHECK(c.session.PutCalls()[0].body == "PROGRAM zerpl_t_prog.\n* changed");
    // The attributes are re-applied to the new inactive version, then activated.
    REQUIRE(p.writes.size() == 1);
    CHECK(p.writes[0].op.call.find("write_progdir = abap_true") != std::string::npos);
    CHECK(p.writes[0].op.call.find("write_cua = abap_false") != std::string::npos);
    CHECK(Activation(p) == "REPS");
}

TEST_CASE("PROG: a screen missing from the files is deleted", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles current;
    ProgFiles desired;
    desired.screens.pop_back();
    desired.flows.erase("0200");
    auto plan = Handler()->Plan(kKey, desired.Build(), Existing(current), c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"screen 0200 (deleted)"});
    CHECK(plan.Value().activate.empty());  // a deletion needs no activation
    CHECK(plan.Value().writes.at(0).op.payload.find("<DELETE_SCREENS><item>0200</item>") !=
          std::string::npos);
}

TEST_CASE("PROG: unsupported parts are refused by name", "[workflow][abapgit_prog]") {
    Ctx c;
    SECTION("VARIS") {
        ProgFiles files;
        files.extra = "<VARIS><item><VARIANT>SAP&amp;DEFAULT</VARIANT></item></VARIS>";
        auto plan = Handler()->Plan(kKey, files.Build(), ObjectState{}, c.ctx);
        REQUIRE(plan.IsErr());
        CHECK(plan.Error().message.find("VARIS") != std::string::npos);
    }
    SECTION("native dynpro") {
        ProgFiles files;
        files.screens[0] = "<item><HEADER><SCREEN>0100</SCREEN></HEADER><NAT_HEADER><PROG>X</PROG>"
                           "</NAT_HEADER></item>";
        auto plan = Handler()->Plan(kKey, files.Build(), ObjectState{}, c.ctx);
        REQUIRE(plan.IsErr());
        CHECK(plan.Error().message.find("screen 0100") != std::string::npos);
        CHECK(plan.Error().message.find("native dynpro") != std::string::npos);
    }
    CHECK(c.engine.runs.empty());
}

TEST_CASE("PROG: a flow logic file without its screen is refused", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles files;
    files.flows["0300"] = "PROCESS BEFORE OUTPUT.\n";
    auto plan = Handler()->Plan(kKey, files.Build(), ObjectState{}, c.ctx);
    REQUIRE(plan.IsErr());
    CHECK(plan.Error().message.find("screen_0300") != std::string::npos);
}

TEST_CASE("PROG: delete is RS_DELETE_PROGRAM with package and transport",
          "[workflow][abapgit_prog]") {
    Ctx c;
    c.ctx.transport = "A4HK900001";
    auto deleted = Handler()->Delete(kKey, c.ctx);
    REQUIRE(deleted.IsOk());
    CHECK(c.engine.runs.at(0).at(0).call ==
          "prog_delete( name = 'ZERPL_T_PROG' devclass = '$TMP' trkorr = 'A4HK900001' ).");
}

TEST_CASE("PROG: inactive leftovers are rewritten and activated", "[workflow][abapgit_prog]") {
    Ctx c;
    ProgFiles files;
    auto current = Existing(files);
    current.pending = {"CUAD", "DYNP0100"};
    auto plan = Handler()->Plan(kKey, files.Build(), current, c.ctx);
    REQUIRE(plan.IsOk());
    CHECK(plan.Value().parts == std::vector<std::string>{"screen 0100", "CUA"});
    CHECK(Activation(plan.Value()) == "DYNP0100 CUAD");
}

TEST_CASE("PROG: pending inactive parts come from the engine's PENDING data",
          "[workflow][abapgit_prog]") {
    Ctx c;
    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    r.data["PENDING"] = "CUAD DYNP0100 REPS";
    auto state = Handler()->BuildState(kKey, {r}, c.ctx);
    REQUIRE(state.IsOk());
    CHECK(state.Value().pending == std::vector<std::string>{"CUAD", "DYNP0100", "REPS"});
}

TEST_CASE("PROG: last changers per part (source, texts, CUA known; dynpros unknown)",
          "[workflow][abapgit_prog][drift]") {
    Ctx c;
    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    r.changed_by = "DEVELOPER";
    r.data["XML"] = "<?xml version=\"1.0\" encoding=\"utf-16\"?><asx:abap "
                    "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
                    "<PROGDIR><NAME>ZERPL_T_PROG</NAME></PROGDIR></asx:values></asx:abap>";
    r.data["SOURCE"] = "REPORT zerpl_t_prog.\n";
    r.data["SCREEN_0100"] = "PROCESS BEFORE OUTPUT.\n";
    r.values["CHANGED"] = {"SOURCE DEVELOPER", "TEXTS COLLEAGUE", "CUA ?"};
    auto state = Handler()->BuildState(kKey, {r}, c.ctx);
    REQUIRE(state.IsOk());
    const auto& by = state.Value().part_changed_by;
    CHECK(by.at("zerpl_t_prog.prog.abap") == "DEVELOPER");
    CHECK(by.at("zerpl_t_prog.prog.xml#PROGDIR") == "DEVELOPER");
    CHECK(by.at("zerpl_t_prog.prog.xml#TPOOL") == "COLLEAGUE");
    CHECK(by.at("zerpl_t_prog.prog.xml#CUA").empty());
    CHECK(by.at("zerpl_t_prog.prog.xml").empty());  // DYNPROS & others: unknown
    CHECK(by.at("zerpl_t_prog.prog.screen_0100.abap").empty());
}

TEST_CASE("PROG: Validate finds file problems without touching the system",
          "[workflow][abapgit_prog][status]") {
    ProgFiles files;
    files.flows["0300"] = "PROCESS BEFORE OUTPUT.\n";
    auto desired = files.Build();
    auto checked = Handler()->Validate(kKey, desired, ObjectState{}, "E");
    REQUIRE(checked.IsErr());
    CHECK(checked.Error().message.find("screen_0300") != std::string::npos);

    ProgFiles no_source;
    auto without = no_source.Build();
    without.erase("zerpl_t_prog.prog.abap");
    auto missing = Handler()->Validate(kKey, without, ObjectState{}, "E");
    REQUIRE(missing.IsErr());
    CHECK(missing.Error().message.find("zerpl_t_prog.prog.abap") != std::string::npos);

    ProgFiles fine;
    CHECK(Handler()->Validate(kKey, fine.Build(), ObjectState{}, "E").IsOk());
}
