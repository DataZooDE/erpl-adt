#include <erpl_adt/adt/bw_search.hpp>

#include "atom_parser.hpp"
#include "adt_utils.hpp"
#include "xml_utils.hpp"
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
    if (options.endpoint_override.has_value() &&
        !options.endpoint_override->empty()) {
        return *options.endpoint_override;
    }

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
    AppendParam(url, "infoArea", options.info_area);
    if (options.search_in_description) {
        url += "&searchInDescription=true";
    }
    if (!options.search_in_name) {
        url += "&searchInName=false";
    }
    return url;
}

Result<BwSearchResponse, Error> ParseSearchResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwSearchObjects", kBwSearchPath,
            "Failed to parse BW search response XML")) {
        return Result<BwSearchResponse, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwSearchResponse, Error>::Ok(BwSearchResponse{});
    }

    BwSearchResponse response;
    response.feed_incomplete =
        xml_utils::AttrAny(root, "bwModel:feedIncomplete", "feedIncomplete") == "true";

    // Atom feed: <feed> -> <entry> elements
    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) continue;

        BwSearchResult r;

        r.description = atom_parser::ChildTextByLocalName(entry, "title");
        r.uri = atom_parser::ChildTextByLocalName(entry, "id");

        if (const auto* props = atom_parser::AtomEntryProperties(entry)) {
            r.name = xml_utils::AttrAny(props, "bwModel:objectName", "objectName");
            r.type = xml_utils::AttrAny(props, "bwModel:objectType", "objectType");
            r.subtype = xml_utils::AttrAny(props, "bwModel:objectSubtype", "objectSubtype");
            r.version = xml_utils::AttrAny(props, "bwModel:objectVersion", "objectVersion");
            r.status = xml_utils::AttrAny(props, "bwModel:objectStatus", "objectStatus");
            r.technical_name = xml_utils::AttrAny(props, "bwModel:technicalObjectName", "technicalObjectName");
            r.last_changed = xml_utils::AttrAny(props, "bwModel:lastChangedAt", "lastChangedAt");
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
            response.results.push_back(std::move(r));
        }
    }

    return Result<BwSearchResponse, Error>::Ok(std::move(response));
}

} // anonymous namespace

Result<BwSearchResponse, Error> BwSearchObjectsDetailed(
    IAdtSession& session,
    const BwSearchOptions& options) {
    if (options.query.empty()) {
        return Result<BwSearchResponse, Error>::Err(Error{
            "BwSearchObjects", kBwSearchPath, std::nullopt,
            "Search query must not be empty", std::nullopt});
    }

    auto url = BuildSearchUrl(options);

    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwSearchResponse, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwSearchObjects", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwSearchResponse, Error>::Err(std::move(error));
    }

    return ParseSearchResponse(http.body);
}

Result<std::vector<BwSearchResult>, Error> BwSearchObjects(
    IAdtSession& session,
    const BwSearchOptions& options) {
    auto response = BwSearchObjectsDetailed(session, options);
    if (response.IsErr()) {
        return Result<std::vector<BwSearchResult>, Error>::Err(response.Error());
    }
    return Result<std::vector<BwSearchResult>, Error>::Ok(
        std::move(response.Value().results));
}

} // namespace erpl_adt
