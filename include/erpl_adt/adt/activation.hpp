#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/core/result.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Activation — free functions for ADT activation operations.
//
// GET  /sap/bc/adt/activation/inactive  — enumerate inactive objects
// POST /sap/bc/adt/activation           — submit mass activation (async)
// POST /sap/bc/adt/activation?method=activate&preauditRequested=true
//                                       — activate a single object
// ---------------------------------------------------------------------------

[[nodiscard]] Result<std::vector<InactiveObject>, Error> GetInactiveObjects(
    IAdtSession& session,
    const IXmlCodec& codec);

[[nodiscard]] Result<ActivationResult, Error> ActivateAll(
    IAdtSession& session,
    const IXmlCodec& codec,
    const std::vector<InactiveObject>& objects,
    std::chrono::seconds timeout = std::chrono::seconds{600});

// ---------------------------------------------------------------------------
// ActivateObject — activate a single ABAP object by URI.
//
// New-style module: parses XML directly with tinyxml2 (no IXmlCodec).
// ---------------------------------------------------------------------------

struct ActivateObjectParams {
    std::string uri;        // e.g. "/sap/bc/adt/oo/classes/ZCL_MY_CLASS"
    std::string type;       // e.g. "CLAS/OC" (optional, improves activation)
    std::string name;       // e.g. "ZCL_MY_CLASS" (optional)
    std::string parent_uri; // e.g. "/sap/bc/adt/packages/ZLOCAL" (optional)
};

[[nodiscard]] Result<ActivationResult, Error> ActivateObject(
    IAdtSession& session,
    const ActivateObjectParams& params,
    std::chrono::seconds timeout = std::chrono::seconds{600});

// ActivationParamsFromObjectUri — the activation reference for a well-known
// ADT object URI (type, upper-case name, parent), without an HTTP round trip.
// Parts of a function group get their own type: ".../groups/zfg/fmodules/zfm"
// is FUGR/FF ZFM and ".../groups/zfg/includes/lzfgtop" FUGR/I LZFGTOP, both
// with the group as parent_uri. "%2f" in names is decoded ("/DMO/FM"). A URI
// that matches no known pattern yields params with only `uri` set.
[[nodiscard]] ActivateObjectParams ActivationParamsFromObjectUri(const std::string& object_uri);

// ActivateObjects — activate several objects in one request (one
// adtcore:objectReference each), so that objects which depend on each other
// (a function group include and the function modules using it) are checked
// and activated together, as Eclipse does for "Activate all".
[[nodiscard]] Result<ActivationResult, Error> ActivateObjects(
    IAdtSession& session,
    const std::vector<ActivateObjectParams>& objects,
    std::chrono::seconds timeout = std::chrono::seconds{600});

} // namespace erpl_adt
