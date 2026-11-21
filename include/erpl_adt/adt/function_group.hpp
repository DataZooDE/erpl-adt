#pragma once

#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Function group parts over ADT — the requests Eclipse ADT sends when a
// function module or a function group include is created.
//
//   POST /sap/bc/adt/functions/groups/{fg}/fmodules   (fmodule:abapFunctionModule, FUGR/FF)
//   POST /sap/bc/adt/functions/groups/{fg}/includes   (finclude:abapFunctionGroupInclude, FUGR/I)
//
// The new object is empty (a FUNCTION statement without interface / an empty
// include); its source is written afterwards with lock + PUT .../source/main.
// ---------------------------------------------------------------------------

// URIs, lower case, '/' of namespaces escaped: "/sap/bc/adt/functions/groups/zfg".
[[nodiscard]] std::string FunctionGroupUri(const std::string& group);
[[nodiscard]] std::string FunctionModuleUri(const std::string& group, const std::string& name);
[[nodiscard]] std::string FunctionGroupIncludeUri(const std::string& group,
                                                  const std::string& include);

[[nodiscard]] Result<void, Error> CreateFunctionModule(
    IAdtSession& session, const std::string& group, const std::string& name,
    const std::string& description, const std::optional<std::string>& transport);

[[nodiscard]] Result<void, Error> CreateFunctionGroupInclude(
    IAdtSession& session, const std::string& group, const std::string& include,
    const std::string& description, const std::optional<std::string>& transport);

// The RFC flag of a function module is metadata, not source: ADT ignores
// fmodule:processingType on create. Eclipse's properties view sets it by
// GETting the module's metadata and PUTting it back with the new type under
// the lock (`lock_handle`); measured on A4H. `processing_type` is ADT's value:
// "normal" or "rfc".
[[nodiscard]] Result<void, Error> SetFunctionModuleProcessingType(
    IAdtSession& session, const std::string& group, const std::string& name,
    const std::string& processing_type, const LockHandle& lock_handle,
    const std::optional<std::string>& transport);

// The main program of a function group: SAPL<group>, or /NS/SAPL<group> for
// a namespaced group (upper case). The one place that spells this (crew F8).
[[nodiscard]] std::string FunctionGroupMainProgramName(const std::string& group);

// What activates a function group's main program SAPL<fg> (`main_program`):
// the group (FUGR/F) and the main program as the object ADT lists it - FUGR/I
// named SAPL<fg> at <group>/source/main - in one request. The group alone
// leaves REPS SAPL<fg> inactive; the main program alone is answered with a
// pre-audit list naming the group (both measured on A4H).
[[nodiscard]] std::vector<ActivateObjectParams> FunctionGroupMainProgramActivation(
    const std::string& group, const std::string& main_program);

}  // namespace erpl_adt
