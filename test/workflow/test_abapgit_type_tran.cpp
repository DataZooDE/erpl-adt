#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

class RecordingEngine final : public IEngineRunner {
public:
    std::vector<std::vector<EngineOp>> runs;
    std::vector<EngineOpResult> next;  // empty: every op succeeds

    Result<std::vector<EngineOpResult>, Error> Run(const std::vector<EngineOp>& ops) override {
        runs.push_back(ops);
        if (!next.empty()) {
            return Result<std::vector<EngineOpResult>, Error>::Ok(next);
        }
        std::vector<EngineOpResult> ok(ops.size());
        for (auto& r : ok) {
            r.rc = 0;
        }
        return Result<std::vector<EngineOpResult>, Error>::Ok(ok);
    }
};

const char* kVa03Values =
    "<TSTC><TCODE>ZERPL_VA03</TCODE><PGMNA>ZERPL_SO_DISPLAY</PGMNA><DYPNO>0100</DYPNO></TSTC>"
    "<TSTCC><TCODE>ZERPL_VA03</TCODE><S_WEBGUI>1</S_WEBGUI><S_WIN32>X</S_WIN32>"
    "<S_PLATIN>X</S_PLATIN></TSTCC>"
    "<TSTCT><SPRSL>E</SPRSL><TCODE>ZERPL_VA03</TCODE><TTEXT>Display Sales Order</TTEXT></TSTCT>";

std::string TranFile(const std::string& values) {
    AbapGitXml xml;
    xml.serializer = "LCL_OBJECT_TRAN";
    xml.serializer_version = "v1.0.0";
    xml.values_xml = values;
    return RenderAbapGitXml(xml);
}

struct Fixture {
    MockAdtSession session;
    RecordingEngine engine;
    std::shared_ptr<const IClassicTypeHandler> handler = DefaultTypeHandlers().at("TRAN");
    AbapGitObjectKey key{"TRAN", "ZERPL_VA03"};
    TypeContext Ctx(const std::string& transport = "") {
        return TypeContext{session, engine, "ZERPL_DEMO", transport, "E"};
    }
    ObjectFileSet Files(const std::string& values) {
        return {{"zerpl_va03.tran.xml", TranFile(values)}};
    }
    std::string ReplaceText(const std::string& from, const std::string& to) {
        std::string v = kVa03Values;
        v.replace(v.find(from), from.size(), to);
        return v;
    }
};

}  // namespace

TEST_CASE("TRAN handler: is registered for abapGit type TRAN", "[workflow][abapgit_tran]") {
    auto handlers = DefaultTypeHandlers();
    REQUIRE(handlers.count("TRAN") == 1);
    CHECK(handlers.at("TRAN")->Type() == "TRAN");
}

TEST_CASE("TRAN handler: reads through one tran_get operation", "[workflow][abapgit_tran]") {
    Fixture f;
    auto ops = f.handler->ReadOps(f.key, "E");
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].call == "tran_get( name = 'ZERPL_VA03' language = 'E' ).");
    CHECK(ops[0].payload.empty());
}

TEST_CASE("TRAN handler: engine data becomes the abapGit file", "[workflow][abapgit_tran]") {
    Fixture f;
    EngineOpResult r;
    r.rc = 0;
    r.exists = true;
    r.changed_by = "DEVELOPER";
    r.data["XML"] = "<?xml version=\"1.0\" encoding=\"utf-16\"?><asx:abap xmlns:asx="
                    "\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>" +
                    std::string(kVa03Values) + "</asx:values></asx:abap>";
    auto ctx = f.Ctx();
    auto state = f.handler->BuildState(f.key, {r}, ctx);
    REQUIRE(state.IsOk());
    CHECK(state.Value().exists);
    CHECK(state.Value().changed_by == "DEVELOPER");
    CHECK(state.Value().files == f.Files(kVa03Values));
}

TEST_CASE("TRAN handler: a missing transaction is reported as not existing",
          "[workflow][abapgit_tran]") {
    Fixture f;
    EngineOpResult r;
    r.rc = 0;
    r.exists = false;
    auto ctx = f.Ctx();
    auto state = f.handler->BuildState(f.key, {r}, ctx);
    REQUIRE(state.IsOk());
    CHECK_FALSE(state.Value().exists);
    CHECK(state.Value().files.empty());
}

TEST_CASE("TRAN handler: create inserts with the transport and all four parts",
          "[workflow][abapgit_tran]") {
    Fixture f;
    auto ctx = f.Ctx("A4HK900042");
    auto parts = f.handler->Apply(f.key, f.Files(kVa03Values), ObjectState{}, ctx);
    REQUIRE(parts.IsOk());
    CHECK(parts.Value() == std::vector<std::string>{"TSTC", "TSTCC", "TSTCT"});
    REQUIRE(f.engine.runs.size() == 1);
    REQUIRE(f.engine.runs[0].size() == 1);
    const auto& op = f.engine.runs[0][0];
    CHECK(op.call ==
          "tran_put( name = 'ZERPL_VA03' language = 'E' devclass = 'ZERPL_DEMO' "
          "trkorr = 'A4HK900042' ).");
    // TSTCP is absent in a dialog transaction's file; the engine still gets
    // an (empty) element so its transformation finds every part.
    auto values = ExtractAsxValues(op.payload);
    REQUIRE(values.IsOk());
    CHECK(values.Value() == std::string(kVa03Values) + "<TSTCP/>");
}

TEST_CASE("TRAN handler: an update reports only the parts that differ",
          "[workflow][abapgit_tran]") {
    Fixture f;
    ObjectState current;
    current.exists = true;
    current.files = f.Files(kVa03Values);
    auto ctx = f.Ctx();
    auto desired = f.Files(f.ReplaceText("Display Sales Order", "Display Sales Order (EPM)"));
    auto parts = f.handler->Apply(f.key, desired, current, ctx);
    REQUIRE(parts.IsOk());
    CHECK(parts.Value() == std::vector<std::string>{"TSTCT"});
}

TEST_CASE("TRAN handler: parameter transaction passes TSTCP through",
          "[workflow][abapgit_tran]") {
    Fixture f;
    AbapGitObjectKey key{"TRAN", "ZERPL_SO_LIST"};
    const std::string values =
        "<TSTC><TCODE>ZERPL_SO_LIST</TCODE><CINFO>Ag==</CINFO></TSTC>"
        "<TSTCT><SPRSL>E</SPRSL><TCODE>ZERPL_SO_LIST</TCODE><TTEXT>Sales Order Table</TTEXT>"
        "</TSTCT><TSTCP><TCODE>ZERPL_SO_LIST</TCODE>"
        "<PARAM>/*SE16 DATABROWSE-TABLENAME=SNWD_SO;</PARAM></TSTCP>";
    auto ctx = f.Ctx();
    auto parts = f.handler->Apply(key, {{"zerpl_so_list.tran.xml", TranFile(values)}},
                                  ObjectState{}, ctx);
    REQUIRE(parts.IsOk());
    CHECK(parts.Value() == std::vector<std::string>{"TSTC", "TSTCT", "TSTCP"});
    auto payload = ExtractAsxValues(f.engine.runs.at(0).at(0).payload);
    REQUIRE(payload.IsOk());
    CHECK(payload.Value().find("<TSTCC/>") != std::string::npos);
    CHECK(payload.Value().find("/*SE16 DATABROWSE-TABLENAME=SNWD_SO;") != std::string::npos);
}

TEST_CASE("TRAN handler: parts only reachable by table writes are refused, not dropped",
          "[workflow][abapgit_tran]") {
    Fixture f;
    auto ctx = f.Ctx();

    SECTION("authorization objects (TSTCA)") {
        auto desired = f.Files(std::string(kVa03Values) +
                               "<AUTHORIZATIONS><TSTCA><TCODE>ZERPL_VA03</TCODE><OBJCT>S_TCODE"
                               "</OBJCT></TSTCA></AUTHORIZATIONS>");
        auto r = f.handler->Apply(f.key, desired, ObjectState{}, ctx);
        REQUIRE(r.IsErr());
        CHECK(r.Error().message.find("AUTHORIZATIONS") != std::string::npos);
    }
    SECTION("report transaction with variant (CINFO x'90')") {
        auto desired = f.Files(f.ReplaceText("<DYPNO>0100</DYPNO>",
                                             "<DYPNO>1000</DYPNO><CINFO>kA==</CINFO>"));
        auto r = f.handler->Apply(f.key, desired, ObjectState{}, ctx);
        REQUIRE(r.IsErr());
        CHECK(r.Error().message.find("CINFO x'90' (report transaction with variant)") !=
              std::string::npos);
    }
    SECTION("OO transaction (CINFO x'08')") {
        auto desired = f.Files(f.ReplaceText("<DYPNO>0100</DYPNO>",
                                             "<DYPNO>0100</DYPNO><CINFO>CA==</CINFO>"));
        auto r = f.handler->Apply(f.key, desired, ObjectState{}, ctx);
        REQUIRE(r.IsErr());
        CHECK(r.Error().message.find("OO transaction") != std::string::npos);
    }
    SECTION("translations") {
        auto desired = f.Files(std::string(kVa03Values) +
                               "<I18N_TPOOL><TSTCT><SPRSL>D</SPRSL><TTEXT>Kundenauftrag"
                               "</TTEXT></TSTCT></I18N_TPOOL>");
        auto r = f.handler->Apply(f.key, desired, ObjectState{}, ctx);
        REQUIRE(r.IsErr());
        CHECK(r.Error().message.find("I18N_TPOOL") != std::string::npos);
    }
    SECTION("a file whose TCODE is another transaction") {
        auto desired = f.Files(f.ReplaceText("<TCODE>ZERPL_VA03</TCODE><PGMNA>",
                                             "<TCODE>SE38</TCODE><PGMNA>"));
        auto r = f.handler->Apply(f.key, desired, ObjectState{}, ctx);
        REQUIRE(r.IsErr());
        CHECK(r.Error().message.find("TSTC-TCODE SE38") != std::string::npos);
    }
    CHECK(f.engine.runs.empty());
}

TEST_CASE("TRAN handler: SAP's reason is surfaced when the insert fails",
          "[workflow][abapgit_tran]") {
    Fixture f;
    EngineOpResult failed;
    failed.rc = 3;
    failed.messages.push_back({"E", "SEUK", "081", "Transaction ZERPL_VA03 is locked by FOO"});
    f.engine.next = {failed};
    auto ctx = f.Ctx();
    auto r = f.handler->Apply(f.key, f.Files(kVa03Values), ObjectState{}, ctx);
    REQUIRE(r.IsErr());
    CHECK(r.Error().message.find("locked by FOO") != std::string::npos);
    CHECK(r.Error().category == ErrorCategory::LockConflict);
}

TEST_CASE("TRAN handler: delete runs tran_delete with the transport",
          "[workflow][abapgit_tran]") {
    Fixture f;
    auto ctx = f.Ctx("A4HK900042");
    REQUIRE(f.handler->Delete(f.key, ctx).IsOk());
    REQUIRE(f.engine.runs.size() == 1);
    CHECK(f.engine.runs[0].at(0).call ==
          "tran_delete( name = 'ZERPL_VA03' trkorr = 'A4HK900042' ).");
}
