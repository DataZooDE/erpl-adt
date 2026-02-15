#include <erpl_adt/adt/abapgit.hpp>
#include "adt_utils.hpp"

#include <string>

namespace erpl_adt {

namespace {

const char* kReposPath = "/sap/bc/adt/abapgit/repos";
const char* kCloneContentType = "application/vnd.sap.adt.abapgit.repositories.v1+xml";

std::string RepoPath(const RepoKey& key) {
    return std::string(kReposPath) + "/" + key.Value();
}

std::string PullPath(const RepoKey& key) {
    return RepoPath(key) + "/pull";
}

} // namespace

Result<std::vector<RepoInfo>, Error> ListRepos(
    IAdtSession& session,
    const IXmlCodec& codec) {

    auto response = session.Get(kReposPath);
    if (response.IsErr()) {
        return Result<std::vector<RepoInfo>, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<RepoInfo>, Error>::Err(
            Error::FromHttpStatus("ListRepos", kReposPath, http.status_code, http.body));
    }

    return codec.ParseRepoListResponse(http.body);
}

Result<std::optional<RepoInfo>, Error> FindRepo(
    IAdtSession& session,
    const IXmlCodec& codec,
    const RepoUrl& repo_url) {

    auto repos = ListRepos(session, codec);
    if (repos.IsErr()) {
        return Result<std::optional<RepoInfo>, Error>::Err(std::move(repos).Error());
    }

    for (const auto& repo : repos.Value()) {
        if (repo.url == repo_url.Value()) {
            return Result<std::optional<RepoInfo>, Error>::Ok(repo);
        }
    }

    return Result<std::optional<RepoInfo>, Error>::Ok(std::nullopt);
}

Result<RepoInfo, Error> CloneRepo(
    IAdtSession& session,
    const IXmlCodec& codec,
    const RepoUrl& url,
    const BranchRef& branch,
    const PackageName& package,
    std::chrono::seconds timeout) {

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<RepoInfo, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildRepoCloneXml(url, branch, package);
    if (xml.IsErr()) {
        return Result<RepoInfo, Error>::Err(std::move(xml).Error());
    }

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kReposPath, xml.Value(), kCloneContentType, headers);
    if (response.IsErr()) {
        return Result<RepoInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 with Location header — poll until complete.
    if (http.status_code == 202) {
        auto location = adt_utils::RequireHeaderCi(http.headers, "Location",
                                                   "CloneRepo",
                                                   kReposPath, 202);
        if (location.IsErr()) {
            return Result<RepoInfo, Error>::Err(std::move(location).Error());
        }

        auto poll = session.PollUntilComplete(location.Value(), timeout);
        if (poll.IsErr()) {
            return Result<RepoInfo, Error>::Err(std::move(poll).Error());
        }
        if (poll.Value().status == PollStatus::Failed) {
            return Result<RepoInfo, Error>::Err(Error{
                "CloneRepo", kReposPath, std::nullopt,
                "async clone operation failed", std::nullopt,
                ErrorCategory::CloneError});
        }
        if (poll.Value().status == PollStatus::Running) {
            return Result<RepoInfo, Error>::Err(Error{
                "CloneRepo", kReposPath, std::nullopt,
                "async clone operation did not complete within timeout",
                std::nullopt, ErrorCategory::Timeout});
        }

        return codec.ParseRepoListResponse(poll.Value().body)
            .AndThen([&url](const std::vector<RepoInfo>& repos) -> Result<RepoInfo, Error> {
                for (const auto& r : repos) {
                    if (r.url == url.Value()) {
                        return Result<RepoInfo, Error>::Ok(r);
                    }
                }
                return Result<RepoInfo, Error>::Err(Error{
                    "CloneRepo", "", std::nullopt,
                    "cloned repo not found in response", std::nullopt});
            });
    }

    // Synchronous: 200/201 with repo info in body.
    if (http.status_code == 200 || http.status_code == 201) {
        return codec.ParseRepoListResponse(http.body)
            .AndThen([&url](const std::vector<RepoInfo>& repos) -> Result<RepoInfo, Error> {
                for (const auto& r : repos) {
                    if (r.url == url.Value()) {
                        return Result<RepoInfo, Error>::Ok(r);
                    }
                }
                // If only one repo in the response, return it regardless.
                if (repos.size() == 1) {
                    return Result<RepoInfo, Error>::Ok(repos[0]);
                }
                return Result<RepoInfo, Error>::Err(Error{
                    "CloneRepo", "", std::nullopt,
                    "cloned repo not found in response", std::nullopt});
            });
    }

    return Result<RepoInfo, Error>::Err(
        Error::FromHttpStatus("CloneRepo", kReposPath, http.status_code, http.body));
}

Result<PollResult, Error> PullRepo(
    IAdtSession& session,
    const IXmlCodec& /*codec*/,
    const RepoKey& repo_key,
    std::chrono::seconds timeout) {

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<PollResult, Error>::Err(std::move(csrf).Error());
    }

    auto path = PullPath(repo_key);
    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(path, "", "application/xml", headers);
    if (response.IsErr()) {
        return Result<PollResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Pull is always async: 202 + Location.
    if (http.status_code == 202) {
        auto location = adt_utils::RequireHeaderCi(http.headers, "Location",
                                                   "PullRepo", path, 202);
        if (location.IsErr()) {
            return Result<PollResult, Error>::Err(std::move(location).Error());
        }
        auto poll = session.PollUntilComplete(location.Value(), timeout);
        if (poll.IsErr()) {
            return Result<PollResult, Error>::Err(std::move(poll).Error());
        }
        if (poll.Value().status == PollStatus::Running) {
            return Result<PollResult, Error>::Err(Error{
                "PullRepo", path, std::nullopt,
                "async pull operation did not complete within timeout",
                std::nullopt, ErrorCategory::Timeout});
        }
        return poll;
    }

    // Synchronous success (e.g. no changes needed).
    if (http.status_code == 200) {
        return Result<PollResult, Error>::Ok(
            PollResult{PollStatus::Completed, http.body, std::chrono::milliseconds{0}});
    }

    return Result<PollResult, Error>::Err(
        Error::FromHttpStatus("PullRepo", path, http.status_code, http.body));
}

Result<void, Error> UnlinkRepo(
    IAdtSession& session,
    const RepoKey& repo_key) {

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<void, Error>::Err(std::move(csrf).Error());
    }

    auto path = RepoPath(repo_key);
    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Delete(path, headers);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("UnlinkRepo", path, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
