#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/deploy_workflow.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Helper to build a minimal AppConfig with one repo.
AppConfig MakeSingleRepoConfig() {
    AppConfig config;
    config.connection.host = "sap.example.com";
    config.connection.port = 50000;
    config.connection.user = "user";
    config.connection.password = "pass";
    config.timeout_seconds = 600;

    config.repos.push_back(RepoConfig{
        "test-repo",
        RepoUrl::Create("https://github.com/org/repo.git").Value(),
        BranchRef::Create("refs/heads/main").Value(),
        PackageName::Create("ZTEST").Value(),
        true,
        {},
    });

    return config;
}

// Helper to configure the mock for a successful discovery.
// Discovery: GET /sap/bc/adt/discovery -> 200 + parse result with abapgit=true.
void SetupDiscoverySuccess(MockAdtSession& session, MockXmlCodec& codec) {
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<discovery-xml/>"}));
    DiscoveryResult dr;
    dr.has_abapgit_support = true;
    dr.has_packages_support = true;
    dr.has_activation_support = true;
    codec.SetParseDiscoveryResponse(
        Result<DiscoveryResult, Error>::Ok(dr));
}

void SetupPackageExistsSuccess(MockAdtSession& session, MockXmlCodec& codec) {
    // PackageExists: GET -> 200
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<existing-pkg/>"}));
    // EnsurePackage fetches info: GET -> 200
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<pkg-info/>"}));
    codec.SetParsePackageResponse(
        Result<PackageInfo, Error>::Ok(
            PackageInfo{"ZTEST", "existing", "LOCAL", "/sap/bc/adt/packages/ZTEST", ""}));
}

// Clone already linked: FindRepo returns the repo.
void SetupCloneAlreadyLinked(MockAdtSession& session, MockXmlCodec& codec) {
    // FindRepo: ListRepos GET -> 200 + repo present
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<repos/>"}));

    std::vector<RepoInfo> repos = {
        {"KEY1", "https://github.com/org/repo.git", "refs/heads/main", "ZTEST",
         RepoStatusEnum::Active, "Linked"},
    };
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(repos));
}

// Pull: CSRF + POST->202 + Location + poll->Completed
void SetupPullSuccess(MockAdtSession& session, MockXmlCodec& /*codec*/) {
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-3")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/pull/1"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<pull-done/>", std::chrono::milliseconds{100}}));
}

// Activate: GET inactive->200 + objects, then CSRF + build + POST->200 + parse result
void SetupActivateSuccess(MockAdtSession& session, MockXmlCodec& codec) {
    // GetInactiveObjects: GET -> 200
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<inactive/>"}));
    std::vector<InactiveObject> objects = {
        {"CLAS", "ZCL_TEST", "/sap/bc/adt/oo/classes/ZCL_TEST"},
    };
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(objects));

    // ActivateAll: CSRF + build + POST->200 + parse
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-4")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<act-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<act-result/>"}));
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(ActivationResult{1, 1, 0, {}}));
}

// Activate with no inactive objects (skip).
void SetupActivateSkipped(MockAdtSession& session, MockXmlCodec& codec) {
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<empty/>"}));
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(std::vector<InactiveObject>{}));
}

} // namespace

// ===========================================================================
// Discovery-only subcommand
// ===========================================================================

TEST_CASE("Workflow Execute Discover: succeeds with abapgit support", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Discover);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    CHECK(result.Value().discovery.outcome == StepOutcome::Completed);
    CHECK(result.Value().summary == "Discovery succeeded");
}

TEST_CASE("Workflow Execute Discover: fails when discovery fails", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/discovery", std::nullopt,
              "connection refused", std::nullopt}));

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Discover);

    REQUIRE(result.IsOk());  // Execute returns Ok with failed discovery inside
    CHECK_FALSE(result.Value().success);
    CHECK(result.Value().discovery.outcome == StepOutcome::Failed);
}

TEST_CASE("Workflow Execute Discover: fails when no abapgit support", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<discovery-xml/>"}));
    DiscoveryResult dr;
    dr.has_abapgit_support = false;
    codec.SetParseDiscoveryResponse(Result<DiscoveryResult, Error>::Ok(dr));

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Discover);

    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().success);
    CHECK(result.Value().discovery.outcome == StepOutcome::Failed);
    CHECK(result.Value().discovery.message.find("abapGit") != std::string::npos);
}

// ===========================================================================
// Deploy subcommand — step ordering
// ===========================================================================

TEST_CASE("Workflow Deploy: step ordering discover->package->clone->pull->activate", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    // Discovery
    SetupDiscoverySuccess(session, codec);
    // Package exists
    SetupPackageExistsSuccess(session, codec);
    // Clone already linked (returns KEY1)
    SetupCloneAlreadyLinked(session, codec);
    // Pull
    SetupPullSuccess(session, codec);
    // Activate
    SetupActivateSuccess(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    CHECK(result.Value().discovery.outcome == StepOutcome::Completed);

    REQUIRE(result.Value().repo_results.size() == 1);
    const auto& repo = result.Value().repo_results[0];
    CHECK(repo.repo_name == "test-repo");
    CHECK(repo.success);

    // Verify step names in order.
    REQUIRE(repo.steps.size() == 4);
    CHECK(repo.steps[0].step_name == "package");
    CHECK(repo.steps[1].step_name == "clone");
    CHECK(repo.steps[2].step_name == "pull");
    CHECK(repo.steps[3].step_name == "activate");
}

// ===========================================================================
// Deploy — package exists -> skipped (still Completed in our impl)
// ===========================================================================

TEST_CASE("Workflow Deploy: package exists does not fail", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);
    SetupPullSuccess(session, codec);
    SetupActivateSkipped(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    const auto& repo = result.Value().repo_results[0];
    CHECK(repo.steps[0].step_name == "package");
    CHECK(repo.steps[0].outcome == StepOutcome::Completed);
}

// ===========================================================================
// Deploy — clone already linked -> skipped
// ===========================================================================

TEST_CASE("Workflow Deploy: clone skipped when already linked", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);
    SetupPullSuccess(session, codec);
    SetupActivateSkipped(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    const auto& repo = result.Value().repo_results[0];
    CHECK(repo.steps[1].step_name == "clone");
    CHECK(repo.steps[1].outcome == StepOutcome::Skipped);
}

// ===========================================================================
// Deploy — pull failed -> subsequent steps don't run
// ===========================================================================

TEST_CASE("Workflow Deploy: pull failure stops subsequent steps", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);

    // Pull fails: CSRF OK, POST->500
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {500, {}, "Error"}));

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().success);
    const auto& repo = result.Value().repo_results[0];
    CHECK_FALSE(repo.success);

    // Should have package, clone, pull steps — but NOT activate (stopped after pull).
    REQUIRE(repo.steps.size() == 3);
    CHECK(repo.steps[0].step_name == "package");
    CHECK(repo.steps[1].step_name == "clone");
    CHECK(repo.steps[2].step_name == "pull");
    CHECK(repo.steps[2].outcome == StepOutcome::Failed);
}

// ===========================================================================
// Deploy — discovery fails -> no repo steps
// ===========================================================================

TEST_CASE("Workflow Deploy: discovery failure stops entire workflow", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/discovery", std::nullopt,
              "connection refused", std::nullopt}));

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().success);
    CHECK(result.Value().repo_results.empty());
    CHECK(result.Value().discovery.outcome == StepOutcome::Failed);
}

// ===========================================================================
// Deploy — activation disabled for repo -> skipped
// ===========================================================================

TEST_CASE("Workflow Deploy: activation skipped when repo.activate=false", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();
    config.repos[0].activate = false;

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);
    SetupPullSuccess(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    const auto& repo = result.Value().repo_results[0];
    REQUIRE(repo.steps.size() == 4);
    CHECK(repo.steps[3].step_name == "activate");
    CHECK(repo.steps[3].outcome == StepOutcome::Skipped);
}

// ===========================================================================
// Deploy — no inactive objects -> activate skipped
// ===========================================================================

TEST_CASE("Workflow Deploy: activation skipped when no inactive objects", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);
    SetupPullSuccess(session, codec);
    SetupActivateSkipped(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    const auto& repo = result.Value().repo_results[0];
    CHECK(repo.steps[3].step_name == "activate");
    CHECK(repo.steps[3].outcome == StepOutcome::Skipped);
}

// ===========================================================================
// Deploy — timing info present
// ===========================================================================

TEST_CASE("Workflow Deploy: results carry timing info", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    SetupDiscoverySuccess(session, codec);
    SetupPackageExistsSuccess(session, codec);
    SetupCloneAlreadyLinked(session, codec);
    SetupPullSuccess(session, codec);
    SetupActivateSkipped(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    // total_duration should be non-negative (could be 0ms in fast tests)
    CHECK(result.Value().total_duration.count() >= 0);
    CHECK(result.Value().discovery.duration.count() >= 0);

    for (const auto& repo : result.Value().repo_results) {
        CHECK(repo.elapsed.count() >= 0);
        for (const auto& step : repo.steps) {
            CHECK(step.duration.count() >= 0);
        }
    }
}

// ===========================================================================
// Deploy — unimplemented subcommand
// ===========================================================================

TEST_CASE("Workflow Execute: unimplemented subcommand returns error", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Status);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "Subcommand not yet implemented");
}

// ===========================================================================
// Deploy — multi-repo ordering
// ===========================================================================

TEST_CASE("Workflow Deploy: multi-repo processes in config order", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    auto config = MakeSingleRepoConfig();

    // Add second repo (activate=false).
    config.repos.push_back(RepoConfig{
        "second-repo",
        RepoUrl::Create("https://github.com/org/repo2.git").Value(),
        BranchRef::Create("refs/heads/main").Value(),
        PackageName::Create("ZREPO2").Value(),
        false,
        {},
    });

    // Both repos present in repo list (mock codec returns same canned response).
    std::vector<RepoInfo> all_repos = {
        {"KEY1", "https://github.com/org/repo.git", "refs/heads/main", "ZTEST",
         RepoStatusEnum::Active, "Linked"},
        {"KEY2", "https://github.com/org/repo2.git", "refs/heads/main", "ZREPO2",
         RepoStatusEnum::Active, "Linked"},
    };
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(all_repos));

    // Discovery
    SetupDiscoverySuccess(session, codec);

    // Repo 1: package exists + clone already linked + pull + activate skipped
    SetupPackageExistsSuccess(session, codec);
    // FindRepo for repo1 (GET repos)
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<repos/>"}));
    // Pull repo1
    SetupPullSuccess(session, codec);
    // Activate repo1 (no inactive objects)
    SetupActivateSkipped(session, codec);

    // Repo 2: package exists + clone already linked + pull (activate=false)
    // Package exists
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<pkg/>"}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<pkg-info/>"}));
    // FindRepo for repo2 (GET repos)
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<repos/>"}));
    // Pull repo2
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-5")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<no-changes/>"}));
    // activate=false, so no activate step.

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    REQUIRE(result.Value().repo_results.size() == 2);
    CHECK(result.Value().repo_results[0].repo_name == "test-repo");
    CHECK(result.Value().repo_results[1].repo_name == "second-repo");
    CHECK(result.Value().repo_results[0].success);
    CHECK(result.Value().repo_results[1].success);
}

// ===========================================================================
// Deploy — empty repo list
// ===========================================================================

TEST_CASE("Workflow Deploy: empty repo list succeeds immediately", "[workflow]") {
    MockAdtSession session;
    MockXmlCodec codec;
    AppConfig config;
    config.connection.host = "sap.example.com";
    config.timeout_seconds = 600;

    SetupDiscoverySuccess(session, codec);

    DeployWorkflow wf(session, codec, config);
    auto result = wf.Execute(Subcommand::Deploy);

    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
    CHECK(result.Value().repo_results.empty());
    CHECK(result.Value().summary == "0 succeeded, 0 failed");
}
