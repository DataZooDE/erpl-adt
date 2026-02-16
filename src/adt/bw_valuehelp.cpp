#include <erpl_adt/adt/bw_valuehelp.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>

#include <tinyxml2.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

namespace {

const char* kValueHelpBase = "/sap/bw/modeling/is/values/";
const char* kVirtualFoldersPath = "/sap/bw/modeling/repo/is/virtualfolders";
const char* kDataVolumesPath = "/sap/bw/modeling/repo/is/datavolumes";

std::string BuildValueHelpUrl(const BwValueHelpOptions& options) {
    std::string url = std::string(kValueHelpBase) + options.domain;
    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value() || value->empty()) {
            return;
        }
        url += (has_query ? "&" : "?");
        url += key;
        url += "=";
        url += UrlEncode(*value);
        has_query = true;
    };

    if (options.raw_query.has_value() && !options.raw_query->empty()) {
        url += "?" + *options.raw_query;
        has_query = true;
    }
    if (options.max_rows.has_value()) {
        add("maxrows", std::optional<std::string>{std::to_string(*options.max_rows)});
    }
    add("pattern", options.pattern);
    add("objectType", options.object_type);
    add("infoprovider", options.infoprovider);
    return url;
}

Result<std::string, Error> Fetch(IAdtSession& session,
                                 const std::string& path,
                                 const char* operation) {
    HttpHeaders headers;
    headers["Accept"] = "application/xml, application/atom+xml, */*";

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

bool HasElementChildren(const tinyxml2::XMLElement* element) {
    return element && element->FirstChildElement() != nullptr;
}

std::vector<BwValueHelpRow> ParseGenericRows(std::string_view xml,
                                             const char* operation,
                                             const std::string& endpoint) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, operation, endpoint,
            "Failed to parse BW XML response")) {
        return {};
    }

    std::vector<BwValueHelpRow> out;

    std::function<void(const tinyxml2::XMLElement*)> visit =
        [&](const tinyxml2::XMLElement* element) {
            if (!element) {
                return;
            }

            BwValueHelpRow row;
            row.fields["_element"] = std::string(atom_parser::LocalName(element->Name()));

            for (const auto* attr = element->FirstAttribute(); attr;
                 attr = attr->Next()) {
                row.fields[attr->Name()] = attr->Value() ? attr->Value() : "";
            }

            if (element->GetText() != nullptr) {
                row.fields["_text"] = element->GetText();
            }

            if (row.fields.size() > 1 && (!HasElementChildren(element) || element->GetText() != nullptr)) {
                out.push_back(std::move(row));
            }

            for (auto* child = element->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                visit(child);
            }
        };

    const auto* root = doc.RootElement();
    if (root) {
        visit(root);
    }
    return out;
}

}  // namespace

Result<std::vector<BwValueHelpRow>, Error>
BwGetValueHelp(IAdtSession& session, const BwValueHelpOptions& options) {
    if (options.domain.empty()) {
        return Result<std::vector<BwValueHelpRow>, Error>::Err(Error{
            "BwGetValueHelp", kValueHelpBase, std::nullopt,
            "domain must not be empty", std::nullopt});
    }

    auto path = BuildValueHelpUrl(options);
    auto response = Fetch(session, path, "BwGetValueHelp");
    if (response.IsErr()) {
        return Result<std::vector<BwValueHelpRow>, Error>::Err(
            std::move(response).Error());
    }

    return Result<std::vector<BwValueHelpRow>, Error>::Ok(
        ParseGenericRows(response.Value(), "BwGetValueHelp", path));
}

Result<std::vector<BwValueHelpRow>, Error>
BwGetVirtualFolders(IAdtSession& session,
                    const std::optional<std::string>& package_name,
                    const std::optional<std::string>& object_type,
                    const std::optional<std::string>& user_name) {
    std::string path = kVirtualFoldersPath;
    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value() || value->empty()) {
            return;
        }
        path += (has_query ? "&" : "?");
        path += key;
        path += "=";
        path += UrlEncode(*value);
        has_query = true;
    };
    add("package", package_name);
    add("objecttype", object_type);
    add("user", user_name);

    auto response = Fetch(session, path, "BwGetVirtualFolders");
    if (response.IsErr()) {
        return Result<std::vector<BwValueHelpRow>, Error>::Err(
            std::move(response).Error());
    }

    return Result<std::vector<BwValueHelpRow>, Error>::Ok(
        ParseGenericRows(response.Value(), "BwGetVirtualFolders", path));
}

Result<std::vector<BwValueHelpRow>, Error>
BwGetDataVolumes(IAdtSession& session,
                 const std::optional<std::string>& infoprovider,
                 const std::optional<int>& max_rows) {
    std::string path = kDataVolumesPath;
    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value() || value->empty()) {
            return;
        }
        path += (has_query ? "&" : "?");
        path += key;
        path += "=";
        path += UrlEncode(*value);
        has_query = true;
    };
    add("infoprovider", infoprovider);
    if (max_rows.has_value()) {
        add("maxrows", std::optional<std::string>{std::to_string(*max_rows)});
    }

    auto response = Fetch(session, path, "BwGetDataVolumes");
    if (response.IsErr()) {
        return Result<std::vector<BwValueHelpRow>, Error>::Err(
            std::move(response).Error());
    }

    return Result<std::vector<BwValueHelpRow>, Error>::Ok(
        ParseGenericRows(response.Value(), "BwGetDataVolumes", path));
}

}  // namespace erpl_adt
