#include <erpl_adt/adt/bw_discovery.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwDiscoveryPath = "/sap/bw/modeling/discovery";

// Check whether an element name (possibly namespace-prefixed) ends with a
// given local name.  E.g. NameEndsWith("app:collection", "collection") == true.
bool NameEndsWith(const char* raw, const char* suffix) {
    if (!raw || !suffix) return false;
    std::string name(raw);
    if (name == suffix) return true;
    // Match ":suffix" at the end (namespace-prefixed form)
    std::string colon_suffix = std::string(":") + suffix;
    return name.size() >= colon_suffix.size() &&
           name.compare(name.size() - colon_suffix.size(),
                        colon_suffix.size(), colon_suffix) == 0;
}

Result<BwDiscoveryResult, Error> ParseDiscoveryResponse(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwDiscover", kBwDiscoveryPath,
            "Failed to parse BW discovery response XML")) {
        return Result<BwDiscoveryResult, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwDiscoveryResult, Error>::Err(Error{
            "BwDiscover", kBwDiscoveryPath, std::nullopt,
            "Empty BW discovery response", std::nullopt});
    }

    BwDiscoveryResult result;

    // Atom service document has two real-world flavours:
    //
    // 1. Test / simplified:
    //    <service xmlns="http://www.w3.org/2007/app">
    //      <workspace> <collection href="...">
    //        <categories> <atom:category scheme="..." term="..."/> </categories>
    //        <link rel="..." href="..." type="..."/>
    //      </collection> </workspace>
    //    </service>
    //
    // 2. Real SAP BW/4HANA:
    //    <app:service xmlns:app="..." xmlns:atom="...">
    //      <app:workspace> <app:collection href="...">
    //        <atom:category scheme="..." term="..."/>
    //        <app:accept>media-type</app:accept>
    //        <adtcomp:templateLinks>
    //          <adtcomp:templateLink rel="..." template="..." type="..."/>
    //        </adtcomp:templateLinks>
    //      </app:collection> </app:workspace>
    //    </app:service>
    //
    // We handle both by matching on local names regardless of prefix.

    for (auto* workspace = root->FirstChildElement(); workspace;
         workspace = workspace->NextSiblingElement()) {
        for (auto* collection = workspace->FirstChildElement(); collection;
             collection = collection->NextSiblingElement()) {
            const char* href = collection->Attribute("href");
            if (!href) continue;

            std::string scheme;
            std::string term;
            std::string content_type;

            for (auto* child = collection->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                const char* child_name = child->Name();
                if (!child_name) continue;

                // Pattern 1: <categories> wrapper around <atom:category .../>
                if (NameEndsWith(child_name, "categories")) {
                    for (auto* cat = child->FirstChildElement(); cat;
                         cat = cat->NextSiblingElement()) {
                        const char* s = cat->Attribute("scheme");
                        const char* t = cat->Attribute("term");
                        if (s) scheme = s;
                        if (t) term = t;
                    }
                }

                // Pattern 2: <atom:category scheme="..." term="..."/>
                //             directly on the collection (no wrapper)
                if (NameEndsWith(child_name, "category")) {
                    const char* s = child->Attribute("scheme");
                    const char* t = child->Attribute("term");
                    if (s) scheme = s;
                    if (t) term = t;
                }

                // <accept> or <app:accept>
                if (NameEndsWith(child_name, "accept")) {
                    const char* text = child->GetText();
                    if (text) content_type = text;
                }
            }

            // Collect links from <link> elements (test fixture) and from
            // <adtcomp:templateLinks> -> <adtcomp:templateLink> (real SAP).
            bool found_link = false;

            for (auto* child = collection->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                const char* child_name = child->Name();
                if (!child_name) continue;

                // Direct <link> elements (test fixture format)
                if (NameEndsWith(child_name, "link") &&
                    !NameEndsWith(child_name, "templateLink") &&
                    !NameEndsWith(child_name, "templateLinks")) {
                    const char* link_href = child->Attribute("href");
                    const char* link_rel = child->Attribute("rel");
                    const char* link_type = child->Attribute("type");

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
                        found_link = true;
                    }
                }

                // <adtcomp:templateLinks> wrapper (real SAP format)
                if (NameEndsWith(child_name, "templateLinks")) {
                    for (auto* tl = child->FirstChildElement(); tl;
                         tl = tl->NextSiblingElement()) {
                        if (!NameEndsWith(tl->Name(), "templateLink")) continue;

                        // Real SAP uses "template" attr; fall back to "href"
                        const char* tl_href = tl->Attribute("template");
                        if (!tl_href) tl_href = tl->Attribute("href");
                        const char* tl_rel = tl->Attribute("rel");
                        const char* tl_type = tl->Attribute("type");

                        BwServiceEntry entry;
                        entry.scheme = scheme;
                        entry.term = term;
                        entry.href = tl_href ? tl_href : std::string(href);
                        entry.content_type = tl_type ? tl_type : content_type;

                        if (!entry.scheme.empty() || tl_rel) {
                            if (tl_rel && entry.scheme.empty()) {
                                entry.scheme = tl_rel;
                            }
                            result.services.push_back(std::move(entry));
                            found_link = true;
                        }
                    }
                }
            }

            // Add collection-level entry when:
            // - No links found (fallback, as before), OR
            // - <app:accept> provides a content type (preserves versioned
            //   Accept type for BwResolveContentType even when templateLinks
            //   provide their own type attributes)
            if (!scheme.empty() && !term.empty() &&
                (!found_link || !content_type.empty())) {
                BwServiceEntry entry;
                entry.scheme = scheme;
                entry.term = term;
                entry.href = href;
                entry.content_type = content_type;
                result.services.push_back(std::move(entry));
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
        auto error = Error::FromHttpStatus("BwDiscover", kBwDiscoveryPath,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwDiscoveryResult, Error>::Err(std::move(error));
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

std::string BwResolveContentType(
    const BwDiscoveryResult& discovery,
    const std::string& tlogo) {
    // Lowercase the tlogo for comparison against discovery terms.
    std::string lower_tlogo = tlogo;
    std::transform(lower_tlogo.begin(), lower_tlogo.end(), lower_tlogo.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Two-pass search: first prefer versioned content types (from <app:accept>),
    // then fall back to any non-empty content type.  Versioned types contain
    // "-v" followed by a version number (e.g. "-v2_1_0+xml").
    std::string fallback;
    for (const auto& entry : discovery.services) {
        std::string lower_term = entry.term;
        std::transform(lower_term.begin(), lower_term.end(), lower_term.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_term != lower_tlogo || entry.content_type.empty()) {
            continue;
        }
        if (entry.content_type.find("-v") != std::string::npos) {
            return entry.content_type;
        }
        if (fallback.empty()) {
            fallback = entry.content_type;
        }
    }
    return fallback;
}

} // namespace erpl_adt
