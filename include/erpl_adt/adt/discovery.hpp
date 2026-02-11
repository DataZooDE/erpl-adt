#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/core/result.hpp>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Discovery — free functions for the ADT discovery endpoint.
//
// GET /sap/bc/adt/discovery returns an Atom Service Document describing
// available ADT services. These functions fetch and interpret it.
// ---------------------------------------------------------------------------

[[nodiscard]] Result<DiscoveryResult, Error> Discover(
    IAdtSession& session,
    const IXmlCodec& codec);

[[nodiscard]] bool HasAbapGitSupport(const DiscoveryResult& discovery);

} // namespace erpl_adt
