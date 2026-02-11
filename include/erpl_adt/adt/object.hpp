#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ObjectInfo — metadata about an ABAP object.
// ---------------------------------------------------------------------------
struct ObjectInfo {
    std::string name;
    std::string type;           // e.g. "CLAS/OC"
    std::string uri;
    std::string description;
    std::string source_uri;     // relative path to source
    std::string version;        // "active", "inactive"
    std::string language;
    std::string responsible;
    std::string changed_by;
    std::string changed_at;
    std::string created_at;
};

// ---------------------------------------------------------------------------
// ObjectInclude — a source include within an object (e.g. class includes).
// ---------------------------------------------------------------------------
struct ObjectInclude {
    std::string name;
    std::string type;
    std::string include_type;   // e.g. "main", "definitions", "implementations"
    std::string source_uri;
};

// ---------------------------------------------------------------------------
// ObjectStructure — full object structure with includes.
// ---------------------------------------------------------------------------
struct ObjectStructure {
    ObjectInfo info;
    std::vector<ObjectInclude> includes;
};

// ---------------------------------------------------------------------------
// GetObjectStructure — fetch metadata and structure for an ADT object.
//
// Endpoint: GET {objectUri}
// Parses XML directly with tinyxml2.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<ObjectStructure, Error> GetObjectStructure(
    IAdtSession& session,
    const ObjectUri& uri);

// ---------------------------------------------------------------------------
// CreateObjectParams — parameters for creating a new ABAP object.
// ---------------------------------------------------------------------------
struct CreateObjectParams {
    std::string object_type;     // e.g. "CLAS/OC", "PROG/P"
    std::string name;            // e.g. "ZCL_MY_CLASS"
    std::string package_name;    // e.g. "ZTEST"
    std::string description;     // human-readable description
    std::optional<std::string> transport_number;  // e.g. "NPLK900001"
    std::optional<std::string> responsible;        // owner (defaults to session user)
};

// ---------------------------------------------------------------------------
// CreateObject — create a new ABAP object.
//
// Endpoint: POST /sap/bc/adt/{creationPath}?corrNr={transport}
// Returns the URI of the created object.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<ObjectUri, Error> CreateObject(
    IAdtSession& session,
    const CreateObjectParams& params);

// ---------------------------------------------------------------------------
// DeleteObject — delete an ABAP object.
//
// Endpoint: DELETE {objectUri}?lockHandle={handle}&corrNr={transport}
// Requires a lock handle from a prior LockObject call.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> DeleteObject(
    IAdtSession& session,
    const ObjectUri& uri,
    const LockHandle& lock_handle,
    const std::optional<std::string>& transport_number = std::nullopt);

} // namespace erpl_adt
