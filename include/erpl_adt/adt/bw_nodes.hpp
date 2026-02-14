#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwNodeEntry — a single child node of a BW object.
// ---------------------------------------------------------------------------
struct BwNodeEntry {
    std::string name;
    std::string type;
    std::string subtype;
    std::string description;
    std::string version;
    std::string status;
    std::string uri;
};

// ---------------------------------------------------------------------------
// BwGetNodes — get repository node structure for a BW object.
//
// Endpoint (InfoProvider): GET /sap/bw/modeling/repo/infoproviderstructure/{type}/{name}
// Endpoint (DataSource):   GET /sap/bw/modeling/repo/datasourcestructure/{type}/{name}
// Accept:   application/atom+xml
//
// XML response is parsed directly with tinyxml2.
// ---------------------------------------------------------------------------

struct BwNodesOptions {
    std::string object_type;                     // Required
    std::string object_name;                     // Required
    bool datasource = false;                     // Use datasourcestructure path
    std::optional<std::string> child_name;       // Filter by child name
    std::optional<std::string> child_type;       // Filter by child type
};

[[nodiscard]] Result<std::vector<BwNodeEntry>, Error> BwGetNodes(
    IAdtSession& session,
    const BwNodesOptions& options);

} // namespace erpl_adt
