#include <erpl_adt/adt/activation.hpp>
#include "adt_utils.hpp"

#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kInactivePath = "/sap/bc/adt/activation/inactive";
const char* kActivationPath = "/sap/bc/adt/activation";
const char* kActivationContentType = "application/vnd.sap.adt.activation.v1+xml";
const char* kActivateObjectPath = "/sap/bc/adt/activation?method=activate&preauditRequested=true";
constexpr const char* kNsAdtCore = "http://www.sap.com/adt/core";

std::string BuildActivationRequestXml(const ActivateObjectParams& params) {
    tinyxml2::XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    auto* root = doc.NewElement("adtcore:objectReferences");
    root->SetAttribute("xmlns:adtcore", kNsAdtCore);
    doc.InsertEndChild(root);

    auto* ref = doc.NewElement("adtcore:objectReference");
    ref->SetAttribute("adtcore:uri", params.uri.c_str());
    if (!params.type.empty()) {
        ref->SetAttribute("adtcore:type", params.type.c_str());
    }
    if (!params.name.empty()) {
        ref->SetAttribute("adtcore:name", params.name.c_str());
    }
    root->InsertEndChild(ref);

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr());
}

ActivationResult ParseActivationResultXml(std::string_view xml) {
    ActivationResult result;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return result;
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return result;
    }

    // Parse messages from <chkl:messages>.
    const auto* messages = root->FirstChildElement("chkl:messages");
    if (messages) {
        for (auto* msg = messages->FirstChildElement("msg"); msg;
             msg = msg->NextSiblingElement("msg")) {

            const char* msg_type = msg->Attribute("type");
            std::string type_str = msg_type ? msg_type : "";

            // Extract text from <shortText><txt> child.
            const auto* short_text = msg->FirstChildElement("shortText");
            if (short_text) {
                const auto* txt = short_text->FirstChildElement("txt");
                if (txt && txt->GetText()) {
                    result.error_messages.push_back(txt->GetText());
                }
            }

            ++result.total;
            if (type_str == "E" || type_str == "A") {
                ++result.failed;
            } else {
                ++result.activated;
            }
        }
    }

    // Check for remaining inactive objects (partial failure).
    const auto* inactive = root->FirstChildElement("ioc:inactiveObjects");
    if (inactive && inactive->FirstChildElement("ioc:entry")) {
        for (auto* entry = inactive->FirstChildElement("ioc:entry"); entry;
             entry = entry->NextSiblingElement("ioc:entry")) {
            ++result.failed;
            ++result.total;
        }
    }

    return result;
}

} // namespace

Result<std::vector<InactiveObject>, Error> GetInactiveObjects(
    IAdtSession& session,
    const IXmlCodec& codec) {

    auto response = session.Get(kInactivePath);
    if (response.IsErr()) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            Error::FromHttpStatus("GetInactiveObjects", kInactivePath, http.status_code, http.body));
    }

    return codec.ParseInactiveObjectsResponse(http.body);
}

Result<ActivationResult, Error> ActivateAll(
    IAdtSession& session,
    const IXmlCodec& codec,
    const std::vector<InactiveObject>& objects,
    std::chrono::seconds timeout) {

    // Nothing to activate — return a zero-count success.
    if (objects.empty()) {
        return Result<ActivationResult, Error>::Ok(
            ActivationResult{0, 0, 0, {}});
    }

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildActivationXml(objects);
    if (xml.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(xml).Error());
    }

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kActivationPath, xml.Value(), kActivationContentType, headers);
    if (response.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 + Location → poll until complete.
    if (http.status_code == 202) {
        auto location = adt_utils::RequireHeaderCi(http.headers, "Location",
                                                   "ActivateAll",
                                                   kActivationPath, 202);
        if (location.IsErr()) {
            return Result<ActivationResult, Error>::Err(
                std::move(location).Error());
        }

        auto poll = session.PollUntilComplete(location.Value(), timeout);
        if (poll.IsErr()) {
            return Result<ActivationResult, Error>::Err(std::move(poll).Error());
        }
        if (poll.Value().status == PollStatus::Failed) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateAll", kActivationPath, std::nullopt,
                "async activation operation failed", std::nullopt,
                ErrorCategory::ActivationError});
        }
        if (poll.Value().status == PollStatus::Running) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateAll",
                kActivationPath,
                std::nullopt,
                "async activation operation did not complete within timeout",
                std::nullopt,
                ErrorCategory::Timeout});
        }

        return codec.ParseActivationResponse(poll.Value().body);
    }

    // Synchronous: 200 with activation result.
    if (http.status_code == 200) {
        return codec.ParseActivationResponse(http.body);
    }

    return Result<ActivationResult, Error>::Err(
        Error::FromHttpStatus("ActivateAll", kActivationPath, http.status_code, http.body));
}

Result<ActivationResult, Error> ActivateObject(
    IAdtSession& session,
    const ActivateObjectParams& params,
    std::chrono::seconds timeout) {

    if (params.uri.empty()) {
        return Result<ActivationResult, Error>::Err(Error{
            "ActivateObject", "", std::nullopt,
            "URI is required for activation", std::nullopt});
    }

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(csrf).Error());
    }

    auto xml = BuildActivationRequestXml(params);

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kActivateObjectPath, xml, kActivationContentType, headers);
    if (response.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 + Location -> poll until complete.
    if (http.status_code == 202) {
        auto location = adt_utils::RequireHeaderCi(http.headers, "Location",
                                                   "ActivateObject",
                                                   kActivateObjectPath, 202);
        if (location.IsErr()) {
            return Result<ActivationResult, Error>::Err(
                std::move(location).Error());
        }

        auto poll = session.PollUntilComplete(location.Value(), timeout);
        if (poll.IsErr()) {
            return Result<ActivationResult, Error>::Err(std::move(poll).Error());
        }
        if (poll.Value().status == PollStatus::Failed) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateObject", kActivateObjectPath, std::nullopt,
                "async activation operation failed", std::nullopt,
                ErrorCategory::ActivationError});
        }
        if (poll.Value().status == PollStatus::Running) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateObject",
                kActivateObjectPath,
                std::nullopt,
                "async activation operation did not complete within timeout",
                std::nullopt,
                ErrorCategory::Timeout});
        }

        return Result<ActivationResult, Error>::Ok(
            ParseActivationResultXml(poll.Value().body));
    }

    // Synchronous: 200 with activation result.
    if (http.status_code == 200) {
        return Result<ActivationResult, Error>::Ok(
            ParseActivationResultXml(http.body));
    }

    return Result<ActivationResult, Error>::Err(
        Error::FromHttpStatus("ActivateObject", kActivateObjectPath, http.status_code, http.body));
}

} // namespace erpl_adt
