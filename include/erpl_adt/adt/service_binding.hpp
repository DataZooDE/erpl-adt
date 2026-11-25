#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Publishing OData service bindings (SRVB) (#104).
//
// Activating a service binding does not give the service an endpoint;
// publishing does (ADT's "Publish" button in the binding editor). For V2 the
// service then answers at /sap/opu/odata/sap/<SERVICE>.
//
//   POST /sap/bc/adt/businessservices/odatav2/publishjobs
//        ?servicename=<NAME>&serviceversion=<VERSION>
//   POST .../odatav2/unpublishjobs?...                   (same shape)
//   V4:  .../odatav4/publishjobs, .../odatav4/unpublishjobs
//
// Body: <adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">
//         <adtcore:objectReference adtcore:name="NAME"/>
//       </adtcore:objectReferences>
//
// Answer: asx:abap/asx:values/DATA with SEVERITY (OK on success), SHORT_TEXT
// and LONG_TEXT. A job SAP refuses still answers HTTP 200 - the verdict is in
// SEVERITY, not in the status code.
// ---------------------------------------------------------------------------

enum class ODataVersion { V2, V4 };

// "v2" / "V2" / "v4" / "V4" -> ODataVersion; anything else is a Validation
// error naming the accepted values.
[[nodiscard]] Result<ODataVersion, Error> ParseODataVersion(std::string_view text);

// "v2" or "v4".
[[nodiscard]] std::string ODataVersionName(ODataVersion version);

enum class ServicePublishAction { Publish, Unpublish };

// The job's answer, plus what was asked for.
struct ServicePublishResult {
    std::string binding;          // service name as sent, e.g. ZUI_TRAVEL_O2
    std::string service_version;  // e.g. 0001
    ODataVersion odata = ODataVersion::V2;
    ServicePublishAction action = ServicePublishAction::Publish;
    std::string severity;    // SAP's SEVERITY, e.g. OK or ERROR
    std::string short_text;  // SHORT_TEXT
    std::string long_text;   // LONG_TEXT (often empty)

    // True when SAP answered SEVERITY OK (case-insensitive).
    [[nodiscard]] bool Succeeded() const;
};

// The request path, query included (exposed for tests and docs).
[[nodiscard]] std::string ServicePublishPath(ServicePublishAction action,
                                             ODataVersion odata,
                                             std::string_view service_name,
                                             std::string_view service_version);

// Parse the asx:abap answer into severity/short_text/long_text. An answer
// without asx:values/DATA/SEVERITY is an error (it is not a publish result).
[[nodiscard]] Result<ServicePublishResult, Error> ParseServicePublishResponse(
    std::string_view xml);

// Publish / unpublish the service of a binding. The name is sent upper-case
// (SAP keeps repository names upper-case). service_version defaults to
// 0001 (the version `object create --type SRVB/SVB` creates). An HTTP status
// other than 2xx is an Err carrying SAP's message; a non-OK SEVERITY is an Ok
// result whose Succeeded() is false.
[[nodiscard]] Result<ServicePublishResult, Error> PublishServiceBinding(
    IAdtSession& session,
    std::string_view service_name,
    std::string_view service_version = "0001",
    ODataVersion odata = ODataVersion::V2);

[[nodiscard]] Result<ServicePublishResult, Error> UnpublishServiceBinding(
    IAdtSession& session,
    std::string_view service_name,
    std::string_view service_version = "0001",
    ODataVersion odata = ODataVersion::V2);

// The one JSON shape for `service publish|unpublish --json` and the MCP tools:
// binding, service_version, odata, action, severity, short_text, long_text
// and ok (SEVERITY was OK).
[[nodiscard]] nlohmann::json ServicePublishResultToJson(const ServicePublishResult& result);

} // namespace erpl_adt
