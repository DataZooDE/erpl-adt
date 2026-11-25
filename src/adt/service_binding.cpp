#include <erpl_adt/adt/service_binding.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/core/url.hpp>

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace erpl_adt {

namespace {

constexpr const char* kAdtCoreNs = "http://www.sap.com/adt/core";

std::string Upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string_view LocalName(const char* qualified) {
    std::string_view name(qualified ? qualified : "");
    const auto colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

// The first child element whose local name (prefix ignored) is `name`.
const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* parent,
                                  std::string_view name) {
    if (!parent) return nullptr;
    for (auto* e = parent->FirstChildElement(); e; e = e->NextSiblingElement()) {
        if (LocalName(e->Name()) == name) return e;
    }
    return nullptr;
}

std::string Text(const tinyxml2::XMLElement* element) {
    if (!element || !element->GetText()) return {};
    return element->GetText();
}

std::string BuildBody(const std::string& name) {
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    auto* root = doc.NewElement("adtcore:objectReferences");
    root->SetAttribute("xmlns:adtcore", kAdtCoreNs);
    auto* ref = doc.NewElement("adtcore:objectReference");
    ref->SetAttribute("adtcore:name", name.c_str());
    root->InsertEndChild(ref);
    doc.InsertEndChild(root);
    tinyxml2::XMLPrinter printer(nullptr, true);  // compact
    doc.Print(&printer);
    return printer.CStr();
}

const char* OperationName(ServicePublishAction action) {
    return action == ServicePublishAction::Publish ? "PublishServiceBinding"
                                                   : "UnpublishServiceBinding";
}

Result<ServicePublishResult, Error> RunPublishJob(IAdtSession& session,
                                                  ServicePublishAction action,
                                                  std::string_view service_name,
                                                  std::string_view service_version,
                                                  ODataVersion odata) {
    const std::string operation = OperationName(action);
    if (service_name.empty()) {
        return Result<ServicePublishResult, Error>::Err(
            Error{operation, "", std::nullopt, "Service binding name is required",
                  std::nullopt, ErrorCategory::Validation});
    }
    if (service_version.empty()) {
        return Result<ServicePublishResult, Error>::Err(
            Error{operation, "", std::nullopt, "Service version is required (e.g. 0001)",
                  std::nullopt, ErrorCategory::Validation});
    }

    const std::string name = Upper(service_name);
    const std::string version(service_version);
    const std::string path = ServicePublishPath(action, odata, name, version);

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ServicePublishResult, Error>::Err(std::move(csrf).Error());
    }

    HttpHeaders headers;
    headers["x-csrf-token"] = csrf.Value();
    headers["Accept"] = "application/*";
    auto response = session.Post(path, BuildBody(name), "application/xml", headers);
    if (response.IsErr()) {
        return Result<ServicePublishResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code < 200 || http.status_code >= 300) {
        return Result<ServicePublishResult, Error>::Err(
            Error::FromHttpStatus(operation, path, http.status_code, http.body));
    }

    auto parsed = ParseServicePublishResponse(http.body);
    if (parsed.IsErr()) {
        auto error = std::move(parsed).Error();
        error.operation = operation;
        error.endpoint = path;
        return Result<ServicePublishResult, Error>::Err(std::move(error));
    }

    auto result = std::move(parsed).Value();
    result.binding = name;
    result.service_version = version;
    result.odata = odata;
    result.action = action;
    return Result<ServicePublishResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

Result<ODataVersion, Error> ParseODataVersion(std::string_view text) {
    if (adt_utils::IEquals(text, "v2")) return Result<ODataVersion, Error>::Ok(ODataVersion::V2);
    if (adt_utils::IEquals(text, "v4")) return Result<ODataVersion, Error>::Ok(ODataVersion::V4);
    return Result<ODataVersion, Error>::Err(
        Error{"ParseODataVersion", "", std::nullopt,
              "Unknown OData version '" + std::string(text) + "' (expected v2 or v4)",
              std::nullopt, ErrorCategory::Validation});
}

std::string ODataVersionName(ODataVersion version) {
    return version == ODataVersion::V4 ? "v4" : "v2";
}

bool ServicePublishResult::Succeeded() const {
    return adt_utils::IEquals(severity, "OK");
}

std::string ServicePublishPath(ServicePublishAction action,
                               ODataVersion odata,
                               std::string_view service_name,
                               std::string_view service_version) {
    std::string path = "/sap/bc/adt/businessservices/odata";
    path += ODataVersionName(odata);
    path += action == ServicePublishAction::Publish ? "/publishjobs" : "/unpublishjobs";
    path += "?servicename=" + UrlEncode(std::string(service_name));
    path += "&serviceversion=" + UrlEncode(std::string(service_version));
    return path;
}

Result<ServicePublishResult, Error> ParseServicePublishResponse(std::string_view xml) {
    auto invalid = [&](const std::string& why) {
        std::string snippet(xml.substr(0, 200));
        return Result<ServicePublishResult, Error>::Err(
            Error{"ParseServicePublishResponse", "", std::nullopt,
                  "Unexpected publish job answer (" + why + ")" +
                      (snippet.empty() ? std::string() : ": " + snippet),
                  std::nullopt, ErrorCategory::Internal});
    };

    tinyxml2::XMLDocument doc;
    if (xml.empty() || doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return invalid("not XML");
    }
    const auto* root = doc.RootElement();
    if (!root || LocalName(root->Name()) != "abap") return invalid("no asx:abap");
    const auto* data = Child(Child(root, "values"), "DATA");
    if (!data) return invalid("no asx:values/DATA");
    const auto* severity = Child(data, "SEVERITY");
    if (!severity) return invalid("no SEVERITY");

    ServicePublishResult result;
    result.severity = Text(severity);
    result.short_text = Text(Child(data, "SHORT_TEXT"));
    result.long_text = Text(Child(data, "LONG_TEXT"));
    return Result<ServicePublishResult, Error>::Ok(std::move(result));
}

Result<ServicePublishResult, Error> PublishServiceBinding(IAdtSession& session,
                                                          std::string_view service_name,
                                                          std::string_view service_version,
                                                          ODataVersion odata) {
    return RunPublishJob(session, ServicePublishAction::Publish, service_name,
                         service_version, odata);
}

Result<ServicePublishResult, Error> UnpublishServiceBinding(IAdtSession& session,
                                                            std::string_view service_name,
                                                            std::string_view service_version,
                                                            ODataVersion odata) {
    return RunPublishJob(session, ServicePublishAction::Unpublish, service_name,
                         service_version, odata);
}

nlohmann::json ServicePublishResultToJson(const ServicePublishResult& result) {
    return nlohmann::json{
        {"binding", result.binding},
        {"service_version", result.service_version},
        {"odata", ODataVersionName(result.odata)},
        {"action", result.action == ServicePublishAction::Publish ? "publish" : "unpublish"},
        {"severity", result.severity},
        {"short_text", result.short_text},
        {"long_text", result.long_text},
        {"ok", result.Succeeded()},
    };
}

} // namespace erpl_adt
