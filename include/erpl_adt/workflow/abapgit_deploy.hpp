#pragma once

#include <erpl_adt/adt/abapgit_repo.hpp>
#include <erpl_adt/adt/classic_engine.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/workflow/classrun_helper.hpp>

#include <filesystem>
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
// ---------------------------------------------------------------------------

// File name (e.g. "zerpl_d_so_status.doma.xml") -> file bytes.
using ObjectFileSet = std::map<std::string, std::string>;

struct ObjectState {
    bool exists = false;
    std::string changed_by;  // last changed by (for drift protection)
    std::string changed_on;  // YYYYMMDD
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

    // Bring the system to `desired`. Returns the parts that were written
    // (e.g. "DD01V", "screen 0100", "CUA") for the report.
    [[nodiscard]] virtual Result<std::vector<std::string>, Error> Apply(
        const AbapGitObjectKey& key, const ObjectFileSet& desired, const ObjectState& current,
        TypeContext& ctx) const = 0;

    [[nodiscard]] virtual Result<void, Error> Delete(const AbapGitObjectKey& key,
                                                     TypeContext& ctx) const = 0;
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

// Reads `git log` / `git show` for the file (up to `max_versions` commits).
// Returns nothing when git is not installed or the file is not tracked.
class GitFileHistory final : public IFileHistory {
public:
    explicit GitFileHistory(int max_versions = 50) : max_versions_(max_versions) {}
    std::vector<std::string> PreviousVersions(const std::filesystem::path& file) const override;

private:
    int max_versions_;
};

enum class ObjectAction { Created, Updated, Unchanged, Deleted, Missing, Exported, Failed, Refused };
std::string ToString(ObjectAction action);

struct ObjectOutcome {
    AbapGitObjectKey key;
    std::string package;
    ObjectAction action = ObjectAction::Unchanged;
    std::vector<std::string> parts;  // what was (or would be) written
    std::string message;             // failure / refusal reason
    std::optional<Error> error;
};

struct AbapGitRunOptions {
    std::string root_package;
    std::string transport;
    std::string language = "E";
    bool dry_run = false;
    bool force = false;                            // overwrite drifted objects
    std::string user;                              // deploying user; default: SAP logon user
    std::vector<std::string> customer_namespaces;  // e.g. "/ACME/"; merged with the
                                                   // namespaces changeable in the system
};

struct AbapGitRunReport {
    std::vector<ObjectOutcome> objects;
    std::vector<std::string> skipped;  // unsupported / ignored files
    [[nodiscard]] bool Failed() const;
    [[nodiscard]] std::optional<Error> FirstError() const;
};

class AbapGitWorkflow {
public:
    AbapGitWorkflow(IAdtSession& session, IEngineRunner& engine,
                    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers,
                    const IFileHistory& history);

    // `objects` must already be in dependency order (ScanAbapGitFolder does that).
    [[nodiscard]] AbapGitRunReport Deploy(const std::vector<AbapGitObjectFiles>& objects,
                                          const AbapGitRunOptions& options);
    [[nodiscard]] AbapGitRunReport Delete(const std::vector<AbapGitObjectFiles>& objects,
                                          const AbapGitRunOptions& options);
    // Reads `keys` from the system; writes files below `target_folder`
    // (unless dry_run, which only reports Exported/Missing).
    [[nodiscard]] AbapGitRunReport Export(const std::vector<AbapGitObjectKey>& keys,
                                          const std::filesystem::path& target_folder,
                                          const AbapGitRunOptions& options);

private:
    struct ReadResult {
        std::vector<Result<ObjectState, Error>> states;
        std::string user;                     // SAP logon user
        std::vector<std::string> namespaces;  // changeable in the system
    };
    ReadResult ReadStates(const std::vector<AbapGitObjectKey>& keys,
                          const AbapGitRunOptions& options);
    const IClassicTypeHandler* Handler(const std::string& type) const;
    std::optional<Error> DetectDrift(const AbapGitObjectFiles& object,
                                     const ObjectFileSet& desired, const ObjectState& current,
                                     const std::string& user) const;

    IAdtSession& session_;
    IEngineRunner& engine_;
    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers_;
    const IFileHistory& history_;
};

// Reads an object's files from disk and normalizes them to the exact bytes
// abapGit would write (XML re-rendered, ABAP with LF line ends), so that
// formatting differences never count as changes.
[[nodiscard]] Result<ObjectFileSet, Error> LoadObjectFiles(const AbapGitObjectFiles& object);

}  // namespace erpl_adt
