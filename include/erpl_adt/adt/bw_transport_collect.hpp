#pragma once

#include <erpl_adt/adt/bw_context_headers.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwCollectObject — an object in the transport collection details.
// ---------------------------------------------------------------------------
struct BwCollectObject {
    std::string name;
    std::string type;
    std::string description;
    std::string status;
    std::string uri;
    std::string last_changed_by;
    std::string last_changed_at;
};

// ---------------------------------------------------------------------------
// BwCollectDependency — a dependency in the transport collection.
// ---------------------------------------------------------------------------
struct BwCollectDependency {
    std::string name;
    std::string type;
    std::string version;
    std::string author;
    std::string package_name;
    std::string association_type;
    std::string associated_name;
    std::string associated_type;
};

// ---------------------------------------------------------------------------
// BwTransportCollectResult — result of transport collection.
// ---------------------------------------------------------------------------
struct BwTransportCollectResult {
    std::vector<BwCollectObject> details;
    std::vector<BwCollectDependency> dependencies;
    std::vector<std::string> messages;
};

// ---------------------------------------------------------------------------
// BwTransportCollect — collect dependent objects for transport.
//
// Endpoint: POST /sap/bw/modeling/cto?collect=true&mode={mode}
// Content-Type: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
// Accept:       application/vnd.sap-bw-modeling.trcollect+xml
//
// Mode values:
//   000 = necessary objects only
//   001 = complete collection
//   003 = dataflow above
//   004 = dataflow below
// ---------------------------------------------------------------------------

struct BwTransportCollectOptions {
    std::string object_type;                     // Required
    std::string object_name;                     // Required
    std::string mode = "000";                    // 000, 001, 003, 004
    std::optional<std::string> transport;        // CORRNR
    BwContextHeaders context_headers;
};

[[nodiscard]] Result<BwTransportCollectResult, Error> BwTransportCollect(
    IAdtSession& session,
    const BwTransportCollectOptions& options);

} // namespace erpl_adt
