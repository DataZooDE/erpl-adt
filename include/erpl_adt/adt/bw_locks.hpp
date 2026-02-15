#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwLockEntry — a BW object lock from the lock monitoring endpoint.
// ---------------------------------------------------------------------------
struct BwLockEntry {
    std::string client;
    std::string user;
    std::string mode;           // "E" (exclusive), etc.
    std::string table_name;     // e.g. "RSBWOBJ_ENQUEUE"
    std::string table_desc;
    std::string object;         // locked object name
    std::string arg;            // base64-encoded argument
    std::string owner1;         // base64-encoded owner 1
    std::string owner2;         // base64-encoded owner 2
    std::string timestamp;      // YYYYMMDDHHMMSS
    int upd_count = 0;
    int dia_count = 0;
};

// ---------------------------------------------------------------------------
// BwListLocksOptions — options for listing BW locks.
// ---------------------------------------------------------------------------
struct BwListLocksOptions {
    std::optional<std::string> user;        // Filter by user
    std::optional<std::string> search;      // Search pattern
    int max_results = 100;
};

// ---------------------------------------------------------------------------
// BwDeleteLockOptions — parameters to delete a specific lock.
// ---------------------------------------------------------------------------
struct BwDeleteLockOptions {
    std::string user;           // Lock owner user
    std::string table_name;     // Table name from lock entry
    std::string arg;            // Base64 arg from lock entry
    std::string scope;          // Lock scope (typically "1")
    std::string lock_mode;      // Lock mode (e.g., "E")
    std::string owner1;         // Base64 owner1 from lock entry
    std::string owner2;         // Base64 owner2 from lock entry
};

// ---------------------------------------------------------------------------
// BwListLocks — list BW object locks.
//
// Endpoint: GET /sap/bw/modeling/utils/locks?user=...&search=...
// Response: XML <bwLocks:dataContainer> with <lock> children.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<BwLockEntry>, Error> BwListLocks(
    IAdtSession& session,
    const BwListLocksOptions& options);

// ---------------------------------------------------------------------------
// BwDeleteLock — delete a stuck BW lock (admin operation).
//
// Endpoint: DELETE /sap/bw/modeling/utils/locks?user=...
// Headers:  BW_OBJNAME, BW_ARGUMENT, BW_SCOPE, BW_TYPE, BW_OWNER1, BW_OWNER2
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> BwDeleteLock(
    IAdtSession& session,
    const BwDeleteLockOptions& options);

} // namespace erpl_adt
