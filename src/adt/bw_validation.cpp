#include <erpl_adt/adt/bw_validation.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>

#include <tinyxml2.h>

#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

namespace {

const char* kValidationPath = "/sap/bw/modeling/validation";
const char* kMoveRequestsPath = "/sap/bw/modeling/move_requests";

Result<std::string, Error> FetchAtom(IAdtSession& session,
                                     const std::string& path,
                                     const char* operation) {
    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus(operation, path, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::string, Error>::Err(std::move(error));
    }

    return Result<std::string, Error>::Ok(http.body);
}

std::string AttrOrChild(const tinyxml2::XMLElement* element,
                        const char* first_attr,
                        const char* second_attr,
                        const char* first_child,
                        const char* second_child) {
    auto out = xml_utils::AttrAny(element, first_attr, second_attr);
    if (!out.empty()) {
        return out;
    }
    out = atom_parser::ChildTextByLocalName(element, first_child);
    if (!out.empty()) {
        return out;
    }
    return atom_parser::ChildTextByLocalName(element, second_child);
}

std::string BuildValidationPath(const BwValidationOptions& options) {
    std::string path = kValidationPath;
    path += "?objectType=" + UrlEncode(options.object_type);
    path += "&objectName=" + UrlEncode(options.object_name);
    if (!options.action.empty()) {
        path += "&action=" + UrlEncode(options.action);
    }
    return path;
}

Result<std::vector<BwValidationMessage>, Error> ParseValidation(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwValidateObject", kValidationPath,
            "Failed to parse BW validation XML")) {
        return Result<std::vector<BwValidationMessage>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwValidationMessage> out;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwValidationMessage>, Error>::Ok(std::move(out));
    }

    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) {
            continue;
        }

        BwValidationMessage item;
        item.text = atom_parser::ChildTextByLocalName(entry, "title");

        const auto* props = atom_parser::AtomEntryProperties(entry);
        if (props) {
            item.severity = AttrOrChild(props, "severity", "textype", "severity", "textype");
            item.object_type = AttrOrChild(props, "objectType", "type", "objectType", "type");
            item.object_name = AttrOrChild(props, "objectName", "name", "objectName", "name");
            item.code = AttrOrChild(props, "code", "messageId", "code", "messageId");
            if (item.text.empty()) {
                item.text = AttrOrChild(props, "text", "message", "text", "message");
            }
        }

        if (!item.text.empty() || !item.code.empty()) {
            out.push_back(std::move(item));
        }
    }

    return Result<std::vector<BwValidationMessage>, Error>::Ok(std::move(out));
}

Result<std::vector<BwMoveRequestEntry>, Error> ParseMoveRequests(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwListMoveRequests", kMoveRequestsPath,
            "Failed to parse BW move requests XML")) {
        return Result<std::vector<BwMoveRequestEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwMoveRequestEntry> out;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwMoveRequestEntry>, Error>::Ok(std::move(out));
    }

    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) {
            continue;
        }

        BwMoveRequestEntry item;
        item.description = atom_parser::ChildTextByLocalName(entry, "title");

        const auto* props = atom_parser::AtomEntryProperties(entry);
        if (props) {
            item.request = AttrOrChild(props, "request", "corrNr", "request", "corrNr");
            item.owner = AttrOrChild(props, "owner", "username", "owner", "username");
            item.status = AttrOrChild(props, "status", "state", "status", "state");
            if (item.description.empty()) {
                item.description = AttrOrChild(props, "description", "text", "description", "text");
            }
        }

        if (!item.request.empty() || !item.description.empty()) {
            out.push_back(std::move(item));
        }
    }

    return Result<std::vector<BwMoveRequestEntry>, Error>::Ok(std::move(out));
}

}  // namespace

Result<std::vector<BwValidationMessage>, Error>
BwValidateObject(IAdtSession& session, const BwValidationOptions& options) {
    if (options.object_type.empty()) {
        return Result<std::vector<BwValidationMessage>, Error>::Err(Error{
            "BwValidateObject", kValidationPath, std::nullopt,
            "object_type must not be empty", std::nullopt});
    }
    if (options.object_name.empty()) {
        return Result<std::vector<BwValidationMessage>, Error>::Err(Error{
            "BwValidateObject", kValidationPath, std::nullopt,
            "object_name must not be empty", std::nullopt});
    }

    auto path = BuildValidationPath(options);
    auto xml_result = FetchAtom(session, path, "BwValidateObject");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwValidationMessage>, Error>::Err(
            std::move(xml_result).Error());
    }

    return ParseValidation(xml_result.Value());
}

Result<std::vector<BwMoveRequestEntry>, Error>
BwListMoveRequests(IAdtSession& session) {
    auto xml_result = FetchAtom(session, kMoveRequestsPath, "BwListMoveRequests");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwMoveRequestEntry>, Error>::Err(
            std::move(xml_result).Error());
    }

    return ParseMoveRequests(xml_result.Value());
}

}  // namespace erpl_adt
