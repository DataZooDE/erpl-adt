#pragma once

#include <erpl_adt/adt/bw_context_headers.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwObjectMetadata — parsed metadata from a BW object read.
// ---------------------------------------------------------------------------
struct BwObjectMetadata {
    // Identity
    std::string name;
    std::string type;              // Tlogo, e.g. "ADSO", "IOBJ"
    std::string sub_type;          // xsi:type value, e.g. "iobj:TimeCharacteristic"
    std::string description;
    std::string long_description;
    std::string short_description;
    std::string version;           // "a", "m", "d"

    // tlogoProperties + root attributes
    std::string status;            // objectStatus: "active", "inactive"
    std::string content_state;     // ACT, INA, MOD
    std::string info_area;
    std::string responsible;
    std::string created_at;
    std::string package_name;
    std::string last_changed_by;
    std::string last_changed_at;
    std::string language;

    // Type-specific properties (root attributes + key child element text)
    std::map<std::string, std::string> properties;

    std::string raw_xml;           // Full XML for save-back workflows
};

// ---------------------------------------------------------------------------
// BwReadObject — read a BW object definition.
//
// Endpoint: GET /sap/bw/modeling/{tlogo}/{name}/{version}
// Accept:   application/vnd.sap.bw.modeling.{tlogo}-v{version}+xml
// ---------------------------------------------------------------------------

struct BwReadOptions {
    std::string object_type;               // Required: tlogo (e.g. "ADSO") — used for Accept header
    std::string object_name;               // Required: name (e.g. "ZSALES")
    std::string version = "a";             // "a" (active), "m" (modified), "d" (delivery)
    std::optional<std::string> source_system;  // Required for RSDS, APCO
    std::optional<std::string> uri;        // Direct URI override (from search results)
    std::optional<std::string> content_type;   // From discovery, overrides default Accept header
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
    std::string foreign_object_locks;  // Foreign-Object-Locks header
    bool is_local = false;           // IS_LOCAL
};

// ---------------------------------------------------------------------------
// BwCreateObject — create a BW object.
// ---------------------------------------------------------------------------
struct BwCreateOptions {
    std::string object_type;
    std::string object_name;
    std::optional<std::string> package_name;
    std::optional<std::string> copy_from_name;
    std::optional<std::string> copy_from_type;
    std::optional<std::string> content;
    std::optional<std::string> content_type;
};

struct BwCreateResult {
    std::string uri;
    int http_status = 0;
};

[[nodiscard]] Result<BwCreateResult, Error> BwCreateObject(
    IAdtSession& session,
    const BwCreateOptions& options);

// ---------------------------------------------------------------------------
// BwLockObject — lock a BW object for editing.
//
// Endpoint: POST /sap/bw/modeling/{tlogo}/{name}?action=lock
// Requires stateful session + CSRF token.
// ---------------------------------------------------------------------------
struct BwLockOptions {
    std::string object_type;
    std::string object_name;
    std::string activity = "CHAN";
    std::optional<std::string> parent_name;
    std::optional<std::string> parent_type;
    BwContextHeaders context_headers;
};

[[nodiscard]] Result<BwLockResult, Error> BwLockObject(
    IAdtSession& session,
    const BwLockOptions& options);

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
    std::optional<std::string> content_type;   // From discovery, overrides default Content-Type
    BwContextHeaders context_headers;
};

[[nodiscard]] Result<void, Error> BwSaveObject(
    IAdtSession& session,
    const BwSaveOptions& options);

// ---------------------------------------------------------------------------
// BwDeleteObject — delete a BW object.
//
// Endpoint: DELETE /sap/bw/modeling/{tlogo}/{name}?lockHandle=...&corrNr=...
// ---------------------------------------------------------------------------
struct BwDeleteOptions {
    std::string object_type;
    std::string object_name;
    std::string lock_handle;
    std::string transport;
    BwContextHeaders context_headers;
};

[[nodiscard]] Result<void, Error> BwDeleteObject(
    IAdtSession& session,
    const BwDeleteOptions& options);

[[nodiscard]] Result<void, Error> BwDeleteObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& lock_handle,
    const std::string& transport);

} // namespace erpl_adt
