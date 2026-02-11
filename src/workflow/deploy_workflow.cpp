#include <erpl_adt/workflow/deploy_workflow.hpp>

#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/abapgit.hpp>
#include <erpl_adt/adt/discovery.hpp>
#include <erpl_adt/adt/packages.hpp>

#include <chrono>
#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

using Clock = std::chrono::steady_clock;

std::chrono::milliseconds Elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}

} // namespace

DeployWorkflow::DeployWorkflow(IAdtSession& session,
                               const IXmlCodec& codec,
                               const AppConfig& config)
    : session_(session), codec_(codec), config_(config) {}

DeployWorkflow::~DeployWorkflow() = default;

Result<DeployResult, Error> DeployWorkflow::Execute(Subcommand cmd) {
    switch (cmd) {
        case Subcommand::Deploy:
            return ExecuteDeploy();
        case Subcommand::Discover:
            return ExecuteDiscover();
        default:
            return Result<DeployResult, Error>::Err(Error{
                "DeployWorkflow", "", std::nullopt,
                "Subcommand not yet implemented", std::nullopt});
    }
}

Result<DeployResult, Error> DeployWorkflow::ExecuteDiscover() {
    auto start = Clock::now();
    auto step = RunDiscovery();

    DeployResult result;
    result.discovery = step;
    result.total_duration = Elapsed(start);

    if (step.outcome == StepOutcome::Failed) {
        result.success = false;
        result.summary = "Discovery failed: " + step.message;
        return Result<DeployResult, Error>::Ok(std::move(result));
    }

    result.success = true;
    result.summary = "Discovery succeeded";
    return Result<DeployResult, Error>::Ok(std::move(result));
}

Result<DeployResult, Error> DeployWorkflow::ExecuteDeploy() {
    auto total_start = Clock::now();
    DeployResult result;

    // Step 1: Discovery.
    result.discovery = RunDiscovery();
    if (result.discovery.outcome == StepOutcome::Failed) {
        result.success = false;
        result.summary = "Discovery failed: " + result.discovery.message;
        result.total_duration = Elapsed(total_start);
        return Result<DeployResult, Error>::Ok(std::move(result));
    }

    // Step 2: Deploy each repo in order (already sorted by depends_on).
    bool any_failed = false;
    for (const auto& repo : config_.repos) {
        auto repo_result = DeployRepo(repo);
        if (!repo_result.success) {
            any_failed = true;
        }
        result.repo_results.push_back(std::move(repo_result));
    }

    // Summary.
    int succeeded = 0;
    int failed = 0;
    for (const auto& r : result.repo_results) {
        if (r.success) {
            ++succeeded;
        } else {
            ++failed;
        }
    }

    result.success = !any_failed;
    result.total_duration = Elapsed(total_start);

    std::ostringstream oss;
    oss << succeeded << " succeeded, " << failed << " failed";
    result.summary = oss.str();

    return Result<DeployResult, Error>::Ok(std::move(result));
}

StepResult DeployWorkflow::RunDiscovery() {
    auto start = Clock::now();
    auto disc = Discover(session_, codec_);

    if (disc.IsErr()) {
        return StepResult{"discover", StepOutcome::Failed,
                          disc.Error().ToString(), Elapsed(start)};
    }

    if (!HasAbapGitSupport(disc.Value())) {
        return StepResult{"discover", StepOutcome::Failed,
                          "abapGit backend not available on this system",
                          Elapsed(start)};
    }

    return StepResult{"discover", StepOutcome::Completed,
                      "abapGit support detected", Elapsed(start)};
}

RepoDeployResult DeployWorkflow::DeployRepo(const RepoConfig& repo) {
    auto repo_start = Clock::now();
    RepoDeployResult result;
    result.repo_name = repo.name;

    // Step 1: Package.
    auto pkg_step = RunPackageStep(repo);
    result.steps.push_back(pkg_step);
    if (pkg_step.outcome == StepOutcome::Failed) {
        result.success = false;
        result.message = "Package step failed: " + pkg_step.message;
        result.elapsed = Elapsed(repo_start);
        return result;
    }

    // Step 2: Clone.
    auto clone_step = RunCloneStep(repo);
    result.steps.push_back(clone_step);

    std::string repo_key;
    if (clone_step.outcome == StepOutcome::Failed) {
        result.success = false;
        result.message = "Clone step failed: " + clone_step.message;
        result.elapsed = Elapsed(repo_start);
        return result;
    }
    // Extract repo_key from the clone step message (stored as "key:VALUE").
    // If clone was skipped (already linked), extract key from message too.
    {
        auto pos = clone_step.message.find("key:");
        if (pos != std::string::npos) {
            repo_key = clone_step.message.substr(pos + 4);
        }
    }

    // Step 3: Pull.
    if (!repo_key.empty()) {
        auto pull_step = RunPullStep(repo, repo_key);
        result.steps.push_back(pull_step);
        if (pull_step.outcome == StepOutcome::Failed) {
            result.success = false;
            result.message = "Pull step failed: " + pull_step.message;
            result.elapsed = Elapsed(repo_start);
            return result;
        }
    }

    // Step 4: Activate.
    if (repo.activate) {
        auto act_step = RunActivateStep();
        result.steps.push_back(act_step);
        if (act_step.outcome == StepOutcome::Failed) {
            result.success = false;
            result.message = "Activation step failed: " + act_step.message;
            result.elapsed = Elapsed(repo_start);
            return result;
        }
    } else {
        result.steps.push_back(StepResult{
            "activate", StepOutcome::Skipped,
            "activation disabled for this repo",
            std::chrono::milliseconds{0}});
    }

    result.success = true;
    result.message = "deployed successfully";
    result.elapsed = Elapsed(repo_start);
    return result;
}

StepResult DeployWorkflow::RunPackageStep(const RepoConfig& repo) {
    auto start = Clock::now();

    auto ensure = EnsurePackage(session_, codec_, repo.package,
                                repo.name, "LOCAL");
    if (ensure.IsErr()) {
        return StepResult{"package", StepOutcome::Failed,
                          ensure.Error().ToString(), Elapsed(start)};
    }

    return StepResult{"package", StepOutcome::Completed,
                      "package ensured: " + repo.package.Value(),
                      Elapsed(start)};
}

StepResult DeployWorkflow::RunCloneStep(const RepoConfig& repo) {
    auto start = Clock::now();
    auto timeout = std::chrono::seconds(config_.timeout_seconds);

    // Check if already linked.
    auto existing = FindRepo(session_, codec_, repo.url);
    if (existing.IsErr()) {
        return StepResult{"clone", StepOutcome::Failed,
                          existing.Error().ToString(), Elapsed(start)};
    }

    if (existing.Value().has_value()) {
        const auto& info = *existing.Value();
        return StepResult{"clone", StepOutcome::Skipped,
                          "already linked, key:" + info.key,
                          Elapsed(start)};
    }

    // Clone.
    auto branch = repo.branch.value_or(BranchRef::Create("refs/heads/main").Value());
    auto cloned = CloneRepo(session_, codec_, repo.url, branch, repo.package, timeout);
    if (cloned.IsErr()) {
        return StepResult{"clone", StepOutcome::Failed,
                          cloned.Error().ToString(), Elapsed(start)};
    }

    return StepResult{"clone", StepOutcome::Completed,
                      "cloned, key:" + cloned.Value().key,
                      Elapsed(start)};
}

StepResult DeployWorkflow::RunPullStep(const RepoConfig& /*repo*/,
                                       const std::string& repo_key) {
    auto start = Clock::now();
    auto timeout = std::chrono::seconds(config_.timeout_seconds);

    auto key = RepoKey::Create(repo_key);
    if (key.IsErr()) {
        return StepResult{"pull", StepOutcome::Failed,
                          "invalid repo key: " + key.Error(),
                          Elapsed(start)};
    }

    auto pull = PullRepo(session_, codec_, key.Value(), timeout);
    if (pull.IsErr()) {
        return StepResult{"pull", StepOutcome::Failed,
                          pull.Error().ToString(), Elapsed(start)};
    }

    if (pull.Value().status == PollStatus::Completed) {
        return StepResult{"pull", StepOutcome::Completed, "pull completed",
                          Elapsed(start)};
    }

    return StepResult{"pull", StepOutcome::Failed,
                      "pull did not complete", Elapsed(start)};
}

StepResult DeployWorkflow::RunActivateStep() {
    auto start = Clock::now();
    auto timeout = std::chrono::seconds(config_.timeout_seconds);

    auto inactive = GetInactiveObjects(session_, codec_);
    if (inactive.IsErr()) {
        return StepResult{"activate", StepOutcome::Failed,
                          inactive.Error().ToString(), Elapsed(start)};
    }

    if (inactive.Value().empty()) {
        return StepResult{"activate", StepOutcome::Skipped,
                          "no inactive objects", Elapsed(start)};
    }

    auto act = ActivateAll(session_, codec_, inactive.Value(), timeout);
    if (act.IsErr()) {
        return StepResult{"activate", StepOutcome::Failed,
                          act.Error().ToString(), Elapsed(start)};
    }

    std::ostringstream oss;
    oss << "activated " << act.Value().activated << "/" << act.Value().total;
    if (act.Value().failed > 0) {
        oss << " (" << act.Value().failed << " failed)";
    }

    auto outcome = (act.Value().failed > 0) ? StepOutcome::Failed : StepOutcome::Completed;
    return StepResult{"activate", outcome, oss.str(), Elapsed(start)};
}

} // namespace erpl_adt
