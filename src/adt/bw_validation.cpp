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

// The validation resource implements post() only — a GET answers HTTP 405
// "Resource controller does not support method GET". The parameters stay in
// the query string; the body is empty.
Result<std::string, Error> PostAtom(IAdtSession& session,
                                    const std::string& path,
                                    const char* operation) {
    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Post(path, "", "application/xml", headers);
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
    // A clean validation answers 200 with an empty body: nothing to report is
    // the good outcome, not a malformed response.
    if (xml.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return Result<std::vector<BwValidationMessage>, Error>::Ok({});
    }

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
    auto xml_result = PostAtom(session, path, "BwValidateObject");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwValidationMessage>, Error>::Err(
            std::move(xml_result).Error());
    }

    return ParseValidation(xml_result.Value());
}

Result<std::vector<BwMoveRequestEntry>, Error>
BwListMoveRequests(IAdtSession& session) {
    // CL_RSO_RES_MOVE_REQUESTS implements post() only: the endpoint *executes*
    // a move (an Atom feed naming the object and its new parent) and never
    // listed anything. A GET here answered HTTP 405 "Resource controller does
    // not support method GET" on every system — so say what the endpoint is
    // instead of forwarding a protocol error the caller cannot act on.
    (void)session;
    Error error{"BwListMoveRequests", kMoveRequestsPath, std::nullopt,
                "The BW move endpoint has no listing: it only executes moves",
                std::nullopt, ErrorCategory::NotFound};
    error.hint =
        "/sap/bw/modeling/move_requests accepts POST only. To see where an "
        "object sits, use 'bw nodes' or 'bw nodepath'.";
    return Result<std::vector<BwMoveRequestEntry>, Error>::Err(std::move(error));
}

}  // namespace erpl_adt
