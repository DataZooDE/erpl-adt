#include <erpl_adt/config/http_security_config.hpp>

#include <yaml-cpp/yaml.h>

#include <fstream>

namespace erpl_adt {

namespace {

Error MakeConfigError(const std::string& message) {
    return Error{"ConfigLoader", "", std::nullopt, message, std::nullopt};
}

// One key, in either spelling. A sequence is joined with commas so callers
// see exactly what the equivalent flag value would have been; a scalar is
// passed through untouched, commas and all.
std::optional<std::string> ReadListOrString(const YAML::Node& node) {
    if (!node) {
        return std::nullopt;
    }
    if (node.IsSequence()) {
        std::string joined;
        for (const auto& entry : node) {
            if (!joined.empty()) {
                joined += ",";
            }
            joined += entry.as<std::string>();
        }
        return joined.empty() ? std::nullopt : std::optional<std::string>(joined);
    }
    auto value = node.as<std::string>("");
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

}  // namespace

Result<HttpSecurityFileSettings, Error> LoadHttpSecuritySettings(
    const std::string& path) {
    std::ifstream probe(path);
    if (!probe.good()) {
        return Result<HttpSecurityFileSettings, Error>::Err(
            MakeConfigError("Config file not found: " + path));
    }
    probe.close();

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        return Result<HttpSecurityFileSettings, Error>::Err(
            MakeConfigError("Failed to parse YAML: " + std::string(e.what())));
    }

    HttpSecurityFileSettings settings;
    const auto http = root["http"];
    if (!http) {
        // Not an error: every config written before this block existed.
        return Result<HttpSecurityFileSettings, Error>::Ok(std::move(settings));
    }

    settings.allowed_hosts = ReadListOrString(http["allowed_hosts"]);
    settings.cors_origin = ReadListOrString(http["cors_origin"]);
    settings.auth_token_env = ReadListOrString(http["auth_token_env"]);
    return Result<HttpSecurityFileSettings, Error>::Ok(std::move(settings));
}

}  // namespace erpl_adt
