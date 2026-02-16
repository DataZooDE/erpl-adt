#include <erpl_adt/adt/search.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kSearchPath = "/sap/bc/adt/repository/informationsystem/search";

std::string BuildSearchUrl(const SearchOptions& options) {
    std::string url = std::string(kSearchPath) +
        "?operation=quickSearch&query=" + UrlEncode(options.query) +
        "&maxResults=" + std::to_string(options.max_results);
    if (options.object_type.has_value()) {
        url += "&objectType=" + UrlEncode(*options.object_type);
    }
    return url;
}

Result<std::vector<SearchResult>, Error> ParseSearchResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "SearchObjects", kSearchPath,
            "Failed to parse search response XML")) {
        return Result<std::vector<SearchResult>, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<SearchResult>, Error>::Err(Error{
            "SearchObjects", kSearchPath, std::nullopt,
            "Empty search response", std::nullopt});
    }

    std::vector<SearchResult> results;

    // Iterate over all child elements named "objectReference" (with namespace prefix).
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        SearchResult r;

        r.uri = xml_utils::AttrAny(el, "adtcore:uri", "uri");
        r.type = xml_utils::AttrAny(el, "adtcore:type", "type");
        r.name = xml_utils::AttrAny(el, "adtcore:name", "name");
        r.description = xml_utils::AttrAny(el, "adtcore:description", "description");
        r.package_name = xml_utils::AttrAny(el, "adtcore:packageName", "packageName");

        if (!r.name.empty()) {
            results.push_back(std::move(r));
        }
    }

    return Result<std::vector<SearchResult>, Error>::Ok(std::move(results));
}

} // anonymous namespace

Result<std::vector<SearchResult>, Error> SearchObjects(
    IAdtSession& session,
    const SearchOptions& options) {
    if (options.query.empty()) {
        return Result<std::vector<SearchResult>, Error>::Err(Error{
            "SearchObjects", kSearchPath, std::nullopt,
            "Search query must not be empty", std::nullopt});
    }

    auto url = BuildSearchUrl(options);
    auto response = session.Get(url);
    if (response.IsErr()) {
        return Result<std::vector<SearchResult>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<SearchResult>, Error>::Err(
            Error::FromHttpStatus("SearchObjects", url, http.status_code, http.body));
    }

    return ParseSearchResponse(http.body);
}

} // namespace erpl_adt
