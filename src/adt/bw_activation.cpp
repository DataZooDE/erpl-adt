#include <erpl_adt/adt/bw_activation.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/adt/bw_media_types.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace erpl_adt {

namespace {

// Activation runs the checks and commits; checkruns runs the same checks and
// stops there. The backend picks between them by URI path — CL_RSO_RES_
// ACTIVATION::get_activation_mode compares the path, and ignores query
// parameters entirely.
const char* kBwActivationPath = "/sap/bw/modeling/activation";
const char* kBwCheckrunPath = "/sap/bw/modeling/checkruns";

// Advertised by the checkruns collection in the BW discovery document.
const char* kActivationMediaType =
    "application/vnd.sap.bw.modeling.activation-v1_0_0+xml";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ChecksOnly(BwActivationMode mode) {
    // Simulate has no counterpart on this API; a dry run is a check run,
    // which is what the caller means by it.
    return mode == BwActivationMode::Validate || mode == BwActivationMode::Simulate;
}

std::string BuildUrl(const BwActivateOptions& options) {
    if (options.endpoint_override.has_value() &&
        !options.endpoint_override->empty()) {
        return *options.endpoint_override;
    }
    return ChecksOnly(options.mode) ? kBwCheckrunPath : kBwActivationPath;
}

// BW resolves the tlogo from the URI's type segment and only recognises it in
// lower case: "/sap/bw/modeling/ADSO/..." makes the backend dump with an
// HTTP 500 while "/sap/bw/modeling/adso/..." works. Callers spell the type the
// way the user typed it, so normalise here rather than trusting every one of
// them. Later segments are left alone — a source system like ECLCLNT100 is
// case-sensitive.
std::string NormalizeTypeSegment(const std::string& uri) {
    constexpr const char* kBase = "/sap/bw/modeling/";
    constexpr size_t kBaseLen = 17;
    if (uri.compare(0, kBaseLen, kBase) != 0) {
        return uri;
    }
    const auto end = uri.find('/', kBaseLen);
    const auto type_end = (end == std::string::npos) ? uri.size() : end;
    return std::string(kBase) + ToLower(uri.substr(kBaseLen, type_end - kBaseLen)) +
           uri.substr(type_end);
}

std::string ObjectUri(const BwActivationObject& object) {
    if (!object.uri.empty()) {
        return NormalizeTypeSegment(object.uri);
    }
    return std::string("/sap/bw/modeling/") + ToLower(object.type) + "/" +
           ToLower(object.name);
}

// One Atom feed, one entry — the backend rejects anything else with "not
// acceptable". version, modelContent and lockHandle are unconditional in
// RSO_RES_ST_BW_CHECKRUN; transportId and packageName are optional.
std::string BuildActivationFeed(const BwActivationObject& object,
                                const BwActivateOptions& options) {
    const auto media_type = BwDefaultMediaType(object.type);
    const auto transport = !object.transport.empty()
        ? object.transport
        : options.transport.value_or("");

    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>)";
    xml += R"(<atom:feed xmlns:atom="http://www.w3.org/2005/Atom">)";
    xml += R"(<atom:entry>)";
    xml += R"(<atom:link href=")" + adt_utils::XmlEscape(ObjectUri(object)) +
           R"(" rel="self" type=")" + adt_utils::XmlEscape(media_type) + R"("/>)";
    xml += R"(<atom:content type="application/xml">)";
    xml += R"(<bwModel:checkProperties xmlns:bwModel="http://www.sap.com/bw/modeling")";
    xml += R"( version=")" + adt_utils::XmlEscape(object.version) + R"(")";
    // modelContent carries an unsaved model inline; we always activate what is
    // already on the server, so it stays empty.
    xml += R"( modelContent="")";
    xml += R"( lockHandle=")" + adt_utils::XmlEscape(options.lock_handle) + R"(")";
    if (!transport.empty()) {
        xml += R"( transportId=")" + adt_utils::XmlEscape(transport) + R"(")";
    }
    if (!object.package_name.empty()) {
        xml += R"( packageName=")" + adt_utils::XmlEscape(object.package_name) + R"(")";
    }
    xml += R"(/>)";
    xml += R"(</atom:content>)";
    xml += R"(</atom:entry>)";
    xml += R"(</atom:feed>)";
    return xml;
}

// SAP spells the severity out in messageType; the rest of erpl-adt uses the
// single-letter ABAP convention.
std::string SeverityFromMessageType(const std::string& message_type) {
    if (message_type == "Error") return "E";
    if (message_type == "Warning") return "W";
    if (message_type == "Success") return "S";
    if (message_type == "Info" || message_type == "Information") return "I";
    return message_type.empty() ? "I" : message_type.substr(0, 1);
}

// The response is an Atom feed: one entry per check message, the severity in
// the entry's bwModel:checkresult content and the human text in its title.
Result<BwActivationResult, Error> ParseActivationResponse(
    std::string_view xml, const HttpHeaders& response_headers,
    const BwActivationObject& object, const std::string& url) {
    BwActivationResult result;
    result.success = true;

    auto location = adt_utils::FindHeaderValueCi(response_headers, "Location");
    if (location.has_value()) {
        const auto& loc = *location;
        auto jobs_pos = loc.find("/jobs/");
        if (jobs_pos != std::string::npos) {
            result.job_guid = loc.substr(jobs_pos + 6);
        }
    }

    if (xml.empty()) {
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwActivateObjects", url,
            "Failed to parse BW activation response XML")) {
        // HTTP said it worked; an unreadable body is not evidence it did not.
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    const auto* root = doc.RootElement();
    if (root == nullptr) {
        return Result<BwActivationResult, Error>::Ok(std::move(result));
    }

    for (const auto* entry = root->FirstChildElement(); entry != nullptr;
         entry = entry->NextSiblingElement()) {
        if (atom_parser::LocalName(entry->Name()) != "entry") {
            continue;
        }

        BwActivationMessage message;
        message.object_name = object.name;
        message.object_type = object.type;
        message.text = atom_parser::ChildTextByLocalName(entry, "title");

        const auto* content = atom_parser::FirstChildByLocalName(entry, "content");
        const auto* checkresult =
            content != nullptr
                ? atom_parser::FirstChildByLocalName(content, "checkresult")
                : nullptr;
        if (checkresult != nullptr) {
            message.severity =
                SeverityFromMessageType(xml_utils::Attr(checkresult, "messageType"));
        }
        if (message.severity.empty()) {
            message.severity = "I";
        }

        if (message.severity == "E") {
            result.success = false;
        }
        result.messages.push_back(std::move(message));
    }

    return Result<BwActivationResult, Error>::Ok(std::move(result));
}

}  // namespace

Result<BwActivationResult, Error> BwActivateObjects(
    IAdtSession& session,
    const BwActivateOptions& options) {
    if (options.objects.empty()) {
        return Result<BwActivationResult, Error>::Err(Error{
            "BwActivateObjects", kBwActivationPath, std::nullopt,
            "No objects specified for activation", std::nullopt});
    }

    const auto url = BuildUrl(options);

    // Accept as well as Content-Type: BW routes content-negotiate strictly
    // and answer HTTP 415 for */* (issue #41).
    HttpHeaders headers;
    headers["Accept"] = kActivationMediaType;

    BwActivationResult combined;
    combined.success = true;

    // One request per object: the feed must carry exactly one entry.
    for (const auto& object : options.objects) {
        const auto body = BuildActivationFeed(object, options);

        auto response = session.Post(url, body, kActivationMediaType, headers);
        if (response.IsErr()) {
            return Result<BwActivationResult, Error>::Err(
                std::move(response).Error());
        }

        const auto& http = response.Value();
        // 200 = result feed, 202 = async job started.
        if (http.status_code != 200 && http.status_code != 202) {
            auto error = Error::FromHttpStatus("BwActivateObjects", url,
                                               http.status_code, http.body);
            AddBwHint(error);
            return Result<BwActivationResult, Error>::Err(std::move(error));
        }

        auto parsed =
            ParseActivationResponse(http.body, http.headers, object, url);
        if (parsed.IsErr()) {
            return parsed;
        }

        const auto& value = parsed.Value();
        if (!value.success) {
            combined.success = false;
        }
        if (combined.job_guid.empty()) {
            combined.job_guid = value.job_guid;
        }
        combined.messages.insert(combined.messages.end(), value.messages.begin(),
                                 value.messages.end());
    }

    return Result<BwActivationResult, Error>::Ok(std::move(combined));
}

}  // namespace erpl_adt
