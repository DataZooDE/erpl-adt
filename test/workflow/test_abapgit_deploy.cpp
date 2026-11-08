#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <filesystem>
#include <fstream>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Hand-written fakes (repo convention: no mocking framework).

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

class FakeHistory final : public IFileHistory {
public:
    std::map<std::string, std::vector<std::string>> versions;  // file name -> old bytes
    std::vector<std::string> PreviousVersions(const std::filesystem::path& file) const override {
        auto it = versions.find(file.filename().string());
        return it == versions.end() ? std::vector<std::string>{} : it->second;
    }
};

// A handler whose "system" is an in-memory map, so the workflow's decisions
// can be checked without SAP.
class FakeHandler final : public IClassicTypeHandler {
public:
    explicit FakeHandler(std::string type) : type_(std::move(type)) {}
    std::map<std::string, ObjectFileSet>* system = nullptr;
    mutable std::vector<std::string> applied;
    mutable std::vector<std::string> deleted;
    std::string fail_apply_for;
    std::string changed_by = "DEVELOPER";

    std::string Type() const override { return type_; }
    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string&) const override {
        return {{"fake_get( '" + key.name + "' ).", ""}};
    }
    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext&) const override {
        REQUIRE(results.size() == 1);
        ObjectState state;
        auto it = system->find(key.name);
        if (it != system->end()) {
            state.exists = true;
            state.files = it->second;
            state.changed_by = changed_by;
        }
        return Result<ObjectState, Error>::Ok(state);
    }
    Result<std::vector<std::string>, Error> Apply(const AbapGitObjectKey& key,
                                                  const ObjectFileSet& desired,
                                                  const ObjectState&,
                                                  TypeContext&) const override {
        if (key.name == fail_apply_for) {
            Error err;
            err.message = "Object DOMA " + key.name + " is locked in request A4HK900012";
            err.category = ErrorCategory::LockConflict;
            return Result<std::vector<std::string>, Error>::Err(err);
        }
        applied.push_back(key.name);
        (*system)[key.name] = desired;
        return Result<std::vector<std::string>, Error>::Ok({"DD01V"});
    }
    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext&) const override {
        deleted.push_back(key.name);
        system->erase(key.name);
        return Result<void, Error>::Ok();
    }

private:
    std::string type_;
};

struct TempRepo {
    std::filesystem::path root;
    TempRepo() {
        root = std::filesystem::temp_directory_path() /
               ("erpl_abapgit_deploy_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);
    }
    ~TempRepo() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    AbapGitObjectFiles Doma(const std::string& name, const std::string& text) {
        const std::string file = name + ".doma.xml";
        std::ofstream(root / file, std::ios::binary)
            << "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<abapGit version=\"v1.0.0\" serializer=\"LCL_OBJECT_DOMA\" "
               "serializer_version=\"v1.0.0\">\n"
               " <asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n"
               "  <asx:values>\n   <DD01V>\n    <DDTEXT>"
            << text
            << "</DDTEXT>\n   </DD01V>\n  </asx:values>\n </asx:abap>\n</abapGit>\n";
        AbapGitObjectFiles obj;
        obj.key = {"DOMA", name == "zerpl_d_a" ? "ZERPL_D_A" : (name == "zerpl_d_b" ? "ZERPL_D_B" : name)};
        obj.package = "$TMP";
        obj.folder = root;
        obj.files["xml"] = root / file;
        return obj;
    }
};

struct Fixture {
    MockAdtSession session;
    FakeEngine engine;
    std::map<std::string, ObjectFileSet> system;
    std::shared_ptr<FakeHandler> handler = std::make_shared<FakeHandler>("DOMA");
    FakeHistory history;
    TempRepo repo;
    Fixture() { handler->system = &system; }
    AbapGitWorkflow Workflow() {
        return AbapGitWorkflow(session, engine, {{"DOMA", handler}}, history);
    }
    AbapGitRunOptions Options() {
        AbapGitRunOptions o;
        o.root_package = "$TMP";
        o.user = "DEVELOPER";
        return o;
    }
};

}  // namespace

TEST_CASE("abapGit deploy: new object is created", "[workflow][abapgit_deploy]") {
    Fixture f;
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options());
    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Created);
    CHECK(f.handler->applied == std::vector<std::string>{"ZERPL_D_A"});
    CHECK_FALSE(report.Failed());
}

TEST_CASE("abapGit deploy: identical object is left alone", "[workflow][abapgit_deploy]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    auto first = f.Workflow().Deploy({obj}, f.Options());
    REQUIRE(first.objects[0].action == ObjectAction::Created);
    f.handler->applied.clear();

    auto second = f.Workflow().Deploy({obj}, f.Options());

    CHECK(second.objects[0].action == ObjectAction::Unchanged);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit deploy: changed object is updated in place", "[workflow][abapgit_deploy]") {
    Fixture f;
    REQUIRE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options())
                .objects[0].action == ObjectAction::Created);
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Updated);
}

TEST_CASE("abapGit deploy: all states are read in one engine run", "[workflow][abapgit_deploy]") {
    Fixture f;
    auto report = f.Workflow().Deploy(
        {f.repo.Doma("zerpl_d_a", "A"), f.repo.Doma("zerpl_d_b", "B")}, f.Options());
    REQUIRE(f.engine.runs.size() == 1);
    CHECK(f.engine.runs[0].size() == 3);  // system info + one read per object
    CHECK(report.objects.size() == 2);
}

TEST_CASE("abapGit deploy: dry run reports but writes nothing", "[workflow][abapgit_deploy]") {
    Fixture f;
    auto options = f.Options();
    options.dry_run = true;
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, options);
    CHECK(report.objects[0].action == ObjectAction::Created);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit deploy: SAP objects are refused before anything is read",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.key.name = "XFELD";
    auto report = f.Workflow().Deploy({obj}, f.Options());
    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("XFELD") != std::string::npos);
    CHECK(f.engine.runs.empty());
    CHECK(report.Failed());
}

TEST_CASE("abapGit deploy: transportable package without transport is refused",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.package = "ZERPL_DEMO";
    auto options = f.Options();
    options.root_package = "ZERPL_DEMO";
    auto report = f.Workflow().Deploy({obj}, options);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("--transport") != std::string::npos);
    CHECK(f.engine.runs.empty());
}

TEST_CASE("abapGit deploy: a failing object is reported with SAP's reason",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    f.handler->fail_apply_for = "ZERPL_D_A";
    auto report = f.Workflow().Deploy(
        {f.repo.Doma("zerpl_d_a", "A"), f.repo.Doma("zerpl_d_b", "B")}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Failed);
    CHECK(report.objects[0].message.find("locked in request A4HK900012") != std::string::npos);
    CHECK(report.objects[1].action == ObjectAction::Created);
    REQUIRE(report.FirstError().has_value());
    CHECK(report.FirstError()->category == ErrorCategory::LockConflict);
}

TEST_CASE("abapGit delete: runs in reverse dependency order", "[workflow][abapgit_deploy]") {
    Fixture f;
    auto a = f.repo.Doma("zerpl_d_a", "A");
    auto b = f.repo.Doma("zerpl_d_b", "B");
    REQUIRE_FALSE(f.Workflow().Deploy({a, b}, f.Options()).Failed());
    auto report = f.Workflow().Delete({a, b}, f.Options());
    CHECK(f.handler->deleted == std::vector<std::string>{"ZERPL_D_B", "ZERPL_D_A"});
    CHECK(report.objects[0].action == ObjectAction::Deleted);
}

TEST_CASE("abapGit delete: an object that is not there is reported as missing",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto report = f.Workflow().Delete({f.repo.Doma("zerpl_d_a", "A")}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Missing);
    CHECK(f.handler->deleted.empty());
    CHECK_FALSE(report.Failed());
}

TEST_CASE("abapGit export: writes the files the system state holds",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    f.system["ZERPL_D_A"] = {{"zerpl_d_a.doma.xml", "<exported/>"}};
    TempRepo out;
    auto report = f.Workflow().Export({{"DOMA", "ZERPL_D_A"}}, out.root, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Exported);
    std::ifstream in(out.root / "zerpl_d_a.doma.xml", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "<exported/>");
}

TEST_CASE("LoadObjectFiles: XML is normalized to abapGit's exact rendering",
          "[workflow][abapgit_deploy]") {
    TempRepo repo;
    std::ofstream(repo.root / "zerpl_d_a.doma.xml", std::ios::binary)
        << "<?xml version=\"1.0\" encoding=\"utf-8\"?><abapGit version=\"v1.0.0\" "
           "serializer=\"LCL_OBJECT_DOMA\" serializer_version=\"v1.0.0\"><asx:abap "
           "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values><DD01V>"
           "<DDTEXT>A</DDTEXT></DD01V></asx:values></asx:abap></abapGit>";
    std::ofstream(repo.root / "zerpl_p.prog.abap", std::ios::binary) << "REPORT zerpl_p.\r\n";
    AbapGitObjectFiles obj;
    obj.key = {"DOMA", "ZERPL_D_A"};
    obj.folder = repo.root;
    obj.files["xml"] = repo.root / "zerpl_d_a.doma.xml";
    obj.files["abap"] = repo.root / "zerpl_p.prog.abap";

    auto files = LoadObjectFiles(obj);
    REQUIRE(files.IsOk());
    const auto& xml = files.Value().at("zerpl_d_a.doma.xml");
    CHECK(xml.rfind("\xEF\xBB\xBF<?xml", 0) == 0);
    CHECK(xml.find("\n   <DD01V>\n    <DDTEXT>A</DDTEXT>\n   </DD01V>\n") != std::string::npos);
    CHECK(files.Value().at("zerpl_d_a.doma.abap") == "REPORT zerpl_p.\n");
}

// ---------------------------------------------------------------------------
// Drift protection: never silently overwrite changes made in the system.
// ---------------------------------------------------------------------------

TEST_CASE("abapGit deploy: change made in the system by someone else is refused",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    // A colleague edits the domain in SE11; the files still hold another version.
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE11/>";
    f.handler->changed_by = "COLLEAGUE";
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("COLLEAGUE") != std::string::npos);
    CHECK(report.objects[0].message.find("--force") != std::string::npos);
    CHECK(report.objects[0].message.find("abapgit export") != std::string::npos);
    CHECK(f.handler->applied.empty());
    CHECK(report.FirstError()->category == ErrorCategory::LockConflict);
}

TEST_CASE("abapGit deploy: --force overwrites a drifted object",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE11/>";
    f.handler->changed_by = "COLLEAGUE";
    auto options = f.Options();
    options.force = true;
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, options);
    CHECK(report.objects[0].action == ObjectAction::Updated);
}

TEST_CASE("abapGit deploy: system holding an older git version is a normal update",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    auto old_obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE_FALSE(f.Workflow().Deploy({old_obj}, f.Options()).Failed());
    const auto deployed = f.system["ZERPL_D_A"].begin()->second;
    // Deployed by the CI user from an earlier commit of the same file.
    f.handler->changed_by = "CI_DEPLOY";
    f.history.versions["zerpl_d_a.doma.xml"] = {"<older/>", deployed};

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Updated);
}

TEST_CASE("abapGit deploy: own earlier change is a normal update",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Updated);
}

TEST_CASE("abapGit status: drift is reported as refused without writing",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    f.handler->changed_by = "COLLEAGUE";
    f.system["ZERPL_D_A"].begin()->second = "<edited/>";
    auto options = f.Options();
    options.dry_run = true;
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, options);
    CHECK(report.objects[0].action == ObjectAction::Refused);
}

TEST_CASE("abapGit deploy: namespaced object needs a namespace changeable in the system",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.key.name = "/ACME/D_STATUS";
    auto report = f.Workflow().Deploy({obj}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Refused);

    auto options = f.Options();
    options.customer_namespaces = {"/ACME/"};
    auto allowed = f.Workflow().Deploy({obj}, options);
    CHECK(allowed.objects[0].action == ObjectAction::Created);
}
