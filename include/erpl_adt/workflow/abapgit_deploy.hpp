#pragma once

#include <erpl_adt/adt/abapgit_repo.hpp>
#include <erpl_adt/adt/classic_engine.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/workflow/classrun_helper.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// abapGit deploy / export / delete for classic objects.
//
// The workflow is type-agnostic: everything type-specific sits behind
// IClassicTypeHandler (one implementation per abapGit object type). It
//   1. reads the current state of every object in one engine run,
//   2. compares it with the files (both sides in abapGit's exact format),
//   3. creates / updates only what differs, in dependency order,
//   4. reports one outcome per object.
//
// Step 3 is batched by phase, so a deploy costs at most three helper-class
// runs (each ~3 s: write + activate the helper class) whatever the number of
// objects. Handlers only plan (DeployPlan); the workflow executes the phases
// of all objects together:
//   read run       every object's state                       (engine run 1)
//   DDIC run       domains, data elements: written + activated  (engine run 2)
//   ADT            function groups, program sources: per object over ADT
//   write run      screens, GUI status, text pools, transactions, new
//                  programs - saved inactive - then ONE activation of every
//                  saved working object (RS_WORKING_OBJECTS_ACTIVATE)  (run 3)
// DDIC objects are active before anything that uses them is written or
// generated. Failures stay with their object: every operation carries its
// owner, an operation of an object whose dependency failed earlier in the run
// is skipped, and what activation leaves inactive fails the object it belongs to.
// ---------------------------------------------------------------------------

// File name (e.g. "zerpl_d_so_status.doma.xml") -> file bytes.
using ObjectFileSet = std::map<std::string, std::string>;

struct ObjectState {
    bool exists = false;
    // Last changer of the object as a whole; empty = SAP records none (e.g.
    // transactions). Drift protection treats an unknown changer as unsafe.
    std::string changed_by;
    std::string changed_on;  // YYYYMMDD
    // Finer-grained last changers, where SAP keeps them: by file name
    // ("zfg.fugr.lzfgu01.abap") or by XML section of a file
    // ("zfoo.prog.xml#CUA"). An empty value means "unknown". Lookup order
    // for a differing section: file#SECTION, file, changed_by.
    std::map<std::string, std::string> part_changed_by;
    ObjectFileSet files;     // the object exactly as abapGit would export it
    // Parts with an inactive version in the system (e.g. left by a deploy
    // whose activation failed). Not part of the files — export shows the
    // active version — but a deploy must rewrite and activate them even when
    // the files equal the active version.
    std::vector<std::string> pending;
};

// Everything a handler may use while applying changes.
struct TypeContext {
    IAdtSession& session;
    IEngineRunner& engine;
    std::string package;
    std::string transport;  // empty for local packages
    std::string language;   // master language, e.g. "E"
};

// One engine operation of a plan, and what its failure is reported as
// ("Writing domain ZA: <SAP's message>").
struct PlannedOp {
    EngineOp op;
    std::string what;
};

// What a handler needs done to bring one object to its files, by phase (see
// the header comment). Empty phases are skipped.
struct DeployPlan {
    std::vector<std::string> parts;   // for the report, e.g. "screen 0100"
    std::vector<PlannedOp> ddic;      // DDIC writes that activate themselves
    // ADT requests (lock / write / unlock / activate), run in object order
    // between the DDIC run and the write run. Returns the parts it wrote
    // (appended to `parts`).
    std::function<Result<std::vector<std::string>, Error>(TypeContext&)> adt;
    std::vector<PlannedOp> writes;    // engine writes, saved inactive
    // Working objects to activate after the writes (owner is set by the
    // workflow), e.g. {"", "REPS", "ZFOO"}.
    std::vector<WorkingObject> activate;
    // Engine writes that need the object active (e.g. report variants, which
    // SAP checks against the generated selection screen): at the end of the
    // write run, after the activation; skipped when the object failed.
    std::vector<PlannedOp> after_activation;
};

class IClassicTypeHandler {
public:
    virtual ~IClassicTypeHandler() = default;
    [[nodiscard]] virtual std::string Type() const = 0;

    // Engine operations that read the object's current state, and the
    // assembly of their results (plus any ADT reads) into an ObjectState.
    [[nodiscard]] virtual std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                                        const std::string& language) const = 0;
    [[nodiscard]] virtual Result<ObjectState, Error> BuildState(
        const AbapGitObjectKey& key, const std::vector<EngineOpResult>& results,
        TypeContext& ctx) const = 0;

    // What it takes to bring the system to `desired`. Writes nothing: the
    // workflow runs the plans of all objects phase by phase.
    [[nodiscard]] virtual Result<DeployPlan, Error> Plan(const AbapGitObjectKey& key,
                                                        const ObjectFileSet& desired,
                                                        const ObjectState& current,
                                                        const TypeContext& ctx) const = 0;

    [[nodiscard]] virtual Result<void, Error> Delete(const AbapGitObjectKey& key,
                                                     TypeContext& ctx) const = 0;

    // Non-mutating checks of the files against what this handler can write
    // (missing files, parts it cannot deploy, changes it does not support
    // relative to `current`). Run by both status and deploy before Apply, so
    // a status shows the same refusals a deploy would hit.
    [[nodiscard]] virtual Result<void, Error> Validate(const AbapGitObjectKey& key,
                                                       const ObjectFileSet& desired,
                                                       const ObjectState& current,
                                                       const std::string& language) const = 0;
};

// The handlers for all supported types, keyed by abapGit type.
std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> DefaultTypeHandlers();

// Earlier committed versions of a file (newest first), used by drift
// protection to tell "the system holds an older version from git" (a normal
// update) from "someone changed the object in the system" (refused).
class IFileHistory {
public:
    virtual ~IFileHistory() = default;
    [[nodiscard]] virtual std::vector<std::string> PreviousVersions(
        const std::filesystem::path& file) const = 0;
};

// For folders that are not git work trees.
class NoFileHistory final : public IFileHistory {
public:
    std::vector<std::string> PreviousVersions(const std::filesystem::path&) const override {
        return {};
    }
};

// Reads the file's last `max_versions` committed versions with ONE
// `git log -p` process (full-file context, so each patch carries the whole
// version). Paths with characters outside a strict allowlist are never
// passed to the shell (no history then). Returns nothing when git is not
// installed or the file is not tracked.
class GitFileHistory final : public IFileHistory {
public:
    explicit GitFileHistory(int max_versions = 20) : max_versions_(max_versions) {}
    std::vector<std::string> PreviousVersions(const std::filesystem::path& file) const override;

private:
    int max_versions_;
};

// What erpl-adt itself last deployed: the exact bytes of every file of an
// object, per SAP system. SAP keeps no last changer for screens (D020S) or
// transactions (TSTC*), so without this a second deploy of an uncommitted
// change would look like someone else's work. A system version equal to the
// recorded last deploy is the deployer's own; anything else (a colleague's
// change) is still drift.
class IDeployRecord {
public:
    virtual ~IDeployRecord() = default;
    // The bytes of `file` erpl-adt last deployed for `key`; nullopt if none.
    [[nodiscard]] virtual std::optional<std::string> LastDeployed(
        const AbapGitObjectKey& key, const std::string& file) const = 0;
    // Replaces the record of `key` with `files` (after a successful deploy).
    virtual void Remember(const AbapGitObjectKey& key, const ObjectFileSet& files) = 0;
    virtual void Forget(const AbapGitObjectKey& key) = 0;  // after a delete
};

class NoDeployRecord final : public IDeployRecord {
public:
    std::optional<std::string> LastDeployed(const AbapGitObjectKey&,
                                            const std::string&) const override {
        return std::nullopt;
    }
    void Remember(const AbapGitObjectKey&, const ObjectFileSet&) override {}
    void Forget(const AbapGitObjectKey&) override {}
};

// The record as plain files: <directory>/<system>/<TYPE>/<NAME>/<file>.
// Never committed: DefaultDirectory puts it inside the repository's git
// directory. Write failures are logged, never fatal (the record only ever
// relaxes drift protection).
class FileDeployRecord final : public IDeployRecord {
public:
    // `system` identifies the SAP system, e.g. "localhost:50000/001".
    FileDeployRecord(std::filesystem::path directory, const std::string& system);
    std::optional<std::string> LastDeployed(const AbapGitObjectKey& key,
                                            const std::string& file) const override;
    void Remember(const AbapGitObjectKey& key, const ObjectFileSet& files) override;
    void Forget(const AbapGitObjectKey& key) override;

    // <git dir>/erpl-adt/deployed when `folder` is inside a git work tree
    // (a ".git" directory, or a ".git" file naming the git directory of a
    // worktree / submodule); otherwise $XDG_CACHE_HOME/erpl-adt/deployed
    // (~/.cache/..., %LOCALAPPDATA%\erpl-adt\deployed on Windows).
    static std::filesystem::path DefaultDirectory(const std::filesystem::path& folder);

private:
    std::filesystem::path ObjectDir(const AbapGitObjectKey& key) const;
    std::filesystem::path root_;
};

enum class ObjectAction {
    Created, Updated, Unchanged, Deleted, Missing, Exported, Failed, Refused,
    Skipped,  // not attempted: an object it depends on failed
};
std::string ToString(ObjectAction action);

struct ObjectOutcome {
    AbapGitObjectKey key;
    std::string package;
    ObjectAction action = ObjectAction::Unchanged;
    std::vector<std::string> parts;  // what was (or would be) written
    std::string message;             // failure / refusal reason
    std::vector<std::string> notes;  // non-fatal annotations (e.g. status without --transport)
    std::optional<Error> error;
};

struct AbapGitRunOptions {
    std::string root_package;
    std::string transport;
    std::string language = "E";
    bool dry_run = false;
    bool force = false;                            // overwrite drifted objects
    // dry_run: a missing --transport for a transportable package is only an
    // annotation (a PR check has no request); strict makes it a refusal.
    bool strict = false;
    // Called before each object is written ("[3/8] updating PROG ZFOO"), for
    // progress output on a terminal. Optional.
    std::function<void(const std::string&)> progress;
    std::string user;                              // deploying user; default: SAP logon user
    // Repository root, for the export command printed in drift refusals
    // (default: the object's folder).
    std::filesystem::path repo_root;
    // Namespaced objects and packages (/ACME/...) are allowed only when the
    // namespace is changeable in the system (SE03, TRNSPACE-EDITFLAG) — the
    // same rule SE80 applies. There is deliberately no override.
};

struct AbapGitRunReport {
    std::vector<ObjectOutcome> objects;
    std::vector<std::string> skipped;  // unsupported / ignored files
    [[nodiscard]] bool Failed() const;
    [[nodiscard]] std::optional<Error> FirstError() const;
    // The error of the most severe failed/refused object, by category:
    //   connection / authentication / authorization (1) > internal (99) >
    //   timeout (10) > transport (9) > lock conflict / drift (6) >
    //   activation (5) > check (8) > test (7) > pull (4) > clone (3) >
    //   package / not found (2).
    // Environment and guard-rail problems first (nothing else can be
    // trusted), then problems that need a human (CTS, someone else's lock or
    // change), then problems in the code being deployed.
    [[nodiscard]] std::optional<Error> MostSevereError() const;
    // MostSevereError()'s exit code; 0 when nothing failed.
    [[nodiscard]] int ExitCode() const;
    // The first error reading the system's state: the run could not
    // compare everything (connection, helper class, ...).
    std::optional<Error> read_error;
    // Exit code of `status` (a dry run): 0 when it could run - drift, a
    // missing --transport or a namespace refusal are reported, not fatal -
    // unless `strict`, where any refusal or failure exits with the most
    // severe code. A failed system read always exits with its code.
    [[nodiscard]] int StatusExitCode(bool strict) const;
};

// Where an export writes: the repository root and its layout (.abapgit.xml).
// The root package is AbapGitRunOptions::root_package.
struct AbapGitExportTarget {
    std::filesystem::path repo_root;
    AbapGitLayout layout;
};

class AbapGitWorkflow {
public:
    AbapGitWorkflow(IAdtSession& session, IEngineRunner& engine,
                    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers,
                    const IFileHistory& history, IDeployRecord& record);
    // Without a deploy record (NoDeployRecord).
    AbapGitWorkflow(IAdtSession& session, IEngineRunner& engine,
                    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers,
                    const IFileHistory& history);

    // `objects` must already be in dependency order (ScanAbapGitFolder does that).
    [[nodiscard]] AbapGitRunReport Deploy(const std::vector<AbapGitObjectFiles>& objects,
                                          const AbapGitRunOptions& options);
    [[nodiscard]] AbapGitRunReport Delete(const std::vector<AbapGitObjectFiles>& objects,
                                          const AbapGitRunOptions& options);
    // Reads the objects from the system and writes their files. An object
    // with a folder (from ScanAbapGitFolder: a refresh) is written there; one
    // without (named by --object) goes to the folder of its TADIR package,
    // derived from the layout - refused when that package is not the root
    // package or below it. After an object's files are written, its other
    // files in that folder (same stem; parts the system no longer has, e.g. a
    // screen deleted in SE51) are removed and reported as "removed <file>".
    // dry_run writes and removes nothing but reports the same.
    [[nodiscard]] AbapGitRunReport Export(const std::vector<AbapGitObjectFiles>& objects,
                                          const AbapGitExportTarget& target,
                                          const AbapGitRunOptions& options);

private:
    struct ReadResult {
        std::vector<Result<ObjectState, Error>> states;
        std::string user;                     // SAP logon user
        std::vector<std::string> namespaces;  // changeable in the system
        // Per key (with_packages only): TADIR package, then its parents.
        std::vector<std::vector<std::string>> package_chains;
    };
    ReadResult ReadStates(const std::vector<AbapGitObjectKey>& keys,
                          const AbapGitRunOptions& options, bool with_packages = false);
    const IClassicTypeHandler* Handler(const std::string& type) const;
    // The one guard-rail phase every command runs once the system facts are
    // known: object and package must be customer names (Y*/Z* or a
    // namespace changeable in the system).
    static std::optional<Error> CheckAgainstSystem(const AbapGitObjectKey& key,
                                                   const std::string& package,
                                                   const std::vector<std::string>& namespaces);
    std::optional<Error> DetectDrift(const AbapGitObjectFiles& object,
                                     const ObjectFileSet& desired, const ObjectState& current,
                                     const std::string& user,
                                     const std::filesystem::path& repo_root) const;

    IAdtSession& session_;
    IEngineRunner& engine_;
    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers_;
    const IFileHistory& history_;
    NoDeployRecord no_record_;
    IDeployRecord& record_;
};

// Reads an object's files from disk and normalizes them to the exact bytes
// abapGit would write (XML re-rendered, ABAP with LF line ends), so that
// formatting differences never count as changes.
[[nodiscard]] Result<ObjectFileSet, Error> LoadObjectFiles(const AbapGitObjectFiles& object);

}  // namespace erpl_adt
