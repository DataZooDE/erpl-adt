#include <erpl_adt/adt/bw_nodes.hpp>

#include "atom_parser.hpp"
#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kInfoProviderPath = "/sap/bw/modeling/repo/infoproviderstructure";
const char* kDataSourcePath   = "/sap/bw/modeling/repo/datasourcestructure";

std::string BuildNodesUrl(const BwNodesOptions& options) {
    if (options.endpoint_override.has_value() &&
        !options.endpoint_override->empty()) {
        return *options.endpoint_override;
    }

    const char* base = options.datasource ? kDataSourcePath : kInfoProviderPath;
    std::string url = std::string(base) + "/" +
        UrlEncode(options.object_type) + "/" +
        UrlEncode(options.object_name);

    bool has_params = false;
    auto add_param = [&](const char* key, const std::string& value) {
        url += (has_params ? "&" : "?");
        url += key;
        url += "=";
        url += UrlEncode(value);
        has_params = true;
    };

    if (options.child_name.has_value()) {
        add_param("childName", *options.child_name);
    }
    if (options.child_type.has_value()) {
        add_param("childType", *options.child_type);
    }
    return url;
}

Result<std::vector<BwNodeEntry>, Error> ParseNodesResponse(
    std::string_view xml, const char* operation) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, operation, "",
            "Failed to parse BW nodes response XML")) {
        return Result<std::vector<BwNodeEntry>, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwNodeEntry>, Error>::Ok(
            std::vector<BwNodeEntry>{});
    }

    std::vector<BwNodeEntry> results;

    // Atom feed: <feed> -> <entry> elements
    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) continue;

        BwNodeEntry r;

        r.description = atom_parser::ChildTextByLocalName(entry, "title");
        r.uri = atom_parser::ChildTextByLocalName(entry, "id");

        if (const auto* props = atom_parser::AtomEntryProperties(entry)) {
            r.name = xml_utils::AttrAny(props, "bwModel:objectName", "objectName");
            r.type = xml_utils::AttrAny(props, "bwModel:objectType", "objectType");
            r.subtype = xml_utils::AttrAny(props, "bwModel:objectSubtype", "objectSubtype");
            r.version = xml_utils::AttrAny(props, "bwModel:objectVersion", "objectVersion");
            r.status = xml_utils::AttrAny(props, "bwModel:objectStatus", "objectStatus");
            if (r.description.empty()) {
                r.description = xml_utils::AttrAny(props, "bwModel:objectDesc", "objectDesc");
            }
        }

        for (auto* child = entry->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            if (!atom_parser::HasLocalName(child, "link")) {
                continue;
            }
            const char* rel = child->Attribute("rel");
            if (rel && std::string(rel) == "self") {
                const char* href = child->Attribute("href");
                if (href && r.uri.empty()) {
                    r.uri = href;
                }
            }
        }

        if (!r.name.empty()) {
            results.push_back(std::move(r));
        }
    }

    return Result<std::vector<BwNodeEntry>, Error>::Ok(std::move(results));
}

} // anonymous namespace

Result<std::vector<BwNodeEntry>, Error> BwGetNodes(
    IAdtSession& session,
    const BwNodesOptions& options) {
    if (options.object_type.empty()) {
        return Result<std::vector<BwNodeEntry>, Error>::Err(Error{
            "BwGetNodes", "", std::nullopt,
            "Object type must not be empty", std::nullopt});
    }
    if (options.object_name.empty()) {
        return Result<std::vector<BwNodeEntry>, Error>::Err(Error{
            "BwGetNodes", "", std::nullopt,
            "Object name must not be empty", std::nullopt});
    }

    auto url = BuildNodesUrl(options);

    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwNodeEntry>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetNodes", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwNodeEntry>, Error>::Err(std::move(error));
    }

    return ParseNodesResponse(http.body, "BwGetNodes");
}

} // namespace erpl_adt
