#include <erpl_adt/adt/bw_jobs.hpp>

#include <tinyxml2.h>

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
        return Result<BwJobStatus, Error>::Err(
            Error::FromHttpStatus("BwGetJobStatus", url, http.status_code, http.body));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwJobStatus, Error>::Err(Error{
            "BwGetJobStatus", url, std::nullopt,
            "Failed to parse job status XML", std::nullopt});
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
        return Result<BwJobProgress, Error>::Err(
            Error::FromHttpStatus("BwGetJobProgress", url, http.status_code, http.body));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwJobProgress, Error>::Err(Error{
            "BwGetJobProgress", url, std::nullopt,
            "Failed to parse job progress XML", std::nullopt});
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
            try { result.percentage = std::stoi(pct); }
            catch (...) { result.percentage = 0; }
        }
        if (result.percentage == 0 && root->GetText()) {
            try { result.percentage = std::stoi(root->GetText()); }
            catch (...) {}
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
        return Result<std::vector<BwJobStep>, Error>::Err(
            Error::FromHttpStatus("BwGetJobSteps", url, http.status_code, http.body));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<BwJobStep>, Error>::Err(Error{
            "BwGetJobSteps", url, std::nullopt,
            "Failed to parse job steps XML", std::nullopt});
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
        return Result<std::vector<BwJobMessage>, Error>::Err(
            Error::FromHttpStatus("BwGetJobMessages", url, http.status_code, http.body));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<BwJobMessage>, Error>::Err(Error{
            "BwGetJobMessages", url, std::nullopt,
            "Failed to parse job messages XML", std::nullopt});
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
        return Result<void, Error>::Err(
            Error::FromHttpStatus("BwCancelJob", url, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
