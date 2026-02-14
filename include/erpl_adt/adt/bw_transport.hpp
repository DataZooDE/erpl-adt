#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwTransportRequest — a transport request entry.
// ---------------------------------------------------------------------------
struct BwTransportTask {
    std::string number;
    std::string function_type;
    std::string status;
    std::string owner;
};

struct BwTransportRequest {
    std::string number;
    std::string function_type;
    std::string status;
    std::string description;
    std::vector<BwTransportTask> tasks;
};

// ---------------------------------------------------------------------------
// BwTransportObject — an object entry in transport state.
// ---------------------------------------------------------------------------
struct BwTransportObject {
    std::string name;
    std::string type;
    std::string operation;
    std::string uri;
    std::string lock_request;
    std::string tadir_status;
};

// ---------------------------------------------------------------------------
// BwChangeability — changeability setting for an object type.
// ---------------------------------------------------------------------------
struct BwChangeability {
    std::string tlogo;
    bool transportable = false;
    bool changeable = false;
};

// ---------------------------------------------------------------------------
// BwTransportCheckResult — result of transport state check.
// ---------------------------------------------------------------------------
struct BwTransportCheckResult {
    bool writing_enabled = false;
    std::vector<BwChangeability> changeability;
    std::vector<BwTransportRequest> requests;
    std::vector<BwTransportObject> objects;
    std::vector<std::string> messages;
};

// ---------------------------------------------------------------------------
// BwTransportCheck — check transport state for BW objects.
//
// Endpoint: GET {cto_endpoint}?rddetails=...
// Accept: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwTransportCheckResult, Error> BwTransportCheck(
    IAdtSession& session,
    bool own_only = false);

// ---------------------------------------------------------------------------
// BwTransportWrite — write BW objects to a transport request.
//
// Endpoint: POST {cto_endpoint}?corrnum=...&package=...
// Content-Type: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
// ---------------------------------------------------------------------------

struct BwTransportWriteOptions {
    std::string object_type;
    std::string object_name;
    std::string transport;           // CORRNR
    std::string package_name;
    bool simulate = false;
};

struct BwTransportWriteResult {
    bool success = false;
    std::vector<std::string> messages;
};

[[nodiscard]] Result<BwTransportWriteResult, Error> BwTransportWrite(
    IAdtSession& session,
    const BwTransportWriteOptions& options);

} // namespace erpl_adt
