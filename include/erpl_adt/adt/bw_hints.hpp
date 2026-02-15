#pragma once

#include <erpl_adt/core/result.hpp>

namespace erpl_adt {

/// Adds an actionable hint to a BW-related error, guiding the user toward
/// the correct SAP transaction to resolve the problem.  No-op if the error
/// does not match a known BW failure pattern.
void AddBwHint(Error& error);

} // namespace erpl_adt
