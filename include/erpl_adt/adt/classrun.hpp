#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ClassRunResult — output from an ABAP console class execution.
// ---------------------------------------------------------------------------
struct ClassRunResult {
    std::string class_name;  // name as supplied by the caller
    std::string output;      // raw console text from IF_OO_ADT_CLASSRUN
};

// ---------------------------------------------------------------------------
// RunClass — execute an ABAP class implementing IF_OO_ADT_CLASSRUN.
//
// Endpoint: POST /sap/bc/adt/oo/classrun/{className}
// class_name may be a bare name (ZCL_FOO, /DMO/CL_FOO) or a full ADT URI
// (/sap/bc/adt/oo/classes/ZCL_FOO). Namespace slashes are percent-encoded.
// Returns the plain-text console output on success.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<ClassRunResult, Error> RunClass(
    IAdtSession& session,
    std::string_view class_name);

} // namespace erpl_adt
