#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwDbInfo — HANA database connection information.
// ---------------------------------------------------------------------------
struct BwDbInfo {
    std::string host;
    std::string port;
    std::string schema;
    std::string database_type;
    std::string database_name;      // <dbInfo:name>
    std::string instance;           // <dbInfo:connect[@instance]>
    std::string user;               // <dbInfo:connect[@user]>
    std::string version;            // <dbInfo:version[@server]>
    std::string patchlevel;         // <dbInfo:patchlevel>
};

// ---------------------------------------------------------------------------
// BwSystemProperty — a key-value system property.
// ---------------------------------------------------------------------------
struct BwSystemProperty {
    std::string key;
    std::string value;
    std::string description;
};

// ---------------------------------------------------------------------------
// BwChangeabilityEntry — per-TLOGO changeability setting.
// ---------------------------------------------------------------------------
struct BwChangeabilityEntry {
    std::string object_type;        // TLOGO code (e.g., "ADSO", "IOBJ")
    std::string changeable;         // changeability status
    std::string transportable;      // "X" or ""
    std::string description;
};

// ---------------------------------------------------------------------------
// BwAdtUriMapping — BW-to-ADT URI mapping entry.
// ---------------------------------------------------------------------------
struct BwAdtUriMapping {
    std::string bw_type;            // BW type code
    std::string adt_type;           // ADT type mapping
    std::string bw_uri_template;    // BW URI template
    std::string adt_uri_template;   // ADT URI template
};

// ---------------------------------------------------------------------------
// BwGetDbInfo — get HANA database info (host, port, schema).
//
// Endpoint: GET /sap/bw/modeling/repo/is/dbinfo
// Accept:   application/atom+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwDbInfo, Error> BwGetDbInfo(IAdtSession& session);

// ---------------------------------------------------------------------------
// BwGetSystemInfo — get system properties.
//
// Endpoint: GET /sap/bw/modeling/repo/is/systeminfo
// Accept:   application/atom+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwSystemProperty>, Error> BwGetSystemInfo(
    IAdtSession& session);

// ---------------------------------------------------------------------------
// BwGetChangeability — get per-TLOGO changeability settings.
//
// Endpoint: GET /sap/bw/modeling/repo/is/chginfo
// Accept:   application/atom+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwChangeabilityEntry>, Error> BwGetChangeability(
    IAdtSession& session);

// ---------------------------------------------------------------------------
// BwGetAdtUriMappings — get BW-to-ADT URI mappings.
//
// Endpoint: GET /sap/bw/modeling/repo/is/adturi
// Accept:   application/atom+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwAdtUriMapping>, Error> BwGetAdtUriMappings(
    IAdtSession& session);

} // namespace erpl_adt
