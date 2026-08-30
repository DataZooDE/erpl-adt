#pragma once

#include <array>
#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Connection settings from the environment.
//
// Every connection setting can be supplied through the environment under two
// spellings: the project prefix (ERPL_ADT_HOST) and the SAP-style prefix
// (SAP_HOST).  The project prefix wins.  Empty values are treated as unset, so
// `SAP_PASSWORD= erpl-adt ...` does not silently authenticate with "".
//
// Precedence in the CLI: explicit flag > environment > .adt.creds > default.
// ---------------------------------------------------------------------------

// The two environment variable names that carry `setting`, most specific
// first.  `setting` is the bare name in upper case, e.g. "HOST".
[[nodiscard]] std::array<std::string, 2> ConnectionEnvNames(
    const std::string& setting);

// The value of `setting` from the environment, or nullopt when neither
// spelling is set to a non-empty value.
[[nodiscard]] std::optional<std::string> ConnectionEnvValue(
    const std::string& setting);

}  // namespace erpl_adt
