#include <erpl_adt/adt/bw_discovery.hpp>

#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwDiscoveryPath = "/sap/bw/modeling/discovery";

Result<BwDiscoveryResult, Error> ParseDiscoveryResponse(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwDiscoveryResult, Error>::Err(Error{
            "BwDiscover", kBwDiscoveryPath, std::nullopt,
            "Failed to parse BW discovery response XML", std::nullopt});
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwDiscoveryResult, Error>::Err(Error{
            "BwDiscover", kBwDiscoveryPath, std::nullopt,
            "Empty BW discovery response", std::nullopt});
    }

    BwDiscoveryResult result;

    // Atom service document: <service> -> <workspace> -> <collection>
    // Each collection has <categories> with scheme/term and <link> elements.
    for (auto* workspace = root->FirstChildElement(); workspace;
         workspace = workspace->NextSiblingElement()) {
        for (auto* collection = workspace->FirstChildElement(); collection;
             collection = collection->NextSiblingElement()) {
            const char* href = collection->Attribute("href");
            if (!href) continue;

            // Find scheme/term from <categories> child
            std::string scheme;
            std::string term;
            std::string content_type;

            for (auto* child = collection->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                const char* child_name = child->Name();
                if (!child_name) continue;

                // Match category elements (may be prefixed)
                std::string name_str(child_name);
                bool is_category = (name_str == "categories" ||
                                    name_str.find(":categories") != std::string::npos);
                bool is_accept = (name_str == "accept" ||
                                  name_str.find(":accept") != std::string::npos);

                if (is_category) {
                    for (auto* cat = child->FirstChildElement(); cat;
                         cat = cat->NextSiblingElement()) {
                        const char* s = cat->Attribute("scheme");
                        const char* t = cat->Attribute("term");
                        if (s) scheme = s;
                        if (t) term = t;
                    }
                }
                if (is_accept) {
                    const char* text = child->GetText();
                    if (text) content_type = text;
                }
            }

            // Also check for link elements with rel="http://www.sap.com/..."
            for (auto* link = collection->FirstChildElement(); link;
                 link = link->NextSiblingElement()) {
                const char* link_name = link->Name();
                if (!link_name) continue;
                std::string ln(link_name);
                if (ln != "link" && ln.find(":link") == std::string::npos) continue;

                const char* link_href = link->Attribute("href");
                const char* link_rel = link->Attribute("rel");
                const char* link_type = link->Attribute("type");

                BwServiceEntry entry;
                entry.scheme = scheme;
                entry.term = term;
                entry.href = link_href ? link_href : std::string(href);
                entry.content_type = link_type ? link_type : content_type;

                if (!entry.scheme.empty() || link_rel) {
                    if (link_rel && entry.scheme.empty()) {
                        entry.scheme = link_rel;
                    }
                    result.services.push_back(std::move(entry));
                }
            }

            // If no link elements found, add the collection itself
            if (!scheme.empty() && !term.empty()) {
                bool has_link = false;
                for (const auto& s : result.services) {
                    if (s.scheme == scheme && s.term == term) {
                        has_link = true;
                        break;
                    }
                }
                if (!has_link) {
                    BwServiceEntry entry;
                    entry.scheme = scheme;
                    entry.term = term;
                    entry.href = href;
                    entry.content_type = content_type;
                    result.services.push_back(std::move(entry));
                }
            }
        }
    }

    return Result<BwDiscoveryResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

Result<BwDiscoveryResult, Error> BwDiscover(IAdtSession& session) {
    HttpHeaders headers;
    headers["Accept"] = "application/atomsvc+xml";

    auto response = session.Get(kBwDiscoveryPath, headers);
    if (response.IsErr()) {
        return Result<BwDiscoveryResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<BwDiscoveryResult, Error>::Err(
            Error::FromHttpStatus("BwDiscover", kBwDiscoveryPath,
                                  http.status_code, http.body));
    }

    return ParseDiscoveryResponse(http.body);
}

Result<std::string, Error> BwResolveEndpoint(
    const BwDiscoveryResult& discovery,
    const std::string& scheme,
    const std::string& term) {
    for (const auto& entry : discovery.services) {
        if (entry.scheme == scheme && entry.term == term) {
            return Result<std::string, Error>::Ok(std::string(entry.href));
        }
    }
    return Result<std::string, Error>::Err(Error{
        "BwResolveEndpoint", "", std::nullopt,
        "BW service not found: scheme=" + scheme + ", term=" + term,
        std::nullopt, ErrorCategory::NotFound});
}

} // namespace erpl_adt
