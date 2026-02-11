#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <chrono>
#include <optional>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// AbapGit — free functions for ADT abapGit repository operations.
//
// GET    /sap/bc/adt/abapgit/repos            — list linked repos
// POST   /sap/bc/adt/abapgit/repos            — clone a repo (async)
// GET    /sap/bc/adt/abapgit/repos/{key}       — repo status
// POST   /sap/bc/adt/abapgit/repos/{key}/pull  — trigger pull (async)
// DELETE /sap/bc/adt/abapgit/repos/{key}       — unlink
// ---------------------------------------------------------------------------

[[nodiscard]] Result<std::vector<RepoInfo>, Error> ListRepos(
    IAdtSession& session,
    const IXmlCodec& codec);

[[nodiscard]] Result<std::optional<RepoInfo>, Error> FindRepo(
    IAdtSession& session,
    const IXmlCodec& codec,
    const RepoUrl& repo_url);

[[nodiscard]] Result<RepoInfo, Error> CloneRepo(
    IAdtSession& session,
    const IXmlCodec& codec,
    const RepoUrl& url,
    const BranchRef& branch,
    const PackageName& package,
    std::chrono::seconds timeout = std::chrono::seconds{600});

[[nodiscard]] Result<PollResult, Error> PullRepo(
    IAdtSession& session,
    const IXmlCodec& codec,
    const RepoKey& repo_key,
    std::chrono::seconds timeout = std::chrono::seconds{600});

[[nodiscard]] Result<void, Error> UnlinkRepo(
    IAdtSession& session,
    const RepoKey& repo_key);

} // namespace erpl_adt
