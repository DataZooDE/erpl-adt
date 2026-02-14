#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwServiceEntry — a single service from the BW discovery document.
// ---------------------------------------------------------------------------
struct BwServiceEntry {
    std::string scheme;
    std::string term;
    std::string href;          // Base URI or template
    std::string content_type;  // Supported media type
};

// ---------------------------------------------------------------------------
// BwDiscoveryResult — parsed BW discovery document.
// ---------------------------------------------------------------------------
struct BwDiscoveryResult {
    std::vector<BwServiceEntry> services;
};

// ---------------------------------------------------------------------------
// BwDiscover — fetch and parse the BW Modeling discovery document.
//
// Endpoint: GET /sap/bw/modeling/discovery
// Accept:   application/atomsvc+xml
//
// Returns available BW modeling services with scheme/term pairs and URIs.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwDiscoveryResult, Error> BwDiscover(
    IAdtSession& session);

// ---------------------------------------------------------------------------
// BwResolveEndpoint — find a service URI by scheme and term.
//
// Searches the discovery result for a matching (scheme, term) pair.
// Returns the href (URI template) or an error if not found.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> BwResolveEndpoint(
    const BwDiscoveryResult& discovery,
    const std::string& scheme,
    const std::string& term);

} // namespace erpl_adt
