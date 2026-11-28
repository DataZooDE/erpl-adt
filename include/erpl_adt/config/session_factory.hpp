#pragma once

#include <erpl_adt/config/app_config.hpp>
#include <map>
#include <memory>

namespace erpl_adt {

struct SessionConfiguration {
    ConnectionConfig connection;
    AdtSessionOptions options;
};

// Shared CLI/MCP resolution: explicit flags > environment > saved profile > defaults.
[[nodiscard]] Result<SessionConfiguration, Error> ResolveSessionConfiguration(
    const std::map<std::string, std::string>& flags,
    const std::string& credentials_path = ".adt.creds");

[[nodiscard]] Result<std::unique_ptr<IAdtSession>, Error> CreateAdtSession(
    const SessionConfiguration& configuration);

} // namespace erpl_adt
