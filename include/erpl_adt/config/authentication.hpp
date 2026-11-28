#pragma once

#include <erpl_adt/core/result.hpp>
#include <string_view>

namespace erpl_adt {

enum class AuthenticationMode { Basic, X509, Snc };

[[nodiscard]] Result<AuthenticationMode, Error> ParseAuthenticationMode(std::string_view value);
[[nodiscard]] std::string_view AuthenticationModeName(AuthenticationMode mode);

} // namespace erpl_adt
