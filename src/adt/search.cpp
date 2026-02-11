#include <erpl_adt/adt/search.hpp>

#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kSearchPath = "/sap/bc/adt/repository/informationsystem/search";

std::string BuildSearchUrl(const SearchOptions& options) {
    std::string url = std::string(kSearchPath) +
        "?operation=quickSearch&query=" + options.query +
        "&maxResults=" + std::to_string(options.max_results);
    if (options.object_type.has_value()) {
        url += "&objectType=" + *options.object_type;
    }
    return url;
}

Result<std::vector<SearchResult>, Error> ParseSearchResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<SearchResult>, Error>::Err(Error{
            "SearchObjects", kSearchPath, std::nullopt,
            "Failed to parse search response XML", std::nullopt});
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

        // Try both namespaced and plain attributes.
        auto get_attr = [&](const char* ns_name, const char* plain_name) -> std::string {
            const char* val = el->Attribute(ns_name);
            if (!val) val = el->Attribute(plain_name);
            return val ? val : "";
        };

        r.uri = get_attr("adtcore:uri", "uri");
        r.type = get_attr("adtcore:type", "type");
        r.name = get_attr("adtcore:name", "name");
        r.description = get_attr("adtcore:description", "description");
        r.package_name = get_attr("adtcore:packageName", "packageName");

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
