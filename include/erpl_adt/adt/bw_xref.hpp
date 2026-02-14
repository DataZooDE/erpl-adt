#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwXrefEntry — a single cross-reference from a BW object.
// ---------------------------------------------------------------------------
struct BwXrefEntry {
    std::string name;
    std::string type;
    std::string version;
    std::string status;
    std::string description;
    std::string uri;
    std::string association_type;    // "001", "003", etc.
    std::string association_label;   // "Used by", "Requires"
};

// ---------------------------------------------------------------------------
// BwGetXrefs — get cross-references for a BW object.
//
// Endpoint: GET /sap/bw/modeling/repo/is/xref?objectType=...&objectName=...
// Accept:   application/atom+xml
//
// XML response is parsed directly with tinyxml2.
// ---------------------------------------------------------------------------

struct BwXrefOptions {
    std::string object_type;                            // Required
    std::string object_name;                            // Required
    std::optional<std::string> object_version;          // "A", "M"
    std::optional<std::string> association;              // Filter by association code
    std::optional<std::string> associated_object_type;  // Filter by related type
};

[[nodiscard]] Result<std::vector<BwXrefEntry>, Error> BwGetXrefs(
    IAdtSession& session,
    const BwXrefOptions& options);

} // namespace erpl_adt
