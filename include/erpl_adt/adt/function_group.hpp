#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>

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

}  // namespace erpl_adt
