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
    std::string rel;           // Link relation ("self", "latest-version", ...)
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

// ---------------------------------------------------------------------------
// BwResolveEndpointByRel — find a service URI by scheme, term and relation.
//
// A type advertises several templates; the first one is rel="self", which for
// object types carries no {version} segment.  Selecting by relation is how a
// versioned read finds "/sap/bw/modeling/adso/{adsonm}/{version}"
// (rel="latest-version").  Falls back to the first (scheme, term) match when
// the requested relation is absent, so behaviour never regresses to an error
// on systems that advertise fewer relations.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> BwResolveEndpointByRel(
    const BwDiscoveryResult& discovery,
    const std::string& scheme,
    const std::string& term,
    const std::string& rel);

// ---------------------------------------------------------------------------
// BwResolveContentType — find the Accept content type for a given tlogo.
//
// Searches the discovery result for a service whose term matches the tlogo
// (case-insensitive) and returns its content_type.  Returns empty string if
// no match is found.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string BwResolveContentType(
    const BwDiscoveryResult& discovery,
    const std::string& tlogo);

} // namespace erpl_adt
