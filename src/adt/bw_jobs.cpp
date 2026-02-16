#include <erpl_adt/adt/bw_jobs.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwJobsBase = "/sap/bw/modeling/jobs/";
const char* kBwJobsCollectionPath = "/sap/bw/modeling/jobs";

std::string JobUrl(const std::string& guid, const std::string& sub_resource = "") {
    auto url = std::string(kBwJobsBase) + guid;
    if (!sub_resource.empty()) {
        url += "/" + sub_resource;
    }
    return url;
}

BwJobInfo ParseJobInfoElement(const tinyxml2::XMLElement* root,
                              const std::string& fallback_guid = "") {
    BwJobInfo result;
    result.guid = xml_utils::AttrAny(root, "guid", "id");
    if (result.guid.empty()) {
        result.guid = fallback_guid;
    }
    result.status = xml_utils::AttrAny(root, "status", "value");
    result.job_type = xml_utils::Attr(root, "jobType");
    result.description = xml_utils::Attr(root, "description");
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwListJobs
// ---------------------------------------------------------------------------
Result<std::vector<BwJobInfo>, Error> BwListJobs(IAdtSession& session) {
    std::string url = kBwJobsCollectionPath;
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwJobInfo>, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwListJobs", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwJobInfo>, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwListJobs", url, "Failed to parse jobs list XML")) {
        return Result<std::vector<BwJobInfo>, Error>::Err(std::move(*parse_error));
    }

    std::vector<BwJobInfo> jobs;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwJobInfo>, Error>::Ok(std::move(jobs));
    }

    if (std::string(root->Name()) == "job" ||
        std::string(root->Name()).find(":job") != std::string::npos) {
        jobs.push_back(ParseJobInfoElement(root));
        return Result<std::vector<BwJobInfo>, Error>::Ok(std::move(jobs));
    }

    for (auto* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const auto* name = child->Name();
        if (!name) {
            continue;
        }
        std::string name_str(name);
        if (name_str == "job" || name_str.find(":job") != std::string::npos) {
            jobs.push_back(ParseJobInfoElement(child));
        }
    }
    return Result<std::vector<BwJobInfo>, Error>::Ok(std::move(jobs));
}

// ---------------------------------------------------------------------------
// BwGetJobResult
// ---------------------------------------------------------------------------
Result<BwJobInfo, Error> BwGetJobResult(IAdtSession& session,
                                        const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<BwJobInfo, Error>::Err(Error{
            "BwGetJobResult", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid);
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs.job+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwJobInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobResult", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwJobInfo, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobResult", url,
            "Failed to parse job result XML")) {
        return Result<BwJobInfo, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    BwJobInfo info;
    if (root) {
        info = ParseJobInfoElement(root, job_guid);
    } else {
        info.guid = job_guid;
    }
    return Result<BwJobInfo, Error>::Ok(std::move(info));
}

// ---------------------------------------------------------------------------
// BwGetJobStatus
// ---------------------------------------------------------------------------
Result<BwJobStatus, Error> BwGetJobStatus(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<BwJobStatus, Error>::Err(Error{
            "BwGetJobStatus", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "status");
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs.job.status+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwJobStatus, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobStatus", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwJobStatus, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobStatus", url,
            "Failed to parse job status XML")) {
        return Result<BwJobStatus, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    BwJobStatus result;
    result.guid = job_guid;
    if (root) {
        result.status = xml_utils::AttrAny(root, "status", "value");
        result.job_type = xml_utils::Attr(root, "jobType");
        result.description = xml_utils::Attr(root, "description");
        if (result.status.empty() && root->GetText()) {
            result.status = root->GetText();
        }
    }

    return Result<BwJobStatus, Error>::Ok(std::move(result));
}

// ---------------------------------------------------------------------------
// BwGetJobProgress
// ---------------------------------------------------------------------------
Result<BwJobProgress, Error> BwGetJobProgress(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<BwJobProgress, Error>::Err(Error{
            "BwGetJobProgress", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "progress");
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs.job.progress+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwJobProgress, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobProgress", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwJobProgress, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobProgress", url,
            "Failed to parse job progress XML")) {
        return Result<BwJobProgress, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    BwJobProgress result;
    result.guid = job_guid;
    if (root) {
        result.status = xml_utils::Attr(root, "status");
        result.description = xml_utils::Attr(root, "description");
        result.percentage = xml_utils::ParseIntOrDefault(
            xml_utils::AttrAny(root, "percentage", "value"), 0);
        if (result.percentage == 0) {
            result.percentage = xml_utils::TextIntOr(root, 0);
        }
    }

    return Result<BwJobProgress, Error>::Ok(std::move(result));
}

// ---------------------------------------------------------------------------
// BwGetJobSteps
// ---------------------------------------------------------------------------
Result<std::vector<BwJobStep>, Error> BwGetJobSteps(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<std::vector<BwJobStep>, Error>::Err(Error{
            "BwGetJobSteps", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "steps");
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs.steps+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwJobStep>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobSteps", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwJobStep>, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobSteps", url,
            "Failed to parse job steps XML")) {
        return Result<std::vector<BwJobStep>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwJobStep> steps;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* step = root->FirstChildElement(); step;
             step = step->NextSiblingElement()) {
            BwJobStep s;
            s.name = xml_utils::Attr(step, "name");
            s.status = xml_utils::Attr(step, "status");
            s.description = xml_utils::Attr(step, "description");
            if (s.description.empty() && step->GetText()) {
                s.description = step->GetText();
            }
            steps.push_back(std::move(s));
        }
    }

    return Result<std::vector<BwJobStep>, Error>::Ok(std::move(steps));
}

// ---------------------------------------------------------------------------
// BwGetJobStep
// ---------------------------------------------------------------------------
Result<BwJobStep, Error> BwGetJobStep(
    IAdtSession& session,
    const std::string& job_guid,
    const std::string& step) {
    if (job_guid.empty()) {
        return Result<BwJobStep, Error>::Err(Error{
            "BwGetJobStep", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }
    if (step.empty()) {
        return Result<BwJobStep, Error>::Err(Error{
            "BwGetJobStep", "", std::nullopt,
            "Step must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "steps/" + step);
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.jobs.step+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwJobStep, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobStep", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwJobStep, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobStep", url,
            "Failed to parse job step XML")) {
        return Result<BwJobStep, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    BwJobStep result;
    result.name = step;
    if (root) {
        if (!xml_utils::Attr(root, "name").empty()) {
            result.name = xml_utils::Attr(root, "name");
        }
        result.status = xml_utils::Attr(root, "status");
        result.description = xml_utils::Attr(root, "description");
        if (result.description.empty() && root->GetText()) {
            result.description = root->GetText();
        }
    }
    return Result<BwJobStep, Error>::Ok(std::move(result));
}

// ---------------------------------------------------------------------------
// BwGetJobMessages
// ---------------------------------------------------------------------------
Result<std::vector<BwJobMessage>, Error> BwGetJobMessages(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<std::vector<BwJobMessage>, Error>::Err(Error{
            "BwGetJobMessages", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "messages");
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.balmessages+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwJobMessage>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetJobMessages", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwJobMessage>, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwGetJobMessages", url,
            "Failed to parse job messages XML")) {
        return Result<std::vector<BwJobMessage>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwJobMessage> messages;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* msg = root->FirstChildElement(); msg;
             msg = msg->NextSiblingElement()) {
            BwJobMessage m;
            m.severity = xml_utils::AttrAny(msg, "severity", "type");
            m.object_name = xml_utils::Attr(msg, "objectName");
            m.text = xml_utils::Attr(msg, "text");
            if (m.text.empty() && msg->GetText()) {
                m.text = msg->GetText();
            }
            messages.push_back(std::move(m));
        }
    }

    return Result<std::vector<BwJobMessage>, Error>::Ok(std::move(messages));
}

// ---------------------------------------------------------------------------
// BwCancelJob
// ---------------------------------------------------------------------------
Result<void, Error> BwCancelJob(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<void, Error>::Err(Error{
            "BwCancelJob", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "interrupt");

    auto response = session.Post(
        url, "", "application/vnd.sap-bw-modeling.jobs.job.interrupt+xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwCancelJob", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// BwRestartJob
// ---------------------------------------------------------------------------
Result<void, Error> BwRestartJob(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<void, Error>::Err(Error{
            "BwRestartJob", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "restart");

    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwRestartJob", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// BwCleanupJob
// ---------------------------------------------------------------------------
Result<void, Error> BwCleanupJob(
    IAdtSession& session,
    const std::string& job_guid) {
    if (job_guid.empty()) {
        return Result<void, Error>::Err(Error{
            "BwCleanupJob", "", std::nullopt,
            "Job GUID must not be empty", std::nullopt});
    }

    auto url = JobUrl(job_guid, "cleanup");

    auto response = session.Delete(url);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwCleanupJob", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
