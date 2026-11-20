#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// TransportInfo — metadata about a transport request.
// ---------------------------------------------------------------------------
struct TransportInfo {
    std::string number;       // e.g. "NPLK900001"
    std::string description;
    std::string owner;
    std::string status;       // "modifiable", "released"
    std::string target;
};

// ---------------------------------------------------------------------------
// ListTransports — list transport requests for a user.
//
// Endpoint: GET /sap/bc/adt/cts/transportrequests?user={user}
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<TransportInfo>, Error> ListTransports(
    IAdtSession& session,
    const std::string& user);

// ---------------------------------------------------------------------------
// TransportDetail — one request (or task) with its tasks and object entries,
// for reviewing what a release would ship.
// ---------------------------------------------------------------------------
struct TransportObjectEntry {
    std::string pgmid;        // "R3TR", "LIMU", "CORR"
    std::string type;         // "CLAS", "METH", "DEVC", ...
    std::string name;
    std::string description;  // tm:obj_info
    std::string lock_status;  // "X" when the object is locked on the request
};

struct TransportTask {
    std::string number;
    std::string description;
    std::string owner;
    std::string status;       // translated like TransportInfo::status
    std::string type;         // e.g. "Development/Correction", "Unclassified"
    std::vector<TransportObjectEntry> objects;
};

struct TransportDetail {
    std::string number;
    std::string description;
    std::string owner;
    std::string status;
    std::string target;
    std::string type;         // "K" workbench, "W" customizing, ...
    std::vector<TransportTask> tasks;
    std::vector<TransportObjectEntry> objects;  // entries on the request itself
};

// ---------------------------------------------------------------------------
// GetTransport — read a request (or task) with its tasks and object entries.
//
// Endpoint: GET /sap/bc/adt/cts/transportrequests/{number}
// A number that does not exist is NotFound (exit 2); a malformed number is
// refused before any request.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<TransportDetail, Error> GetTransport(
    IAdtSession& session,
    const std::string& transport_number);

// ---------------------------------------------------------------------------
// CreateTransport — create a new transport request.
//
// Endpoint: POST /sap/bc/adt/cts/transports
// Returns the transport number.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> CreateTransport(
    IAdtSession& session,
    const std::string& description,
    const std::string& target_package);

// ---------------------------------------------------------------------------
// ReleaseTransport — release a transport request.
//
// Endpoint: POST /sap/bc/adt/cts/transportrequests/{number}/newreleasejobs
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> ReleaseTransport(
    IAdtSession& session,
    const std::string& transport_number);

} // namespace erpl_adt
