#pragma once

#include <erpl_adt/config/app_config.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <string_view>

namespace erpl_adt {

// Parse a YAML config file into an AppConfig.
Result<AppConfig, Error> LoadFromYaml(std::string_view file_path);

// Parse CLI arguments into an AppConfig.
// For single-repo CLI usage, connection + repo fields are merged into one config.
Result<AppConfig, Error> LoadFromCli(int argc, const char* const* argv);

// Merge two configs: cli_overrides take precedence over yaml_base.
// Fields set in cli_overrides replace those in yaml_base.
AppConfig MergeConfigs(const AppConfig& yaml_base, const AppConfig& cli_overrides);

// Resolve password_env: if password is empty and password_env is set,
// read the environment variable and populate password.
Result<AppConfig, Error> ResolvePasswordEnv(AppConfig config);

// Validate that all required fields are present and values are sane.
Result<void, Error> ValidateConfig(const AppConfig& config);

// Sort repos topologically by depends_on. Detects cycles.
Result<std::vector<RepoConfig>, Error> SortReposByDependency(
    const std::vector<RepoConfig>& repos);

} // namespace erpl_adt
