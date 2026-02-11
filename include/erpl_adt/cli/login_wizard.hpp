#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace erpl_adt {

struct LoginCredentials {
    std::string host;
    uint16_t port = 50000;
    std::string user;
    std::string password;
    std::string client;
    bool use_https = false;
};

// Show interactive login form. Returns filled credentials on Save,
// std::nullopt on Cancel/Escape. Pre-populates from `defaults` if provided.
std::optional<LoginCredentials> RunLoginWizard(
    const std::optional<LoginCredentials>& defaults = std::nullopt);

} // namespace erpl_adt
