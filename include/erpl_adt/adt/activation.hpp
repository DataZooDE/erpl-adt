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
    std::string uri;   // e.g. "/sap/bc/adt/oo/classes/ZCL_MY_CLASS"
    std::string type;  // e.g. "CLAS/OC" (optional, improves activation)
    std::string name;  // e.g. "ZCL_MY_CLASS" (optional)
};

[[nodiscard]] Result<ActivationResult, Error> ActivateObject(
    IAdtSession& session,
    const ActivateObjectParams& params,
    std::chrono::seconds timeout = std::chrono::seconds{600});

} // namespace erpl_adt
