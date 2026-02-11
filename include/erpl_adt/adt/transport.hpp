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
