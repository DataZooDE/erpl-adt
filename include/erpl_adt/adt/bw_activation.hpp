#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwActivationObject — an object in an activation request.
// ---------------------------------------------------------------------------
struct BwActivationObject {
    std::string name;
    std::string type;              // Tlogo
    std::string subtype;
    std::string version = "M";    // Modified
    std::string status = "INA";   // Inactive
    std::string description;
    std::string uri;               // href to object
    std::string transport;         // CORRNR
    std::string package_name;
};

// ---------------------------------------------------------------------------
// BwActivationMessage — a message from activation result.
// ---------------------------------------------------------------------------
struct BwActivationMessage {
    std::string object_name;
    std::string object_type;
    std::string severity;       // "E" error, "W" warning, "I" info, "S" success
    std::string text;
};

// ---------------------------------------------------------------------------
// BwActivationResult — result of an activation operation.
// ---------------------------------------------------------------------------
struct BwActivationResult {
    bool success = false;
    std::string job_guid;                         // Non-empty for background jobs
    std::vector<BwActivationMessage> messages;
};

// ---------------------------------------------------------------------------
// BwActivateObjects — activate (or check) BW objects.
//
// Endpoint: POST /sap/bw/modeling/activation   (activate)
//           POST /sap/bw/modeling/checkruns    (check only)
// Content-Type: application/vnd.sap.bw.modeling.activation-v1_0_0+xml
//
// The body is an Atom feed carrying exactly ONE entry: the backend
// (CL_RSO_RES_ACTIVATION) deserializes it with cl_atom_feed_prov and rejects
// a feed with any other number of entries. The entry's rel="self" link names
// the object; its content is a {http://www.sap.com/bw/modeling}
// checkProperties element, whose attributes the RSO_RES_ST_BW_CHECKRUN
// transformation maps to the check-run parameters. The mode comes from the
// path, not from a query parameter. Several objects are several requests.
// ---------------------------------------------------------------------------

enum class BwActivationMode {
    Activate,
    Validate,
    Simulate,
    Background,
};

struct BwActivateOptions {
    std::vector<BwActivationObject> objects;
    BwActivationMode mode = BwActivationMode::Activate;
    bool force = false;                     // Force activation with warnings
    bool exec_checks = false;               // execChk
    bool with_cto = false;                  // withCTO
    bool sort = false;                      // validate: sort
    bool only_inactive = false;             // validate: onlyina
    std::optional<std::string> endpoint_override;  // Full endpoint URL override
    std::optional<std::string> transport;   // transportId in checkProperties
    std::string lock_handle;                // From a prior bw lock, when held
};

[[nodiscard]] Result<BwActivationResult, Error> BwActivateObjects(
    IAdtSession& session,
    const BwActivateOptions& options);

} // namespace erpl_adt
