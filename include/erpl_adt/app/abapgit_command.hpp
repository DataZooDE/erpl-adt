#pragma once

#include <erpl_adt/adt/abapgit_repo.hpp>
#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/workflow/abapgit_deploy.hpp>
#include <erpl_adt/workflow/classrun_helper.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// abapgit deploy / status / export / delete, shared by the CLI
// (`erpl-adt abapgit ...`) and the MCP tools (abapgit_*), so both resolve
// files, validate input and report results identically. The adapters only
// translate their input into an AbapGitRequest, supply the two SAP sessions
// and print / return the report.
// ---------------------------------------------------------------------------

enum class AbapGitCommand { Deploy, Status, Export, Delete };

// "deploy", "status", "export", "delete" — the `command` of the JSON report.
[[nodiscard]] std::string ToString(AbapGitCommand command);

struct AbapGitRequest {
    // Deploy/status/delete: repository folder (with .abapgit.xml) or one
    // object file. Export: the repository folder to write.
    std::filesystem::path path;
    std::string package;    // root package
    std::string transport;  // optional
    bool dry_run = false;
    bool force = false;
    bool strict = false;
    std::vector<std::string> objects;  // export: "TYPE:NAME"; empty = refresh <path>
    std::string master_language;       // export into a new repository (default E)
    // The search for the governing .abapgit.xml stops here (inclusive) instead
    // of at the file system root. The MCP server sets its abapgit root.
    std::optional<std::filesystem::path> boundary;
};

// Everything a run needs, resolved and validated before anything touches SAP.
struct PreparedAbapGitRun {
    AbapGitCommand command = AbapGitCommand::Status;
    AbapGitRunOptions options;
    std::vector<AbapGitObjectFiles> objects;
    std::vector<std::string> skipped;  // unsupported files
    // The folder whose files the run reads or writes: the repository root,
    // the folder of a single object file, or the export folder.
    std::filesystem::path scope;
    // Export only.
    AbapGitExportTarget export_target;
    AbapGitRepoConfig export_config;
    bool write_dot_abapgit = false;  // export starts a new repository
};

// Validates the request (package, transport, --object specs) and resolves the
// files. Errors carry the exit code the command ends with (99 for usage).
[[nodiscard]] Result<PreparedAbapGitRun, Error> PrepareAbapGitRun(AbapGitCommand command,
                                                                 const AbapGitRequest& request);

// Runs the prepared command: removes stale helper classes of the logon user
// (older than two hours), then runs the workflow with one temporary helper
// class. `classrun_session` is a second, independent session (see
// workflow/classrun_helper.hpp). `system` identifies the SAP system
// ("host:port/client") for the record of what this clone last deployed
// there (see IDeployRecord). Export writes .abapgit.xml for a new
// repository.
[[nodiscard]] AbapGitRunReport RunAbapGit(const PreparedAbapGitRun& run, IAdtSession& session,
                                          IAdtSession& classrun_session,
                                          const ClassrunHelperOptions& helper,
                                          const std::string& system);

// The exit code of the command: status (= deploy --dry-run) reports
// refusals without failing unless --strict (AbapGitRunReport::StatusExitCode);
// every other command exits with the most severe object's code.
[[nodiscard]] int AbapGitExitCode(const PreparedAbapGitRun& run, const AbapGitRunReport& report);

// The report as the CLI prints it with --json and the MCP tools return it:
// command, package, dry_run, objects[] (type, name, package, action, parts,
// message, notes, category, exit_code), skipped, summary, exit_code.
[[nodiscard]] nlohmann::json AbapGitReportJson(const PreparedAbapGitRun& run,
                                               const AbapGitRunReport& report);

// The human-readable report (one line per object, then a summary line).
[[nodiscard]] std::string AbapGitReportText(const PreparedAbapGitRun& run,
                                            const AbapGitRunReport& report);

}  // namespace erpl_adt
