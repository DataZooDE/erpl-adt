#include <erpl_adt/adt/bw_activation.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
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
    if (options.exec_checks) {
        xml += R"( execChk="true")";
    }
    if (options.with_cto) {
        xml += R"( withCTO="true")";
    }
    xml += ">";

    for (const auto& obj : options.objects) {
        xml += R"(<object objectName=")" + adt_utils::XmlEscape(obj.name) + R"(")";
        xml += R"( objectType=")" + adt_utils::XmlEscape(obj.type) + R"(")";
        xml += R"( objectVersion=")" + adt_utils::XmlEscape(obj.version) + R"(")";
        xml += R"( technicalObjectName=")" + adt_utils::XmlEscape(obj.name) + R"(")";
        xml += R"( objectSubtype=")" + adt_utils::XmlEscape(obj.subtype) + R"(")";
        xml += R"( objectDesc=")" + adt_utils::XmlEscape(obj.description) + R"(")";
        xml += R"( objectStatus=")" + adt_utils::XmlEscape(obj.status) + R"(")";
        xml += R"( activateObj="true")";
        xml += R"( associationType="")";
        xml += R"( corrnum=")" + adt_utils::XmlEscape(obj.transport) + R"(")";
        xml += R"( package=")" + adt_utils::XmlEscape(obj.package_name) + R"(")";
        xml += R"( href=")" + adt_utils::XmlEscape(obj.uri) + R"(")";
        xml += R"( hrefType=""/>)";
    }

    xml += "</bwActivation:objects>";
    return xml;
}

std::string BuildActivationUrl(const BwActivateOptions& options) {
    std::string base = options.endpoint_override.has_value() &&
                               !options.endpoint_override->empty()
                           ? *options.endpoint_override
                           : std::string(kBwActivationPath);
    std::string url = std::move(base);
    url += (url.find('?') == std::string::npos) ? "?mode=" : "&mode=";

    switch (options.mode) {
        case BwActivationMode::Validate:
            url += "validate";
            url += std::string("&sort=") + (options.sort ? "true" : "false");
            url += std::string("&onlyina=") + (options.only_inactive ? "true" : "false");
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
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwActivateObjects", kBwActivationPath,
            "Failed to parse BW activation response XML")) {
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
            msg.severity = xml_utils::AttrAny(el, "severity", "type");
            if (msg.severity.empty()) msg.severity = "I";
            msg.object_name = xml_utils::Attr(el, "objectName");
            msg.object_type = xml_utils::Attr(el, "objectType");
            if (el->GetText()) {
                msg.text = el->GetText();
            } else {
                msg.text = xml_utils::Attr(el, "text");
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
                    msg.severity = xml_utils::AttrAny(msg_el, "severity", "type");
                    if (msg.severity.empty()) msg.severity = "I";
                    msg.object_name = xml_utils::Attr(el, "objectName");
                    msg.object_type = xml_utils::Attr(el, "objectType");
                    if (msg_el->GetText()) {
                        msg.text = msg_el->GetText();
                    } else {
                        msg.text = xml_utils::Attr(msg_el, "text");
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
