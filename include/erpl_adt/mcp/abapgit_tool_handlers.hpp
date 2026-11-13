#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/mcp/tool_registry.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// abapgit_status / abapgit_export / abapgit_deploy / abapgit_delete — the
// `erpl-adt abapgit` commands as MCP tools, with the same arguments and the
// same JSON report (app/abapgit_command.hpp).
//
// File access is confined to one root folder (`erpl-adt mcp --abapgit-root`,
// YAML `mcp.abapgit_root`, default: the server's working directory): a tool
// can only read or write abapGit files below it. Relative paths resolve
// against the root; absolute paths, `..` and symlinks must stay inside it.
// ---------------------------------------------------------------------------

struct AbapGitToolOptions {
    std::filesystem::path root;
    // A second, independent session for the temporary helper class's
    // classrun (see workflow/classrun_helper.hpp). Called once per tool call.
    std::function<std::unique_ptr<IAdtSession>()> make_classrun_session;
    // "host:port/client" of the SAP system, for the deploy record.
    std::string system;
};

void RegisterAbapGitTools(ToolRegistry& registry, IAdtSession& session,
                          AbapGitToolOptions options);

// `requested` (relative to `root`, or absolute) as a path inside `root`, with
// `..` and existing symlinks resolved. Err (authorization) when it leads
// outside the root, or when the root does not exist.
[[nodiscard]] Result<std::filesystem::path, Error> ResolvePathUnderRoot(
    const std::filesystem::path& root, const std::string& requested);

// Err when any symlink below `dir` (recursively, without following links)
// points outside `root` — a file that a scan would read, or an export would
// overwrite, outside the root. A missing `dir` is fine (export creates it).
[[nodiscard]] Result<void, Error> CheckNoSymlinkEscape(const std::filesystem::path& root,
                                                       const std::filesystem::path& dir);

}  // namespace erpl_adt
