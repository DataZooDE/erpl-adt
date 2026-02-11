#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/core/result.hpp>

#include <chrono>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Activation — free functions for ADT mass activation operations.
//
// GET  /sap/bc/adt/activation/inactive  — enumerate inactive objects
// POST /sap/bc/adt/activation           — submit mass activation (async)
// ---------------------------------------------------------------------------

[[nodiscard]] Result<std::vector<InactiveObject>, Error> GetInactiveObjects(
    IAdtSession& session,
    const IXmlCodec& codec);

[[nodiscard]] Result<ActivationResult, Error> ActivateAll(
    IAdtSession& session,
    const IXmlCodec& codec,
    const std::vector<InactiveObject>& objects,
    std::chrono::seconds timeout = std::chrono::seconds{600});

} // namespace erpl_adt
