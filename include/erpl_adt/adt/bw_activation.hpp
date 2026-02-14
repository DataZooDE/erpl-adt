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
// BwActivateObjects — activate (or validate/simulate) BW objects.
//
// Endpoint: POST {activation_endpoint}?mode=...
// Content-Type: application/vnd.sap-bw-modeling.massact+xml
//
// Modes: activate, validate, simulate, background
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
    std::optional<std::string> transport;   // CORRNR for activation
};

[[nodiscard]] Result<BwActivationResult, Error> BwActivateObjects(
    IAdtSession& session,
    const BwActivateOptions& options);

} // namespace erpl_adt
