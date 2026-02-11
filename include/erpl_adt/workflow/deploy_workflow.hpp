#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/config/app_config.hpp>
#include <erpl_adt/core/result.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Subcommand — the CLI subcommand to execute.
// ---------------------------------------------------------------------------
enum class Subcommand {
    Deploy,    // Full workflow (default)
    Status,    // Show state
    Pull,      // Pull only
    Activate,  // Activate only
    Discover,  // Probe endpoints
};

// ---------------------------------------------------------------------------
// StepOutcome — outcome for each phase of the workflow.
// ---------------------------------------------------------------------------
enum class StepOutcome {
    Completed,
    Skipped,
    Failed,
};

// ---------------------------------------------------------------------------
// StepResult — outcome + timing for a single workflow step.
// ---------------------------------------------------------------------------
struct StepResult {
    std::string step_name;
    StepOutcome outcome = StepOutcome::Failed;
    std::string message;
    std::chrono::milliseconds duration{0};
};

// ---------------------------------------------------------------------------
// RepoDeployResult — per-repo results from the workflow.
// ---------------------------------------------------------------------------
struct RepoDeployResult {
    std::string repo_name;
    bool success = false;
    std::string message;
    std::chrono::milliseconds elapsed{0};
    std::vector<StepResult> steps;
};

// ---------------------------------------------------------------------------
// DeployResult — aggregated results from the full workflow run.
// ---------------------------------------------------------------------------
struct DeployResult {
    bool success = false;
    std::vector<RepoDeployResult> repo_results;
    std::string summary;
    StepResult discovery;
    std::chrono::milliseconds total_duration{0};
};

// ---------------------------------------------------------------------------
// DeployWorkflow — idempotent state machine: discover -> package -> clone ->
//                  pull -> activate.
//
// Takes ownership of nothing — references to session and codec must outlive
// this object.
// ---------------------------------------------------------------------------
class DeployWorkflow {
public:
    DeployWorkflow(IAdtSession& session,
                   const IXmlCodec& codec,
                   const AppConfig& config);

    ~DeployWorkflow();

    // Non-copyable, non-movable.
    DeployWorkflow(const DeployWorkflow&) = delete;
    DeployWorkflow& operator=(const DeployWorkflow&) = delete;
    DeployWorkflow(DeployWorkflow&&) = delete;
    DeployWorkflow& operator=(DeployWorkflow&&) = delete;

    // Execute the given subcommand.
    [[nodiscard]] Result<DeployResult, Error> Execute(Subcommand cmd);

private:
    IAdtSession& session_;
    const IXmlCodec& codec_;
    const AppConfig& config_;

    Result<DeployResult, Error> ExecuteDeploy();
    Result<DeployResult, Error> ExecuteDiscover();

    StepResult RunDiscovery();
    RepoDeployResult DeployRepo(const RepoConfig& repo);
    StepResult RunPackageStep(const RepoConfig& repo);
    StepResult RunCloneStep(const RepoConfig& repo);
    StepResult RunPullStep(const RepoConfig& repo, const std::string& repo_key);
    StepResult RunActivateStep();
};

} // namespace erpl_adt
