#include <erpl_adt/adt/bw_search.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwSearchPath = "/sap/bw/modeling/repo/is/bwsearch";

void AppendParam(std::string& url, const char* name,
                  const std::optional<std::string>& value) {
    if (value.has_value()) {
        url += "&";
        url += name;
        url += "=";
        url += UrlEncode(*value);
    }
}

std::string BuildSearchUrl(const BwSearchOptions& options) {
    std::string url = std::string(kBwSearchPath) +
        "?searchTerm=" + UrlEncode(options.query) +
        "&maxSize=" + std::to_string(options.max_results);
    AppendParam(url, "objectType", options.object_type);
    AppendParam(url, "objectSubType", options.object_sub_type);
    AppendParam(url, "objectStatus", options.object_status);
    AppendParam(url, "objectVersion", options.object_version);
    AppendParam(url, "changedBy", options.changed_by);
    AppendParam(url, "changedOnFrom", options.changed_on_from);
    AppendParam(url, "changedOnTo", options.changed_on_to);
    AppendParam(url, "createdBy", options.created_by);
    AppendParam(url, "createdOnFrom", options.created_on_from);
    AppendParam(url, "createdOnTo", options.created_on_to);
    AppendParam(url, "dependsOnObjectName", options.depends_on_name);
    AppendParam(url, "dependsOnObjectType", options.depends_on_type);
    if (options.search_in_description) {
        url += "&searchInDescription=true";
    }
    if (!options.search_in_name) {
        url += "&searchInName=false";
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

Result<std::vector<BwSearchResult>, Error> ParseSearchResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwSearchObjects", kBwSearchPath,
            "Failed to parse BW search response XML")) {
        return Result<std::vector<BwSearchResult>, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwSearchResult>, Error>::Ok(
            std::vector<BwSearchResult>{});
    }

    std::vector<BwSearchResult> results;

    // Atom feed: <feed> -> <entry> elements
    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        const char* entry_name = entry->Name();
        if (!entry_name) continue;

        std::string name_str(entry_name);
        bool is_entry = (name_str == "entry" ||
                         name_str.find(":entry") != std::string::npos);
        if (!is_entry) continue;

        BwSearchResult r;

        // Get title (description)
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
                // Content element has the attributes
                auto* props = child->FirstChildElement();
                if (props) {
                    r.name = GetAttr(props, "bwModel:objectName", "objectName");
                    r.type = GetAttr(props, "bwModel:objectType", "objectType");
                    r.subtype = GetAttr(props, "bwModel:objectSubtype", "objectSubtype");
                    r.version = GetAttr(props, "bwModel:objectVersion", "objectVersion");
                    r.status = GetAttr(props, "bwModel:objectStatus", "objectStatus");
                    r.technical_name = GetAttr(props, "bwModel:technicalObjectName", "technicalObjectName");
                    r.last_changed = GetAttr(props, "bwModel:lastChangedAt", "lastChangedAt");
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

    return Result<std::vector<BwSearchResult>, Error>::Ok(std::move(results));
}

} // anonymous namespace

Result<std::vector<BwSearchResult>, Error> BwSearchObjects(
    IAdtSession& session,
    const BwSearchOptions& options) {
    if (options.query.empty()) {
        return Result<std::vector<BwSearchResult>, Error>::Err(Error{
            "BwSearchObjects", kBwSearchPath, std::nullopt,
            "Search query must not be empty", std::nullopt});
    }

    auto url = BuildSearchUrl(options);

    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwSearchResult>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwSearchObjects", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwSearchResult>, Error>::Err(std::move(error));
    }

    return ParseSearchResponse(http.body);
}

} // namespace erpl_adt
