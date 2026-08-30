#pragma once

#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BW Modeling media types.
//
// Every BW modeling route content-negotiates strictly: a request that does not
// name the type's media type in Accept is answered with HTTP 415
// ("Requested content type */* does not match back-end content type ...").
// These defaults match what BW systems advertise in the discovery document and
// are overridden by a discovery-resolved type when one is available.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string BwDefaultMediaType(const std::string& tlogo);

// Note: the action routes (?action=lock / ?action=unlock) negotiate on the
// object's media type too — "application/xml" is parsed as version 0.0.0 and
// rejected with "Your BW client is outdated ... client requested -v0_0_0".

}  // namespace erpl_adt
