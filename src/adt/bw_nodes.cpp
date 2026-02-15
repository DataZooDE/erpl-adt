#include <erpl_adt/adt/bw_nodes.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kInfoProviderPath = "/sap/bw/modeling/repo/infoproviderstructure";
const char* kDataSourcePath   = "/sap/bw/modeling/repo/datasourcestructure";

std::string BuildNodesUrl(const BwNodesOptions& options) {
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

// Get attribute trying both namespaced and plain names.
std::string GetAttr(const tinyxml2::XMLElement* el,
                    const char* ns_name, const char* plain_name) {
    const char* val = el->Attribute(ns_name);
    if (!val) val = el->Attribute(plain_name);
    return val ? val : "";
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
        const char* entry_name = entry->Name();
        if (!entry_name) continue;

        std::string name_str(entry_name);
        bool is_entry = (name_str == "entry" ||
                         name_str.find(":entry") != std::string::npos);
        if (!is_entry) continue;

        BwNodeEntry r;

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
                    r.name = GetAttr(props, "bwModel:objectName", "objectName");
                    r.type = GetAttr(props, "bwModel:objectType", "objectType");
                    r.subtype = GetAttr(props, "bwModel:objectSubtype", "objectSubtype");
                    r.version = GetAttr(props, "bwModel:objectVersion", "objectVersion");
                    r.status = GetAttr(props, "bwModel:objectStatus", "objectStatus");
                    if (r.description.empty()) {
                        r.description = GetAttr(props, "bwModel:objectDesc", "objectDesc");
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
