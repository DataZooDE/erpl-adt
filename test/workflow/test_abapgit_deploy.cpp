#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Hand-written fakes (repo convention: no mocking framework).

std::string Between(const std::string& s, const std::string& from, const std::string& to) {
    auto a = s.find(from);
    if (a == std::string::npos) {
        return "";
    }
    a += from.size();
    auto b = s.find(to, a);
    return b == std::string::npos ? "" : s.substr(a, b - a);
}

// The engine as the workflow sees it, with the batching semantics of the ABAP
// base class: a guarded op is skipped when an op of a required owner failed
// earlier in the run, a failed op marks its owner failed, activate_objects
// leaves what `fail_activation_for` names inactive.
class FakeEngine final : public IEngineRunner {
public:
    std::vector<std::vector<EngineOp>> runs;
    std::vector<std::vector<EngineOpResult>> scripted;
    std::vector<std::string> namespaces;  // changeable in the "system" (SE03)
    // Object name -> TADIR package and its parents ("ZROOT_SUB ZROOT").
    std::map<std::string, std::string> package_chains;
    // Executes a (non-read) operation's inner call, e.g. "fake_put( 'DOMA' 'ZA' ).".
    std::function<EngineOpResult(const EngineOp&)> execute;
    std::set<std::string> fail_activation_for;           // owners ("PROG ZA")
    std::vector<std::vector<WorkingObject>> activations;  // per activate_objects op
    std::optional<Error> fail_run;                        // the whole run fails
    size_t fail_run_at = 0;                               // ... this run (0-based)

    Result<std::vector<EngineOpResult>, Error> Run(const std::vector<EngineOp>& ops) override {
        runs.push_back(ops);
        if (fail_run && runs.size() - 1 == fail_run_at) {
            return Result<std::vector<EngineOpResult>, Error>::Err(*fail_run);
        }
        if (!scripted.empty()) {
            auto next = scripted.front();
            scripted.erase(scripted.begin());
            return Result<std::vector<EngineOpResult>, Error>::Ok(next);
        }
        std::set<std::string> failed;
        std::vector<EngineOpResult> out(ops.size());
        for (size_t i = 0; i < ops.size(); ++i) {
            auto& r = out[i];
            r.rc = 0;
            EngineOp op = ops[i];
            std::string owner;
            if (op.call.rfind("IF op_guard(", 0) == 0) {
                owner = Between(op.call, "op_owner = '", "'");
                std::string required = Between(op.call, "requires = '", "'");
                std::string blocked;
                size_t start = 0;
                while (start <= required.size() && blocked.empty()) {
                    auto comma = required.find(',', start);
                    auto one = required.substr(start, comma == std::string::npos
                                                          ? std::string::npos
                                                          : comma - start);
                    if (!one.empty() && failed.count(one) != 0) {
                        blocked = one;
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    start = comma + 1;
                }
                if (!blocked.empty()) {
                    r.rc = 1;
                    r.values["SKIP"] = {blocked};
                    failed.insert(owner);
                    continue;
                }
                op.call = Between(op.call, "abap_true.\n      ", "\n    ENDIF.");
            }
            if (op.call == "system_info( ).") {
                r.changed_by = "DEVELOPER";
                r.values["NS"] = namespaces;
            } else if (op.call.rfind("tadir_package(", 0) == 0) {
                for (const auto& [name, chain] : package_chains) {
                    if (op.call.find("'" + name + "'") != std::string::npos) {
                        r.values["PACKAGE"] = {chain};
                    }
                }
            } else if (op.call == "activate_objects( ).") {
                std::vector<WorkingObject> entries;
                std::istringstream lines(op.payload);
                std::string line;
                while (std::getline(lines, line)) {
                    auto t1 = line.find('\t');
                    auto t2 = line.find('\t', t1 + 1);
                    WorkingObject w{line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1),
                                    line.substr(t2 + 1)};
                    if (failed.count(w.owner) != 0) {
                        continue;
                    }
                    if (fail_activation_for.count(w.owner) != 0) {
                        r.values["INACTIVE"].push_back(w.owner + "\t" + w.object + " " +
                                                       w.obj_name);
                        r.errors.push_back("Program " + w.owner.substr(5) +
                                           ": Field \"GV_MISSING\" is unknown.");
                        failed.insert(w.owner);
                        r.rc = 8;
                    }
                    entries.push_back(w);
                }
                activations.push_back(entries);
            } else if (execute && op.call.rfind("fake_get(", 0) != 0) {
                r = execute(op);
            }
            if (!owner.empty() && r.rc != 0) {
                failed.insert(owner);
            }
        }
        return Result<std::vector<EngineOpResult>, Error>::Ok(out);
    }
};

class FakeRecord final : public IDeployRecord {
public:
    std::map<std::string, ObjectFileSet> objects;  // "TYPE NAME" -> files
    std::optional<std::string> LastDeployed(const AbapGitObjectKey& key,
                                            const std::string& file) const override {
        auto it = objects.find(key.type + " " + key.name);
        if (it == objects.end() || it->second.count(file) == 0) {
            return std::nullopt;
        }
        return it->second.at(file);
    }
    void Remember(const AbapGitObjectKey& key, const ObjectFileSet& files) override {
        objects[key.type + " " + key.name] = files;
    }
    void Forget(const AbapGitObjectKey& key) override { objects.erase(key.type + " " + key.name); }
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
// can be checked without SAP. Its plan writes in `phase`: "ddic" (an op that
// activates itself), "adt" (an ADT request), "write" (an op saved inactive
// plus a REPS working object to activate) or "write-only" (an op that needs
// no activation, like a transaction).
class FakeHandler final : public IClassicTypeHandler {
public:
    explicit FakeHandler(std::string type, std::string phase = "write")
        : type_(std::move(type)), phase_(std::move(phase)) {}
    std::map<std::string, ObjectFileSet>* system = nullptr;
    std::vector<std::string>* log = nullptr;  // shared order of writes, "ADT ZA" / "ENGINE ZA"
    mutable std::vector<std::string> applied;
    mutable std::vector<std::string> deleted;
    mutable std::map<std::string, ObjectFileSet> planned;
    std::string fail_apply_for;
    std::string invalid_for;  // Validate() refuses this object
    std::string changed_by = "DEVELOPER";
    std::map<std::string, std::string> part_changed_by;  // file[#SECTION] -> user
    std::vector<std::string> pending;  // reported as inactive leftovers
    bool after_activation = false;     // also plans a write that needs it active (variants)
    bool writes_translations = true;   // false: like TRAN, an update drops them

    bool KeepsUnmanagedTranslations() const override { return writes_translations; }

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
            state.part_changed_by = part_changed_by;
            state.pending = pending;
        }
        return Result<ObjectState, Error>::Ok(state);
    }
    Result<DeployPlan, Error> Plan(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                   const ObjectState&, const TypeContext&) const override {
        planned[key.name] = desired;
        DeployPlan plan;
        plan.parts = {"DD01V"};
        const EngineOp put{"fake_put( '" + type_ + "' '" + key.name + "' ).", ""};
        if (phase_ == "ddic") {
            plan.ddic.push_back({put, "Writing " + key.name});
        } else if (phase_ == "write" || phase_ == "write-only") {
            plan.writes.push_back({put, "Writing " + key.name});
            if (phase_ == "write") {
                plan.activate.push_back({"", "REPS", key.name});
            }
            if (after_activation) {
                plan.after_activation.push_back(
                    {{"fake_after( '" + type_ + "' '" + key.name + "' ).", ""},
                     "Writing the variants of " + key.name});
            }
        } else {
            const std::string name = key.name;
            plan.parts.clear();
            plan.adt = [this, name](TypeContext&) -> Result<std::vector<std::string>, Error> {
                auto r = Execute(name);
                if (r.rc != 0) {
                    Error err;
                    err.message = "Writing " + name + ": " + r.messages.at(0).text;
                    err.category = ErrorCategory::LockConflict;
                    return Result<std::vector<std::string>, Error>::Err(err);
                }
                return Result<std::vector<std::string>, Error>::Ok({"function group"});
            };
        }
        return Result<DeployPlan, Error>::Ok(plan);
    }
    // Writes one object into the fake system (an engine op or an ADT request).
    EngineOpResult Execute(const std::string& name) const {
        EngineOpResult r;
        r.rc = 0;
        if (log != nullptr) {
            log->push_back((phase_ == "adt" ? "ADT " : "ENGINE ") + name);
        }
        if (name == fail_apply_for) {
            r.rc = 4;
            EngineMessage m;
            m.type = "E";
            m.text = "Object " + type_ + " " + name + " is locked in request A4HK900012";
            r.messages.push_back(m);
            r.values["KIND"] = {"LOCK"};
            return r;
        }
        applied.push_back(name);
        (*system)[name] = planned[name];
        return r;
    }
    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet&,
                                 const ObjectState&, const std::string&) const override {
        if (key.name == invalid_for) {
            Error err;
            err.message = "DOMA " + key.name + " has parts the deploy cannot write: VARIS";
            err.category = ErrorCategory::CheckError;
            return Result<void, Error>::Err(err);
        }
        return Result<void, Error>::Ok();
    }
    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext&) const override {
        deleted.push_back(key.name);
        system->erase(key.name);
        return Result<void, Error>::Ok();
    }

private:
    std::string type_;
    std::string phase_;
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
    std::vector<std::string> log;
    std::shared_ptr<FakeHandler> handler = std::make_shared<FakeHandler>("DOMA");
    std::map<std::string, std::shared_ptr<FakeHandler>> handlers{{"DOMA", handler}};
    FakeHistory history;
    FakeRecord record;
    TempRepo repo;
    Fixture() {
        handler->system = &system;
        engine.execute = [this](const EngineOp& op) {
            if (op.call.rfind("fake_after(", 0) == 0) {
                log.push_back("AFTER " + Between(op.call, "' '", "'"));
                EngineOpResult r;
                r.rc = 0;
                return r;
            }
            // fake_put( 'TYPE' 'NAME' ).
            const auto type = Between(op.call, "fake_put( '", "'");
            const auto name = Between(op.call, "' '", "'");
            return handlers.at(type)->Execute(name);
        };
    }
    // An additional handler (e.g. a "ddic" DOMA and a "write" PROG).
    std::shared_ptr<FakeHandler> Add(const std::string& type, const std::string& phase) {
        auto h = std::make_shared<FakeHandler>(type, phase);
        h->system = &system;
        h->log = &log;
        handlers[type] = h;
        if (type == "DOMA") {
            handler = h;
        }
        return h;
    }
    AbapGitWorkflow Workflow() {
        std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> all;
        for (const auto& [type, h] : handlers) {
            all[type] = h;
        }
        return AbapGitWorkflow(session, engine, all, history, record);
    }
    AbapGitRunOptions Options() {
        AbapGitRunOptions o;
        o.root_package = "$TMP";
        o.user = "DEVELOPER";
        return o;
    }
};

// DOMA in the DDIC phase, FUGR over ADT, PROG and TRAN in the write run.
struct BatchFixture : Fixture {
    std::shared_ptr<FakeHandler> doma = Add("DOMA", "ddic");
    std::shared_ptr<FakeHandler> fugr = Add("FUGR", "adt");
    std::shared_ptr<FakeHandler> prog = Add("PROG", "write");
    std::shared_ptr<FakeHandler> tran = Add("TRAN", "write-only");

    AbapGitObjectFiles Object(const std::string& type, const std::string& name,
                              const std::string& text) {
        auto o = repo.Doma(name, text);
        o.key = {type, name};
        return o;
    }
    std::vector<AbapGitObjectFiles> Demo() {
        return {Object("DOMA", "ZD_A", "A"), Object("DOMA", "ZD_B", "B"),
                Object("FUGR", "ZFG", "uses ZD_A"), Object("PROG", "ZP_A", "uses ZD_B"),
                Object("PROG", "ZP_B", "B"), Object("TRAN", "ZT_A", "calls ZP_A")};
    }
    // The run's operations without the guard (just the inner calls).
    static std::vector<std::string> Calls(const std::vector<EngineOp>& ops) {
        std::vector<std::string> out;
        for (const auto& op : ops) {
            out.push_back(op.call.rfind("IF op_guard(", 0) == 0
                              ? Between(op.call, "abap_true.\n      ", "\n    ENDIF.")
                              : op.call);
        }
        return out;
    }
};

// An object named with --object: no folder yet, the export derives it.
AbapGitObjectFiles Wanted(const std::string& type, const std::string& name) {
    AbapGitObjectFiles o;
    o.key = {type, name};
    return o;
}

// Repository at `root` with abapGit's default layout (PREFIX, /src/). The
// root package is AbapGitRunOptions::root_package (`package`, for the reader).
AbapGitExportTarget Target(const std::filesystem::path& root, const std::string& /*package*/) {
    AbapGitExportTarget t;
    t.repo_root = root;
    return t;
}

void Touch(const std::filesystem::path& file, const std::string& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}

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

TEST_CASE("abapGit deploy: identical files with inactive leftovers are still applied",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE(f.Workflow().Deploy({obj}, f.Options()).objects[0].action == ObjectAction::Created);
    f.handler->applied.clear();
    f.handler->pending = {"DYNP0100"};

    auto report = f.Workflow().Deploy({obj}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Updated);
    CHECK(f.handler->applied == std::vector<std::string>{"ZERPL_D_A"});
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
    REQUIRE(f.engine.runs.size() == 2);   // the read run, then one write run
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
    f.engine.package_chains["ZERPL_D_A"] = "$TMP";
    TempRepo out;
    auto report = f.Workflow().Export({Wanted("DOMA", "ZERPL_D_A")}, Target(out.root, "$TMP"),
                                      f.Options());
    CHECK(report.objects[0].action == ObjectAction::Exported);
    std::ifstream in(out.root / "src" / "zerpl_d_a.doma.xml", std::ios::binary);
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
          "[workflow][abapgit_deploy][guard]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.key.name = "/ACME/D_STATUS";
    auto report = f.Workflow().Deploy({obj}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(f.handler->applied.empty());

    f.engine.namespaces = {"/ACME/"};
    auto allowed = f.Workflow().Deploy({obj}, f.Options());
    CHECK(allowed.objects[0].action == ObjectAction::Created);
}

TEST_CASE("abapGit deploy: Z object into a changeable /NS/ package is allowed",
          "[workflow][abapgit_deploy][guard]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.package = "/ACME/DEMO";
    auto options = f.Options();
    options.root_package = "/ACME/DEMO";
    options.transport = "A4HK900042";

    auto refused = f.Workflow().Deploy({obj}, options);
    REQUIRE(refused.objects.size() == 1);
    CHECK(refused.objects[0].action == ObjectAction::Refused);
    CHECK(refused.objects[0].message.find("/ACME/") != std::string::npos);
    CHECK(f.handler->applied.empty());

    f.engine.namespaces = {"/ACME/"};
    auto allowed = f.Workflow().Deploy({obj}, options);
    CHECK(allowed.objects[0].action == ObjectAction::Created);
}

TEST_CASE("abapGit delete: namespace not changeable in the system is refused",
          "[workflow][abapgit_deploy][guard]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.key.name = "/SAPX/D_STATUS";
    f.system["/SAPX/D_STATUS"] = {{"x", "y"}};
    auto report = f.Workflow().Delete({obj}, f.Options());
    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(f.handler->deleted.empty());
    CHECK(report.FirstError()->category == ErrorCategory::Authorization);
}

TEST_CASE("abapGit delete: object changed in the system by someone else is refused",
          "[workflow][abapgit_deploy][guard][drift]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE_FALSE(f.Workflow().Deploy({obj}, f.Options()).Failed());
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE11/>";
    f.handler->changed_by = "COLLEAGUE";

    auto report = f.Workflow().Delete({obj}, f.Options());
    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("COLLEAGUE") != std::string::npos);
    CHECK(f.handler->deleted.empty());
    CHECK(report.FirstError()->category == ErrorCategory::LockConflict);

    auto options = f.Options();
    options.force = true;
    auto forced = f.Workflow().Delete({obj}, options);
    CHECK(forced.objects[0].action == ObjectAction::Deleted);
    CHECK(f.handler->deleted == std::vector<std::string>{"ZERPL_D_A"});
}

TEST_CASE("abapGit delete: object equal to the files is deleted even if a colleague saved it",
          "[workflow][abapgit_deploy][guard][drift]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE_FALSE(f.Workflow().Deploy({obj}, f.Options()).Failed());
    f.handler->changed_by = "COLLEAGUE";
    auto report = f.Workflow().Delete({obj}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Deleted);
}

TEST_CASE("abapGit export: objects outside the customer namespaces are refused",
          "[workflow][abapgit_deploy][guard]") {
    Fixture f;
    f.system["/SAPX/D"] = {{"_sapx_d.doma.xml", "<x/>"}};
    f.system["XFELD"] = {{"xfeld.doma.xml", "<x/>"}};
    TempRepo out;
    auto report = f.Workflow().Export({Wanted("DOMA", "/SAPX/D"), Wanted("DOMA", "XFELD")},
                                      Target(out.root, "$TMP"), f.Options());
    REQUIRE(report.objects.size() == 2);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[1].action == ObjectAction::Refused);
    CHECK(std::filesystem::is_empty(out.root));
}

// ---------------------------------------------------------------------------
// Drift protection when SAP's last-changer is unknown or per part.
// ---------------------------------------------------------------------------

TEST_CASE("abapGit deploy: unknown last changer is unsafe without git history",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    // A transaction: SAP records no last changer. Someone edited it in SE93.
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE93/>";
    f.handler->changed_by = "";
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("unknown") != std::string::npos);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit deploy: what erpl-adt last deployed counts as the deployer's own",
          "[workflow][abapgit_deploy][drift][record]") {
    Fixture f;
    // First deploy of an uncommitted change (no git history at all).
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    REQUIRE(f.record.objects.count("DOMA ZERPL_D_A") == 1);
    f.handler->changed_by = "";  // like a screen: SAP records no last changer
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Updated);
    CHECK(f.handler->applied == std::vector<std::string>{"ZERPL_D_A"});
    // ... and the record follows the new version.
    CHECK(f.record.objects.at("DOMA ZERPL_D_A").begin()->second.find("Order status") !=
          std::string::npos);
}

TEST_CASE("abapGit deploy: a colleague's change after our deploy is still drift",
          "[workflow][abapgit_deploy][drift][record]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE51 by a colleague/>";
    f.handler->changed_by = "";
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit deploy: only objects that were written are recorded",
          "[workflow][abapgit_deploy][drift][record]") {
    BatchFixture f;
    f.prog->fail_apply_for = "ZP_A";
    auto options = f.Options();

    auto report = f.Workflow().Deploy(f.Demo(), options);

    CHECK(f.record.objects.count("DOMA ZD_A") == 1);
    CHECK(f.record.objects.count("PROG ZP_B") == 1);
    CHECK(f.record.objects.count("PROG ZP_A") == 0);  // failed
    CHECK(f.record.objects.count("TRAN ZT_A") == 0);  // skipped
    // A dry run records nothing.
    BatchFixture dry;
    options.dry_run = true;
    REQUIRE_FALSE(dry.Workflow().Deploy(dry.Demo(), options).Failed());
    CHECK(dry.record.objects.empty());
}

TEST_CASE("abapGit delete: the record of a deleted object is dropped",
          "[workflow][abapgit_deploy][drift][record]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE_FALSE(f.Workflow().Deploy({obj}, f.Options()).Failed());
    REQUIRE(f.record.objects.count("DOMA ZERPL_D_A") == 1);
    REQUIRE_FALSE(f.Workflow().Delete({obj}, f.Options()).Failed());
    CHECK(f.record.objects.empty());
}

TEST_CASE("FileDeployRecord: per system, object and file; exact bytes",
          "[workflow][abapgit_deploy][record]") {
    TempRepo dir;
    FileDeployRecord a4h(dir.root / "deployed", "localhost:50000/001");
    FileDeployRecord other(dir.root / "deployed", "localhost:50000/002");
    const AbapGitObjectKey prog{"PROG", "/ACME/ZFOO"};
    const std::string bytes = "PROCESS BEFORE OUTPUT.\r\n\xEF\xBB\xBF\x00end";
    CHECK_FALSE(a4h.LastDeployed(prog, "#acme#zfoo.prog.xml").has_value());

    a4h.Remember(prog, {{"#acme#zfoo.prog.screen_0100.abap", bytes}, {"#acme#zfoo.prog.xml", "x"}});

    CHECK(a4h.LastDeployed(prog, "#acme#zfoo.prog.screen_0100.abap") == bytes);
    CHECK_FALSE(other.LastDeployed(prog, "#acme#zfoo.prog.xml").has_value());
    // A new deploy replaces the whole record (a part that is gone is forgotten).
    a4h.Remember(prog, {{"#acme#zfoo.prog.xml", "y"}});
    CHECK(a4h.LastDeployed(prog, "#acme#zfoo.prog.xml") == std::string("y"));
    CHECK_FALSE(a4h.LastDeployed(prog, "#acme#zfoo.prog.screen_0100.abap").has_value());
    a4h.Forget(prog);
    CHECK_FALSE(a4h.LastDeployed(prog, "#acme#zfoo.prog.xml").has_value());
    // Names never escape the record directory.
    a4h.Remember({"PROG", "../../ZEVIL"}, {{"../x", "z"}});
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.root)) {
        CHECK(entry.path().string().rfind((dir.root / "deployed").string(), 0) == 0);
    }
}

TEST_CASE("FileDeployRecord: kept inside the git directory, never in the work tree",
          "[workflow][abapgit_deploy][record]") {
    TempRepo repo;
    std::filesystem::create_directories(repo.root / ".git");
    std::filesystem::create_directories(repo.root / "src" / "sub");
    CHECK(FileDeployRecord::DefaultDirectory(repo.root / "src" / "sub") ==
          repo.root / ".git" / "erpl-adt" / "deployed");

    // A worktree / submodule: .git is a file naming the git directory.
    TempRepo worktree;
    std::ofstream(worktree.root / ".git") << "gitdir: " << (repo.root / ".git" / "worktrees" / "w").string()
                                          << "\n";
    CHECK(FileDeployRecord::DefaultDirectory(worktree.root) ==
          repo.root / ".git" / "worktrees" / "w" / "erpl-adt" / "deployed");

#ifndef _WIN32
    TempRepo plain;
    ::setenv("XDG_CACHE_HOME", (plain.root / "cache").string().c_str(), 1);
    CHECK(FileDeployRecord::DefaultDirectory(std::filesystem::path("/")) ==
          plain.root / "cache" / "erpl-adt" / "deployed");
    ::unsetenv("XDG_CACHE_HOME");
#endif
}

TEST_CASE("abapGit deploy: unknown last changer is fine when the system version is in git",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    const auto deployed = f.system["ZERPL_D_A"].begin()->second;
    f.handler->changed_by = "";
    f.history.versions["zerpl_d_a.doma.xml"] = {deployed};
    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());
    CHECK(report.objects[0].action == ObjectAction::Updated);
}

TEST_CASE("abapGit deploy: creator is the deployer but a colleague edited a part -> refused",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    // Object-level metadata names the deployer (e.g. the main program), but
    // the part that differs (an FM include) was last saved by a colleague.
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE37/>";
    f.handler->changed_by = "DEVELOPER";
    f.handler->part_changed_by["zerpl_d_a.doma.xml"] = "COLLEAGUE";
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());

    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("COLLEAGUE") != std::string::npos);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit deploy: the changer of the differing XML section decides",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    // The system version differs from the files only in DD01V; DD01V was last
    // changed by the deployer, another section (unchanged) by a colleague.
    f.handler->changed_by = "";
    f.handler->part_changed_by["zerpl_d_a.doma.xml#DD01V"] = "DEVELOPER";
    f.handler->part_changed_by["zerpl_d_a.doma.xml#DD07V_TAB"] = "COLLEAGUE";
    auto ok = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, f.Options());
    CHECK(ok.objects[0].action == ObjectAction::Updated);

    // Now the differing section has no known changer (like a dynpro) and the
    // system version is not what erpl-adt deployed here last: refused.
    f.handler->part_changed_by.erase("zerpl_d_a.doma.xml#DD01V");
    f.record.objects.clear();
    auto refused = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Again")}, f.Options());
    CHECK(refused.objects[0].action == ObjectAction::Refused);
}

// ---------------------------------------------------------------------------
// Export into the repository layout (sub-packages, obsolete files).
// ---------------------------------------------------------------------------

TEST_CASE("abapGit export: refresh writes into each object's folder; rescan has no duplicates",
          "[workflow][abapgit_deploy][export]") {
    Fixture f;
    TempRepo repo;
    // ZROOT with sub-package ZROOT_SUB (PREFIX logic): src/sub/.
    Touch(repo.root / "src" / "zerpl_d_a.doma.xml", "<old/>");
    Touch(repo.root / "src" / "sub" / "zerpl_d_b.doma.xml", "<old/>");
    // A part the system no longer has (like a screen deleted in SE51).
    Touch(repo.root / "src" / "sub" / "zerpl_d_b.doma.screen_0200.abap", "PROCESS.\n");
    f.system["ZERPL_D_A"] = {{"zerpl_d_a.doma.xml", "<a/>"}};
    f.system["ZERPL_D_B"] = {{"zerpl_d_b.doma.xml", "<b/>"}};
    f.engine.package_chains = {{"ZERPL_D_A", "ZROOT"}, {"ZERPL_D_B", "ZROOT_SUB ZROOT"}};
    AbapGitLayout layout;
    auto scan = ScanAbapGitFolder(repo.root, layout, "ZROOT");
    REQUIRE(scan.IsOk());
    auto options = f.Options();
    options.root_package = "ZROOT";

    auto report = f.Workflow().Export(scan.Value().objects, Target(repo.root, "ZROOT"), options);

    REQUIRE(report.objects.size() == 2);
    CHECK_FALSE(report.Failed());
    CHECK(report.objects[1].package == "ZROOT_SUB");
    CHECK_FALSE(std::filesystem::exists(repo.root / "src" / "zerpl_d_b.doma.xml"));
    CHECK_FALSE(std::filesystem::exists(repo.root / "src" / "sub" /
                                        "zerpl_d_b.doma.screen_0200.abap"));
    bool reported = false;
    for (const auto& part : report.objects[1].parts) {
        reported = reported || part == "removed zerpl_d_b.doma.screen_0200.abap";
    }
    CHECK(reported);
    auto rescan = ScanAbapGitFolder(repo.root, layout, "ZROOT");
    REQUIRE(rescan.IsOk());
    CHECK(rescan.Value().objects.size() == 2);
}

TEST_CASE("abapGit export: --object lands in the folder of its TADIR package",
          "[workflow][abapgit_deploy][export]") {
    Fixture f;
    TempRepo repo;
    f.system["ZERPL_D_B"] = {{"zerpl_d_b.doma.xml", "<b/>"}};
    f.engine.package_chains = {{"ZERPL_D_B", "ZROOT_SUB ZROOT"}};
    auto options = f.Options();
    options.root_package = "ZROOT";

    auto report =
        f.Workflow().Export({Wanted("DOMA", "ZERPL_D_B")}, Target(repo.root, "ZROOT"), options);

    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Exported);
    CHECK(report.objects[0].package == "ZROOT_SUB");
    CHECK(std::filesystem::exists(repo.root / "src" / "sub" / "zerpl_d_b.doma.xml"));
}

TEST_CASE("abapGit export: an object outside the repository's package is refused",
          "[workflow][abapgit_deploy][export]") {
    Fixture f;
    TempRepo repo;
    f.system["ZERPL_D_B"] = {{"zerpl_d_b.doma.xml", "<b/>"}};
    f.engine.package_chains = {{"ZERPL_D_B", "ZOTHER"}};
    auto options = f.Options();
    options.root_package = "ZROOT";

    auto report =
        f.Workflow().Export({Wanted("DOMA", "ZERPL_D_B")}, Target(repo.root, "ZROOT"), options);

    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(report.objects[0].message.find("ZOTHER") != std::string::npos);
    CHECK(std::filesystem::is_empty(repo.root));
}

TEST_CASE("abapGit export: dry run reports removals but touches nothing",
          "[workflow][abapgit_deploy][export]") {
    Fixture f;
    TempRepo repo;
    Touch(repo.root / "src" / "zerpl_d_a.doma.xml", "<old/>");
    Touch(repo.root / "src" / "zerpl_d_a.doma.screen_0200.abap", "x");
    f.system["ZERPL_D_A"] = {{"zerpl_d_a.doma.xml", "<a/>"}};
    f.engine.package_chains = {{"ZERPL_D_A", "$TMP"}};
    auto scan = ScanAbapGitFolder(repo.root, AbapGitLayout{}, "$TMP");
    REQUIRE(scan.IsOk());
    auto options = f.Options();
    options.dry_run = true;
    auto report = f.Workflow().Export(scan.Value().objects, Target(repo.root, "$TMP"), options);
    REQUIRE(report.objects.size() == 1);
    CHECK(std::filesystem::exists(repo.root / "src" / "zerpl_d_a.doma.screen_0200.abap"));
    bool reported = false;
    for (const auto& part : report.objects[0].parts) {
        reported = reported || part == "removed zerpl_d_a.doma.screen_0200.abap";
    }
    CHECK(reported);
}

// ---------------------------------------------------------------------------
// status in pull-request checks: annotations and handler validation.
// ---------------------------------------------------------------------------

TEST_CASE("abapGit status: a missing --transport is an annotation, not a refusal",
          "[workflow][abapgit_deploy][status]") {
    Fixture f;
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    obj.package = "ZERPL_DEMO";
    auto options = f.Options();
    options.root_package = "ZERPL_DEMO";
    options.dry_run = true;

    auto report = f.Workflow().Deploy({obj}, options);

    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Created);
    REQUIRE(report.objects[0].notes.size() == 1);
    CHECK(report.objects[0].notes[0].find("--transport") != std::string::npos);
    CHECK_FALSE(report.Failed());

    options.strict = true;
    auto strict = f.Workflow().Deploy({obj}, options);
    CHECK(strict.objects[0].action == ObjectAction::Refused);
}

TEST_CASE("abapGit status: handler validation failures show up without writing",
          "[workflow][abapgit_deploy][status]") {
    Fixture f;
    f.handler->invalid_for = "ZERPL_D_A";
    auto options = f.Options();
    options.dry_run = true;
    auto status = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, options);
    REQUIRE(status.objects.size() == 1);
    CHECK(status.objects[0].action == ObjectAction::Failed);
    CHECK(status.objects[0].message.find("VARIS") != std::string::npos);
    CHECK(status.Failed());

    // deploy refuses the same object the same way, before Apply.
    auto deploy = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options());
    CHECK(deploy.objects[0].action == ObjectAction::Failed);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit report: the most severe category decides the exit code",
          "[workflow][abapgit_deploy][exit]") {
    auto with = [](ErrorCategory c) {
        ObjectOutcome o;
        o.action = ObjectAction::Failed;
        Error e;
        e.category = c;
        o.error = e;
        return o;
    };
    AbapGitRunReport report;
    CHECK(report.ExitCode() == 0);
    report.objects = {with(ErrorCategory::ActivationError), with(ErrorCategory::LockConflict)};
    CHECK(report.ExitCode() == 6);  // lock beats activation, whatever the order
    report.objects.push_back(with(ErrorCategory::TransportError));
    CHECK(report.ExitCode() == 9);
    report.objects.push_back(with(ErrorCategory::Authorization));
    CHECK(report.ExitCode() == 1);
    AbapGitRunReport missing;
    missing.objects = {with(ErrorCategory::NotFound), with(ErrorCategory::CheckError)};
    CHECK(missing.ExitCode() == 8);
}

// ---------------------------------------------------------------------------
// Dependency order: a failed object's dependents are skipped, not attempted.
// ---------------------------------------------------------------------------

TEST_CASE("abapGit deploy: dependents of a failed object are skipped",
          "[workflow][abapgit_deploy][deps]") {
    Fixture f;
    f.handler->fail_apply_for = "ZERPL_D_A";
    // ZERPL_D_B mentions ZERPL_D_A (as a DTEL names its domain); ZERPL_D_C not.
    auto a = f.repo.Doma("zerpl_d_a", "A");
    auto b = f.repo.Doma("zerpl_d_b", "uses ZERPL_D_A");
    auto c = f.repo.Doma("zerpl_d_c", "independent");
    c.key.name = "ZERPL_D_C";

    auto report = f.Workflow().Deploy({a, b, c}, f.Options());

    REQUIRE(report.objects.size() == 3);
    CHECK(report.objects[0].action == ObjectAction::Failed);
    CHECK(report.objects[1].action == ObjectAction::Skipped);
    CHECK(report.objects[1].message == "dependency DOMA ZERPL_D_A failed");
    CHECK(report.objects[2].action == ObjectAction::Created);
    CHECK(f.handler->applied == std::vector<std::string>{"ZERPL_D_C"});
    CHECK(report.ExitCode() == 6);  // the failure's own exit code
}

TEST_CASE("abapGit deploy: progress is reported per object before it is written",
          "[workflow][abapgit_deploy][deps]") {
    Fixture f;
    std::vector<std::string> lines;
    auto options = f.Options();
    options.progress = [&](const std::string& line) { lines.push_back(line); };
    auto report = f.Workflow().Deploy(
        {f.repo.Doma("zerpl_d_a", "A"), f.repo.Doma("zerpl_d_b", "B")}, options);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "[1/2] creating DOMA ZERPL_D_A");
    CHECK(lines[1] == "[2/2] creating DOMA ZERPL_D_B");
}

TEST_CASE("abapGit status: refusals are reported with exit 0, --strict makes them fatal",
          "[workflow][abapgit_deploy][status]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    f.handler->changed_by = "COLLEAGUE";
    f.record.objects.clear();
    f.system["ZERPL_D_A"].begin()->second = "<changed by a colleague/>";
    auto options = f.Options();
    options.dry_run = true;

    auto drift = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, options);

    REQUIRE(drift.objects[0].action == ObjectAction::Refused);
    CHECK(drift.StatusExitCode(false) == 0);
    CHECK(drift.StatusExitCode(true) == 6);

    // A missing --transport: an annotation, but with --strict a refusal (9).
    auto transportable = f.repo.Doma("zerpl_d_b", "B");
    transportable.package = "ZERPL_DEMO";
    options.root_package = "ZERPL_DEMO";
    CHECK(f.Workflow().Deploy({transportable}, options).StatusExitCode(false) == 0);
    options.strict = true;
    CHECK(f.Workflow().Deploy({transportable}, options).StatusExitCode(true) == 9);
}

TEST_CASE("abapGit status: a system that cannot be read is not a green status",
          "[workflow][abapgit_deploy][status]") {
    Fixture f;
    Error down;
    down.message = "Connection refused";
    down.category = ErrorCategory::Connection;
    f.engine.fail_run = down;
    auto options = f.Options();
    options.dry_run = true;

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, options);

    CHECK(report.StatusExitCode(false) == 1);
}

// ---------------------------------------------------------------------------
// Batching: at most three engine runs per deploy, failures stay with their
// object.
// ---------------------------------------------------------------------------


TEST_CASE("abapGit batching: read, DDIC and write+activate runs, whatever the object count",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    REQUIRE(report.objects.size() == 6);
    for (const auto& o : report.objects) {
        CHECK(o.action == ObjectAction::Created);
    }
    REQUIRE(f.engine.runs.size() == 3);
    CHECK(f.engine.runs[0].size() == 7);  // system info + 6 reads
    CHECK(BatchFixture::Calls(f.engine.runs[1]) ==
          std::vector<std::string>{"fake_put( 'DOMA' 'ZD_A' ).", "fake_put( 'DOMA' 'ZD_B' )."});
    // Programs written, then one activation of all of them, then the
    // transaction (it needs its program active).
    CHECK(BatchFixture::Calls(f.engine.runs[2]) ==
          std::vector<std::string>{"fake_put( 'PROG' 'ZP_A' ).", "fake_put( 'PROG' 'ZP_B' ).",
                                   "activate_objects( ).", "fake_put( 'TRAN' 'ZT_A' )."});
    // The ADT work runs after the DDIC run and before the write run.
    CHECK(f.log == std::vector<std::string>{"ENGINE ZD_A", "ENGINE ZD_B", "ADT ZFG",
                                            "ENGINE ZP_A", "ENGINE ZP_B", "ENGINE ZT_A"});
    REQUIRE(f.engine.activations.size() == 1);
    REQUIRE(f.engine.activations[0].size() == 2);
    CHECK(f.engine.activations[0][0].owner == "PROG ZP_A");
    CHECK(f.engine.activations[0][0].object == "REPS");
    CHECK(f.engine.activations[0][1].owner == "PROG ZP_B");
}

TEST_CASE("abapGit batching: nothing to write costs only the read run",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    REQUIRE_FALSE(f.Workflow().Deploy(f.Demo(), f.Options()).Failed());
    f.engine.runs.clear();

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(f.engine.runs.size() == 1);
    CHECK_FALSE(report.Failed());
}

TEST_CASE("abapGit batching: a failed DDIC op fails its object and skips its dependents",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    f.doma->fail_apply_for = "ZD_A";

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(report.objects[0].action == ObjectAction::Failed);
    CHECK(report.objects[0].message.find("locked in request A4HK900012") != std::string::npos);
    CHECK(report.objects[0].error->category == ErrorCategory::LockConflict);
    CHECK(report.objects[1].action == ObjectAction::Created);
    CHECK(report.objects[2].action == ObjectAction::Skipped);  // ZFG uses ZD_A
    CHECK(report.objects[2].message == "dependency DOMA ZD_A failed");
    CHECK(report.objects[3].action == ObjectAction::Created);
    CHECK(f.fugr->applied.empty());
}

TEST_CASE("abapGit batching: a failed write skips the dependents in the same run",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    f.prog->fail_apply_for = "ZP_A";

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(report.objects[3].action == ObjectAction::Failed);
    CHECK(report.objects[4].action == ObjectAction::Created);
    CHECK(report.objects[5].action == ObjectAction::Skipped);
    CHECK(report.objects[5].message == "dependency PROG ZP_A failed");
    CHECK(f.tran->applied.empty());
    // ZP_A's working objects are not activated.
    REQUIRE(f.engine.activations.size() == 1);
    for (const auto& w : f.engine.activations[0]) {
        CHECK(w.owner != "PROG ZP_A");
    }
}

TEST_CASE("abapGit batching: what activation leaves inactive fails its own object only",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    f.engine.fail_activation_for = {"PROG ZP_A"};

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(report.objects[3].action == ObjectAction::Failed);
    CHECK(report.objects[3].error->category == ErrorCategory::ActivationError);
    CHECK(report.objects[3].message.find("GV_MISSING") != std::string::npos);
    CHECK(report.objects[4].action == ObjectAction::Created);
    CHECK(report.objects[4].message.empty());
    CHECK(report.objects[5].action == ObjectAction::Skipped);  // its program is not active
    CHECK(report.ExitCode() == 5);
}

TEST_CASE("abapGit batching: writes that need their object active run after the activation",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    f.prog->after_activation = true;

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK_FALSE(report.Failed());
    REQUIRE(f.engine.runs.size() == 3);
    CHECK(BatchFixture::Calls(f.engine.runs[2]) ==
          std::vector<std::string>{"fake_put( 'PROG' 'ZP_A' ).", "fake_put( 'PROG' 'ZP_B' ).",
                                   "activate_objects( ).", "fake_put( 'TRAN' 'ZT_A' ).",
                                   "fake_after( 'PROG' 'ZP_A' ).",
                                   "fake_after( 'PROG' 'ZP_B' )."});
    // Guarded by its own object: skipped when that object failed.
    const auto& last = f.engine.runs[2].back().call;
    CHECK(last.find("op_owner = 'PROG ZP_B' requires = 'PROG ZP_B'") != std::string::npos);
}

TEST_CASE("abapGit batching: a write after the activation is skipped when the activation failed",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    f.prog->after_activation = true;
    f.engine.fail_activation_for = {"PROG ZP_A"};

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(report.objects[3].action == ObjectAction::Failed);
    CHECK(report.objects[3].error->category == ErrorCategory::ActivationError);
    CHECK(report.objects[4].action == ObjectAction::Created);
    CHECK(std::find(f.log.begin(), f.log.end(), "AFTER ZP_A") == f.log.end());
    CHECK(std::find(f.log.begin(), f.log.end(), "AFTER ZP_B") != f.log.end());
}

TEST_CASE("abapGit batching: a run that fails as a whole fails every object in it",
          "[workflow][abapgit_deploy][batch]") {
    BatchFixture f;
    Error down;
    down.message = "The SAP helper class did not finish (no @END marker)";
    down.category = ErrorCategory::Internal;
    f.engine.fail_run = down;
    f.engine.fail_run_at = 2;  // the write run

    auto report = f.Workflow().Deploy(f.Demo(), f.Options());

    CHECK(report.objects[0].action == ObjectAction::Created);
    CHECK(report.objects[2].action == ObjectAction::Created);
    for (size_t i = 3; i < 6; ++i) {
        CHECK(report.objects[i].action == ObjectAction::Failed);
        CHECK(report.objects[i].message.find("no @END marker") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// GitFileHistory against a real (temporary) git repository.
// ---------------------------------------------------------------------------

TEST_CASE("GitFileHistory: committed versions newest first, exact bytes",
          "[workflow][abapgit_deploy][git]") {
    if (std::system("git --version > /dev/null 2>&1") != 0) {
        SKIP("git is not installed");
    }
    TempRepo repo;
    const auto dir = repo.root.string();
    auto git = [&](const std::string& args) {
        const std::string cmd = "git -C \"" + dir + "\" -c user.name=t -c user.email=t@t " +
                                args + " > /dev/null 2>&1";
        REQUIRE(std::system(cmd.c_str()) == 0);
    };
    git("init -q");
    const auto file = repo.root / "zerpl_d_a.doma.xml";
    const std::vector<std::string> versions = {"<a>\n <b/>\n</a>\n", "<a>\n <c/>\n</a>",
                                               "line with\ttab\r\n<x/>\n"};
    for (const auto& v : versions) {
        std::ofstream(file, std::ios::binary | std::ios::trunc) << v;
        git("add -A");
        git("commit -qm v");
    }
    GitFileHistory history(10);
    auto got = history.PreviousVersions(file);
    REQUIRE(got.size() == 3);
    CHECK(got[0] == versions[2]);
    CHECK(got[1] == versions[1]);  // no final newline survives
    CHECK(got[2] == versions[0]);

    GitFileHistory capped(2);
    CHECK(capped.PreviousVersions(file).size() == 2);
}

TEST_CASE("GitFileHistory: paths with shell metacharacters are not passed to a shell",
          "[workflow][abapgit_deploy][git]") {
    TempRepo repo;
    GitFileHistory history;
    for (const char* name : {"a'b.doma.xml", "a`id`.doma.xml", "a$(x).doma.xml", "a;b.doma.xml",
                             "a|b.doma.xml", "a&b.doma.xml"}) {
        CHECK(history.PreviousVersions(repo.root / name).empty());
    }
}

TEST_CASE("abapGit deploy: the drift hint is a copy-pasteable export command",
          "[workflow][abapgit_deploy][drift]") {
    Fixture f;
    REQUIRE_FALSE(f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Status")}, f.Options()).Failed());
    f.system["ZERPL_D_A"].begin()->second = "<edited in SE11/>";
    f.handler->changed_by = "COLLEAGUE";
    auto options = f.Options();
    options.repo_root = f.repo.root;

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Order status")}, options);

    const auto& message = report.objects[0].message;
    CHECK(message.find("erpl-adt abapgit export '" + f.repo.root.string() +
                       "' --package '$TMP' --object DOMA:ZERPL_D_A") != std::string::npos);
    CHECK(message.find("<repo>") == std::string::npos);
}

namespace {

// `file` with extra top-level sections appended (e.g. a translation that
// someone added in SE63 after the deploy).
std::string WithSections(const std::string& file, const std::string& extra) {
    auto parsed = ParseAbapGitXml(file);
    REQUIRE(parsed.IsOk());
    AbapGitXml xml = parsed.Value();
    xml.values_xml += extra;
    return RenderAbapGitXml(xml);
}

}  // namespace

TEST_CASE("abapGit delete: translations or documentation the files lack are refused",
          "[workflow][abapgit_deploy][guard][drift]") {
    const std::string translation =
        "<I18N_LANGS><LANGU>D</LANGU></I18N_LANGS><DD01_TEXTS><item><DDLANGUAGE>D</DDLANGUAGE>"
        "<DDTEXT>Status DE</DDTEXT></item></DD01_TEXTS>";
    const std::string docu =
        "<LONGTEXTS><item><DOKIL><ID>DO</ID><OBJECT>ZERPL_D_A</OBJECT><LANGU>D</LANGU>"
        "<TYP>E</TYP></DOKIL><HEAD><TDSPRAS>D</TDSPRAS></HEAD><LINES><TLINE><TDLINE>Doku"
        "</TDLINE></TLINE></LINES></item></LONGTEXTS>";
    for (const auto& extra : {translation, docu}) {
        Fixture f;
        auto obj = f.repo.Doma("zerpl_d_a", "Status");
        REQUIRE_FALSE(f.Workflow().Deploy({obj}, f.Options()).Failed());
        // A colleague translated / documented it in SE63 / SE61 - even as the
        // same user and with our own deploy on record, delete would take
        // that work with it.
        auto& file = f.system["ZERPL_D_A"].begin()->second;
        file = WithSections(file, extra);

        auto report = f.Workflow().Delete({obj}, f.Options());
        REQUIRE(report.objects.size() == 1);
        CHECK(report.objects[0].action == ObjectAction::Refused);
        CHECK(report.objects[0].message.find("--force") != std::string::npos);
        CHECK(report.ExitCode() == 6);
        CHECK(f.handler->deleted.empty());

        auto status = f.Options();
        status.dry_run = true;
        CHECK(f.Workflow().Delete({obj}, status).objects[0].action == ObjectAction::Refused);

        auto options = f.Options();
        options.force = true;
        CHECK(f.Workflow().Delete({obj}, options).objects[0].action == ObjectAction::Deleted);
    }
}

TEST_CASE("abapGit deploy: a type whose update drops translations compares them all",
          "[workflow][abapgit_deploy][guard][drift]") {
    // A transaction update is delete + insert (RPY_TRANSACTION_*), which
    // drops the SE63 translations of its short text. They cannot be in the
    // files (no workbench API writes them), so they must not be scoped away:
    // the update is drift until --force.
    Fixture f;
    f.handler->writes_translations = false;
    f.handler->changed_by = "";  // SAP records no changer for a transaction
    auto obj = f.repo.Doma("zerpl_d_a", "Status");
    REQUIRE_FALSE(f.Workflow().Deploy({obj}, f.Options()).Failed());
    auto& file = f.system["ZERPL_D_A"].begin()->second;
    file = WithSections(file, "<I18N_TPOOL><item><SPRSL>D</SPRSL><TTEXT>Anzeigen</TTEXT>"
                              "</item></I18N_TPOOL>");
    f.handler->applied.clear();

    auto report = f.Workflow().Deploy({f.repo.Doma("zerpl_d_a", "Changed")}, f.Options());
    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK(f.handler->applied.empty());
}

TEST_CASE("abapGit export: a destination outside the repository is never written",
          "[workflow][abapgit_deploy][guard]") {
    // A layout that did not come through ParseDotAbapGit (or a package folder
    // that climbs out) must not write or remove files outside the root.
    Fixture f;
    f.system["ZERPL_D_A"] = {{"zerpl_d_a.doma.xml", "<new/>"}};
    f.engine.package_chains["ZERPL_D_A"] = "$TMP";
    auto target = Target(f.repo.root / "repo", "$TMP");
    target.layout.starting_folder = "/../outside/";
    const auto outside = f.repo.root / "outside";
    Touch(outside / "zerpl_d_a.doma.screen_0100.abap", "someone else's file");

    auto report = f.Workflow().Export({Wanted("DOMA", "ZERPL_D_A")}, target, f.Options());

    REQUIRE(report.objects.size() == 1);
    CHECK(report.objects[0].action == ObjectAction::Refused);
    CHECK_FALSE(std::filesystem::exists(outside / "zerpl_d_a.doma.xml"));
    CHECK(std::filesystem::exists(outside / "zerpl_d_a.doma.screen_0100.abap"));
}

TEST_CASE("abapGit status: a file that cannot be loaded is a local error",
          "[workflow][abapgit_deploy]") {
    Fixture f;
    auto good = f.repo.Doma("zerpl_d_a", "Status");
    auto bad = f.repo.Doma("zerpl_d_b", "B");
    std::ofstream(bad.files["xml"], std::ios::binary | std::ios::trunc) << "<abapGit><broken";
    auto options = f.Options();
    options.dry_run = true;

    auto report = f.Workflow().Deploy({good, bad}, options);

    REQUIRE(report.objects.size() == 2);
    REQUIRE(report.local_error.has_value());
    CHECK(report.local_error->message.find("zerpl_d_b.doma.xml") != std::string::npos);
    CHECK(report.StatusExitCode(false) != 0);
    CHECK_FALSE(report.read_error.has_value());
}
