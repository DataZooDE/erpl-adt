#include <erpl_adt/adt/bw_jobs.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <exception>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwJobsBase = "/sap/bw/modeling/jobs/";

std::string JobUrl(const std::string& guid, const std::string& sub_resource = "") {
    auto url = std::string(kBwJobsBase) + guid;
    if (!sub_resource.empty()) {
        url += "/" + sub_resource;
    }
    return url;
}

// Get attribute or empty string.
std::string Attr(const tinyxml2::XMLElement* el, const char* name) {
    const char* val = el->Attribute(name);
    return val ? val : "";
}

} // anonymous namespace

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
        result.status = Attr(root, "status");
        if (result.status.empty()) result.status = Attr(root, "value");
        result.job_type = Attr(root, "jobType");
        result.description = Attr(root, "description");
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
        result.status = Attr(root, "status");
        result.description = Attr(root, "description");
        const char* pct = root->Attribute("percentage");
        if (!pct) pct = root->Attribute("value");
        if (pct) {
            try {
                result.percentage = std::stoi(pct);
            } catch (const std::exception&) {
                result.percentage = 0;
            }
        }
        if (result.percentage == 0 && root->GetText()) {
            try {
                result.percentage = std::stoi(root->GetText());
            } catch (const std::exception&) {
            }
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
            s.name = Attr(step, "name");
            s.status = Attr(step, "status");
            s.description = Attr(step, "description");
            if (s.description.empty() && step->GetText()) {
                s.description = step->GetText();
            }
            steps.push_back(std::move(s));
        }
    }

    return Result<std::vector<BwJobStep>, Error>::Ok(std::move(steps));
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
            m.severity = Attr(msg, "severity");
            if (m.severity.empty()) m.severity = Attr(msg, "type");
            m.object_name = Attr(msg, "objectName");
            m.text = Attr(msg, "text");
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
