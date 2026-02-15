#include <erpl_adt/adt/bw_activation.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwActivationPath = "/sap/bw/modeling/activation";

std::string BuildActivationXml(const BwActivateOptions& options) {
    std::string xml = R"(<bwActivation:objects xmlns:bwActivation="http://www.sap.com/bw/massact")";
    xml += R"( bwChangeable="" basisChangeable="")";
    if (options.force) {
        xml += R"( forceAct="true")";
    }
    xml += ">";

    for (const auto& obj : options.objects) {
        xml += R"(<object objectName=")" + obj.name + R"(")";
        xml += R"( objectType=")" + obj.type + R"(")";
        xml += R"( objectVersion=")" + obj.version + R"(")";
        xml += R"( technicalObjectName=")" + obj.name + R"(")";
        xml += R"( objectSubtype=")" + obj.subtype + R"(")";
        xml += R"( objectDesc=")" + obj.description + R"(")";
        xml += R"( objectStatus=")" + obj.status + R"(")";
        xml += R"( activateObj="true")";
        xml += R"( associationType="")";
        xml += R"( corrnum=")" + obj.transport + R"(")";
        xml += R"( package=")" + obj.package_name + R"(")";
        xml += R"( href=")" + obj.uri + R"(")";
        xml += R"( hrefType=""/>)";
    }

    xml += "</bwActivation:objects>";
    return xml;
}

std::string BuildActivationUrl(const BwActivateOptions& options) {
    std::string url = std::string(kBwActivationPath) + "?mode=";

    switch (options.mode) {
        case BwActivationMode::Validate:
            url += "validate";
            break;
        case BwActivationMode::Simulate:
            url += "activate&simu=true";
            break;
        case BwActivationMode::Background:
            url += "activate&asjob=true";
            break;
        case BwActivationMode::Activate:
        default:
            url += "activate&simu=false";
            break;
    }

    if (options.transport.has_value()) {
        url += "&corrnum=" + *options.transport;
    }

    return url;
}

Result<BwActivationResult, Error> ParseActivationResponse(
    std::string_view xml, const HttpHeaders& response_headers) {
    BwActivationResult result;

    // Check for job GUID in Location header (background mode)
    auto location = adt_utils::FindHeaderValueCi(response_headers, "Location");
    if (location.has_value()) {
        // Extract GUID from URL like /sap/bw/modeling/jobs/ABC123...
        const auto& loc = *location;
        auto jobs_pos = loc.find("/jobs/");
        if (jobs_pos != std::string::npos) {
            result.job_guid = loc.substr(jobs_pos + 6);
        }
    }

    if (xml.empty()) {
        result.success = true;
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        // Non-parseable response but HTTP success — treat as OK
        result.success = true;
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    auto* root = doc.RootElement();
    if (!root) {
        result.success = true;
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    // Check for error messages in the activation result
    bool has_errors = false;
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        const char* el_name = el->Name();
        if (!el_name) continue;
        std::string name_str(el_name);

        // Look for message elements
        if (name_str == "message" || name_str.find(":message") != std::string::npos ||
            name_str == "msg") {
            BwActivationMessage msg;
            const char* severity = el->Attribute("severity");
            if (!severity) severity = el->Attribute("type");
            msg.severity = severity ? severity : "I";
            msg.object_name = el->Attribute("objectName") ?
                              el->Attribute("objectName") : "";
            msg.object_type = el->Attribute("objectType") ?
                              el->Attribute("objectType") : "";
            if (el->GetText()) {
                msg.text = el->GetText();
            } else {
                const char* text_attr = el->Attribute("text");
                msg.text = text_attr ? text_attr : "";
            }
            if (msg.severity == "E") has_errors = true;
            result.messages.push_back(std::move(msg));
        }

        // Look for object elements with status
        if (name_str == "object" || name_str.find(":object") != std::string::npos) {
            const char* status = el->Attribute("objectStatus");
            if (status && std::string(status) == "ACT") {
                // Active means success
            }
            // Check for per-object messages
            for (auto* msg_el = el->FirstChildElement(); msg_el;
                 msg_el = msg_el->NextSiblingElement()) {
                const char* msg_name = msg_el->Name();
                if (!msg_name) continue;
                std::string mn(msg_name);
                if (mn == "message" || mn.find(":message") != std::string::npos) {
                    BwActivationMessage msg;
                    const char* sev = msg_el->Attribute("severity");
                    if (!sev) sev = msg_el->Attribute("type");
                    msg.severity = sev ? sev : "I";
                    msg.object_name = el->Attribute("objectName") ?
                                      el->Attribute("objectName") : "";
                    msg.object_type = el->Attribute("objectType") ?
                                      el->Attribute("objectType") : "";
                    if (msg_el->GetText()) {
                        msg.text = msg_el->GetText();
                    }
                    if (msg.severity == "E") has_errors = true;
                    result.messages.push_back(std::move(msg));
                }
            }
        }
    }

    result.success = !has_errors;
    return Result<BwActivationResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

Result<BwActivationResult, Error> BwActivateObjects(
    IAdtSession& session,
    const BwActivateOptions& options) {
    if (options.objects.empty()) {
        return Result<BwActivationResult, Error>::Err(Error{
            "BwActivateObjects", kBwActivationPath, std::nullopt,
            "No objects specified for activation", std::nullopt});
    }

    auto url = BuildActivationUrl(options);
    auto body = BuildActivationXml(options);

    auto response = session.Post(
        url, body, "application/vnd.sap-bw-modeling.massact+xml");
    if (response.IsErr()) {
        return Result<BwActivationResult, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    // 200 = sync result, 202 = async job started
    if (http.status_code != 200 && http.status_code != 202) {
        auto error = Error::FromHttpStatus("BwActivateObjects", url,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwActivationResult, Error>::Err(std::move(error));
    }

    return ParseActivationResponse(http.body, http.headers);
}

} // namespace erpl_adt
