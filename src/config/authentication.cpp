#include <erpl_adt/config/authentication.hpp>

namespace erpl_adt {

Result<AuthenticationMode, Error> ParseAuthenticationMode(std::string_view value) {
    if (value == "basic") return Result<AuthenticationMode, Error>::Ok(AuthenticationMode::Basic);
    if (value == "x509") return Result<AuthenticationMode, Error>::Ok(AuthenticationMode::X509);
    if (value == "snc") return Result<AuthenticationMode, Error>::Ok(AuthenticationMode::Snc);
    Error error;
    error.category = ErrorCategory::Validation;
    error.message = "Invalid authentication mode: " + std::string(value) + "; expected basic, x509 or snc";
    return Result<AuthenticationMode, Error>::Err(std::move(error));
}

std::string_view AuthenticationModeName(AuthenticationMode mode) {
    switch (mode) {
    case AuthenticationMode::Basic: return "basic";
    case AuthenticationMode::X509: return "x509";
    case AuthenticationMode::Snc: return "snc";
    }
    return "invalid";
}

} // namespace erpl_adt
