#include <erpl_adt/adt/checks.hpp>

#include <tinyxml2.h>

#include <cstdlib>
#include <string>

namespace erpl_adt {

namespace {

Result<std::string, Error> CreateWorklist(
    IAdtSession& session,
    const std::string& check_variant) {
    auto url = "/sap/bc/adt/atc/worklists?checkVariant=" + check_variant;

    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<std::string, Error>::Err(Error{
            "RunAtcCheck", url, http.status_code,
            "Failed to create ATC worklist", std::nullopt,
            ErrorCategory::CheckError});
    }

    // Response body is the worklist ID.
    auto id = http.body;
    // Trim whitespace.
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) {
        id.pop_back();
    }
    if (id.empty()) {
        return Result<std::string, Error>::Err(Error{
            "RunAtcCheck", url, std::nullopt,
            "Empty worklist ID in response", std::nullopt,
            ErrorCategory::CheckError});
    }

    return Result<std::string, Error>::Ok(std::move(id));
}

Result<void, Error> CreateRun(
    IAdtSession& session,
    const std::string& worklist_id,
    const std::string& object_uri) {
    auto url = "/sap/bc/adt/atc/runs?worklistId=" + worklist_id;

    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<atc:run maximumVerdicts=\"100\" xmlns:atc=\"http://www.sap.com/adt/atc\">\n"
        "  <objectSets xmlns:adtcore=\"http://www.sap.com/adt/core\">\n"
        "    <objectSet kind=\"inclusive\">\n"
        "      <adtcore:objectReferences>\n"
        "        <adtcore:objectReference adtcore:uri=\"" + object_uri + "\"/>\n"
        "      </adtcore:objectReferences>\n"
        "    </objectSet>\n"
        "  </objectSets>\n"
        "</atc:run>\n";

    auto response = session.Post(url, body, "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<void, Error>::Err(Error{
            "RunAtcCheck", url, http.status_code,
            "Failed to create ATC run", std::nullopt,
            ErrorCategory::CheckError});
    }

    return Result<void, Error>::Ok();
}

Result<std::vector<AtcFinding>, Error> GetWorklistFindings(
    IAdtSession& session,
    const std::string& worklist_id) {
    auto url = "/sap/bc/adt/atc/worklists/" + worklist_id;

    HttpHeaders headers;
    headers["Accept"] = "application/atc.worklist.v1+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<AtcFinding>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<AtcFinding>, Error>::Err(Error{
            "RunAtcCheck", url, http.status_code,
            "Failed to get ATC worklist results", std::nullopt,
            ErrorCategory::CheckError});
    }

    // Parse findings from worklist XML.
    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<AtcFinding>, Error>::Err(Error{
            "RunAtcCheck", url, std::nullopt,
            "Failed to parse ATC worklist XML", std::nullopt,
            ErrorCategory::CheckError});
    }

    std::vector<AtcFinding> findings;

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<AtcFinding>, Error>::Ok(std::move(findings));
    }

    // Navigate to objects > object > findings > finding
    auto* objects = root->FirstChildElement("objects");
    if (!objects) objects = root; // fallback to root level

    for (auto* obj = objects->FirstChildElement("object"); obj;
         obj = obj->NextSiblingElement("object")) {
        auto* findings_el = obj->FirstChildElement("findings");
        if (!findings_el) continue;

        for (auto* f = findings_el->FirstChildElement("finding"); f;
             f = f->NextSiblingElement("finding")) {
            AtcFinding finding;

            const char* uri = f->Attribute("uri");
            finding.uri = uri ? uri : "";

            const char* priority = f->Attribute("priority");
            finding.priority = priority ? std::atoi(priority) : 0;

            const char* check = f->Attribute("checkTitle");
            finding.check_title = check ? check : "";

            const char* msg_title = f->Attribute("messageTitle");
            finding.message_title = msg_title ? msg_title : "";

            const char* msg = f->Attribute("message");
            if (!msg) {
                // Try getting text content
                msg = f->GetText();
            }
            finding.message = msg ? msg : "";

            findings.push_back(std::move(finding));
        }
    }

    return Result<std::vector<AtcFinding>, Error>::Ok(std::move(findings));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RunAtcCheck
// ---------------------------------------------------------------------------
Result<AtcResult, Error> RunAtcCheck(
    IAdtSession& session,
    const std::string& uri,
    const std::string& check_variant) {
    // Step 1: Create worklist.
    auto worklist_result = CreateWorklist(session, check_variant);
    if (worklist_result.IsErr()) {
        return Result<AtcResult, Error>::Err(std::move(worklist_result).Error());
    }
    auto worklist_id = std::move(worklist_result).Value();

    // Step 2: Create run.
    auto run_result = CreateRun(session, worklist_id, uri);
    if (run_result.IsErr()) {
        return Result<AtcResult, Error>::Err(std::move(run_result).Error());
    }

    // Step 3: Get findings.
    auto findings_result = GetWorklistFindings(session, worklist_id);
    if (findings_result.IsErr()) {
        return Result<AtcResult, Error>::Err(std::move(findings_result).Error());
    }

    AtcResult result;
    result.worklist_id = worklist_id;
    result.findings = std::move(findings_result).Value();
    return Result<AtcResult, Error>::Ok(std::move(result));
}

} // namespace erpl_adt
