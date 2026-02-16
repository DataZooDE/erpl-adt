#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwJobStatus — status of a background job.
// ---------------------------------------------------------------------------
struct BwJobStatus {
    std::string guid;
    std::string status;          // "N" new, "R" running, "E" error, "W" warning, "S" success
    std::string job_type;        // e.g. "TLOGO_ACTIVATION"
    std::string description;
};

// ---------------------------------------------------------------------------
// BwJobProgress — progress of a background job.
// ---------------------------------------------------------------------------
struct BwJobProgress {
    std::string guid;
    int percentage = 0;
    std::string status;
    std::string description;
};

// ---------------------------------------------------------------------------
// BwJobStep — a step in a background job.
// ---------------------------------------------------------------------------
struct BwJobStep {
    std::string name;
    std::string status;
    std::string description;
};

// ---------------------------------------------------------------------------
// BwJobMessage — a message from a background job.
// ---------------------------------------------------------------------------
struct BwJobMessage {
    std::string severity;        // "E", "W", "I", "S"
    std::string text;
    std::string object_name;
};

// ---------------------------------------------------------------------------
// BwJobInfo — summary or result resource for a BW background job.
// ---------------------------------------------------------------------------
struct BwJobInfo {
    std::string guid;
    std::string status;
    std::string job_type;
    std::string description;
};

// ---------------------------------------------------------------------------
// BwListJobs — list BW background jobs.
//
// Endpoint: GET /sap/bw/modeling/jobs
// Accept:   application/vnd.sap-bw-modeling.jobs+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwJobInfo>, Error> BwListJobs(
    IAdtSession& session);

// ---------------------------------------------------------------------------
// BwGetJobResult — read single BW background job result resource.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}
// Accept:   application/vnd.sap-bw-modeling.jobs.job+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwJobInfo, Error> BwGetJobResult(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwGetJobStatus — get status of a background job.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}/status
// Accept:   application/vnd.sap-bw-modeling.jobs.job.status+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwJobStatus, Error> BwGetJobStatus(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwGetJobProgress — get progress percentage of a background job.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}/progress
// Accept:   application/vnd.sap-bw-modeling.jobs.job.progress+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwJobProgress, Error> BwGetJobProgress(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwGetJobSteps — get step details of a background job.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}/steps
// Accept:   application/vnd.sap-bw-modeling.jobs.steps+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwJobStep>, Error> BwGetJobSteps(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwGetJobStep — get one step resource from a BW background job.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}/steps/{step}
// Accept:   application/vnd.sap-bw-modeling.jobs.step+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwJobStep, Error> BwGetJobStep(
    IAdtSession& session,
    const std::string& job_guid,
    const std::string& step);

// ---------------------------------------------------------------------------
// BwGetJobMessages — get messages from a background job.
//
// Endpoint: GET /sap/bw/modeling/jobs/{guid}/messages
// Accept:   application/vnd.sap-bw-modeling.balmessages+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwJobMessage>, Error> BwGetJobMessages(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwCancelJob — cancel a running background job.
//
// Endpoint: POST /sap/bw/modeling/jobs/{guid}/interrupt
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwCancelJob(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwRestartJob — restart a failed background job.
//
// Endpoint: POST /sap/bw/modeling/jobs/{guid}/restart
// Prerequisite: job must be in error state (status "E").
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwRestartJob(
    IAdtSession& session,
    const std::string& job_guid);

// ---------------------------------------------------------------------------
// BwCleanupJob — cleanup resources of a completed/failed job.
//
// Endpoint: DELETE /sap/bw/modeling/jobs/{guid}/cleanup
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwCleanupJob(
    IAdtSession& session,
    const std::string& job_guid);

} // namespace erpl_adt
