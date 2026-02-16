#include <erpl_adt/adt/bw_xref.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwXrefPath = "/sap/bw/modeling/repo/is/xref";

std::string BuildXrefUrl(const BwXrefOptions& options) {
    if (options.endpoint_override.has_value() &&
        !options.endpoint_override->empty()) {
        return *options.endpoint_override;
    }

    std::string url = std::string(kBwXrefPath) +
        "?objectType=" + UrlEncode(options.object_type) +
        "&objectName=" + UrlEncode(options.object_name);
    if (options.object_version.has_value()) {
        url += "&objectVersion=" + UrlEncode(*options.object_version);
    }
    if (options.association.has_value()) {
        url += "&association=" + UrlEncode(*options.association);
    }
    if (options.associated_object_type.has_value()) {
        url += "&associatedObjectType=" + UrlEncode(*options.associated_object_type);
    }
    if (options.max_results > 0) {
        url += "&$top=" + std::to_string(options.max_results);
    }
    return url;
}

std::string AssociationLabel(const std::string& code) {
    if (code == "001") return "Used by";
    if (code == "002") return "Uses";
    if (code == "003") return "Depends on";
    if (code == "004") return "Required by";
    if (code == "005") return "Part of";
    if (code == "006") return "Contains";
    return code;
}

Result<std::vector<BwXrefEntry>, Error> ParseXrefResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetXrefs", kBwXrefPath,
            "Failed to parse BW xref response XML")) {
        return Result<std::vector<BwXrefEntry>, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwXrefEntry>, Error>::Ok(
            std::vector<BwXrefEntry>{});
    }

    std::vector<BwXrefEntry> results;

    // Atom feed: <feed> -> <entry> elements
    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        const char* entry_name = entry->Name();
        if (!entry_name) continue;

        std::string name_str(entry_name);
        bool is_entry = (name_str == "entry" ||
                         name_str.find(":entry") != std::string::npos);
        if (!is_entry) continue;

        BwXrefEntry r;

        for (auto* child = entry->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const char* child_name = child->Name();
            if (!child_name) continue;
            std::string cn(child_name);

            if (cn == "title" || cn.find(":title") != std::string::npos) {
                if (child->GetText()) r.description = child->GetText();
            } else if (cn == "id" || cn.find(":id") != std::string::npos) {
                if (child->GetText()) r.uri = child->GetText();
            } else if (cn == "content" || cn.find(":content") != std::string::npos) {
                auto* props = child->FirstChildElement();
                if (props) {
                    r.name = xml_utils::AttrAny(props, "bwModel:objectName", "objectName");
                    r.type = xml_utils::AttrAny(props, "bwModel:objectType", "objectType");
                    r.version = xml_utils::AttrAny(props, "bwModel:objectVersion", "objectVersion");
                    r.status = xml_utils::AttrAny(props, "bwModel:objectStatus", "objectStatus");
                    r.association_type = xml_utils::AttrAny(props, "bwModel:associationType", "associationType");
                    if (r.description.empty()) {
                        r.description = xml_utils::AttrAny(props, "bwModel:objectDesc", "objectDesc");
                    }
                }
            } else if (cn == "link" || cn.find(":link") != std::string::npos) {
                const char* rel = child->Attribute("rel");
                if (rel && std::string(rel) == "self") {
                    const char* href = child->Attribute("href");
                    if (href && r.uri.empty()) r.uri = href;
                }
            }
        }

        if (!r.name.empty()) {
            r.association_label = AssociationLabel(r.association_type);
            results.push_back(std::move(r));
        }
    }

    return Result<std::vector<BwXrefEntry>, Error>::Ok(std::move(results));
}

} // anonymous namespace

Result<std::vector<BwXrefEntry>, Error> BwGetXrefs(
    IAdtSession& session,
    const BwXrefOptions& options) {
    if (options.object_type.empty()) {
        return Result<std::vector<BwXrefEntry>, Error>::Err(Error{
            "BwGetXrefs", kBwXrefPath, std::nullopt,
            "Object type must not be empty", std::nullopt});
    }
    if (options.object_name.empty()) {
        return Result<std::vector<BwXrefEntry>, Error>::Err(Error{
            "BwGetXrefs", kBwXrefPath, std::nullopt,
            "Object name must not be empty", std::nullopt});
    }

    auto url = BuildXrefUrl(options);

    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwXrefEntry>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetXrefs", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwXrefEntry>, Error>::Err(std::move(error));
    }

    return ParseXrefResponse(http.body);
}

} // namespace erpl_adt
