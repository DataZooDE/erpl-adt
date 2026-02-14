#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwObjectMetadata — parsed metadata from a BW object read.
// ---------------------------------------------------------------------------
struct BwObjectMetadata {
    std::string name;
    std::string type;            // Tlogo, e.g. "ADSO", "IOBJ"
    std::string description;
    std::string version;         // "a", "m", "d"
    std::string status;          // "ACT", "INA"
    std::string package_name;
    std::string last_changed_by;
    std::string last_changed_at;
    std::string raw_xml;         // Full XML for save-back workflows
};

// ---------------------------------------------------------------------------
// BwReadObject — read a BW object definition.
//
// Endpoint: GET /sap/bw/modeling/{tlogo}/{name}/{version}
// Accept:   application/vnd.sap.bw.modeling.{tlogo}-v{version}+xml
// ---------------------------------------------------------------------------

struct BwReadOptions {
    std::string object_type;               // Required: tlogo (e.g. "ADSO")
    std::string object_name;               // Required: name (e.g. "ZSALES")
    std::string version = "a";             // "a" (active), "m" (modified), "d" (delivery)
    std::optional<std::string> source_system;  // Required for RSDS, APCO
    bool raw = false;                      // Return raw XML only
};

[[nodiscard]] Result<BwObjectMetadata, Error> BwReadObject(
    IAdtSession& session,
    const BwReadOptions& options);

// ---------------------------------------------------------------------------
// BwLockResult — result of a BW object lock operation.
// ---------------------------------------------------------------------------
struct BwLockResult {
    std::string lock_handle;
    std::string transport_number;    // CORRNR
    std::string transport_text;      // CORRTEXT
    std::string transport_owner;     // CORRUSER
    std::string timestamp;           // Server timestamp
    std::string package_name;        // Development-Class header
    bool is_local = false;           // IS_LOCAL
};

// ---------------------------------------------------------------------------
// BwLockObject — lock a BW object for editing.
//
// Endpoint: POST /sap/bw/modeling/{tlogo}/{name}?action=lock
// Requires stateful session + CSRF token.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwLockResult, Error> BwLockObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& activity = "CHAN");

// ---------------------------------------------------------------------------
// BwUnlockObject — release a lock on a BW object.
//
// Endpoint: POST /sap/bw/modeling/{tlogo}/{name}?action=unlock
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwUnlockObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name);

// ---------------------------------------------------------------------------
// BwSaveObject — save a modified BW object.
//
// Endpoint: PUT /sap/bw/modeling/{tlogo}/{name}?lockHandle=...&corrNr=...&timestamp=...
// ---------------------------------------------------------------------------

struct BwSaveOptions {
    std::string object_type;
    std::string object_name;
    std::string content;             // Modified XML body
    std::string lock_handle;
    std::string transport;           // CORRNR
    std::string timestamp;
};

[[nodiscard]] Result<void, Error> BwSaveObject(
    IAdtSession& session,
    const BwSaveOptions& options);

// ---------------------------------------------------------------------------
// BwDeleteObject — delete a BW object.
//
// Endpoint: DELETE /sap/bw/modeling/{tlogo}/{name}?lockHandle=...&corrNr=...
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwDeleteObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& lock_handle,
    const std::string& transport);

} // namespace erpl_adt
