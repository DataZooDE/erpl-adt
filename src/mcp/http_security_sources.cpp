#include <erpl_adt/mcp/http_security_sources.hpp>

#include <erpl_adt/config/http_security_config.hpp>
#include <erpl_adt/core/connection_env.hpp>

#include <ostream>

namespace erpl_adt {

namespace {

// flag > environment > file, the first non-empty one winning.
std::string Resolve(const std::string& flag_value,
                    const std::string& env_setting,
                    const std::optional<std::string>& file_value) {
    if (!flag_value.empty()) {
        return flag_value;
    }
    if (auto from_env = ConnectionEnvValue(env_setting); from_env.has_value()) {
        return *from_env;
    }
    return file_value.value_or("");
}

std::string Join(const std::vector<std::string>& values,
                 const std::string& empty_text) {
    if (values.empty()) {
        return empty_text;
    }
    std::string joined;
    for (const auto& value : values) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += value;
    }
    return joined;
}

}  // namespace

std::optional<HttpSecurityOptions> ResolveHttpSecurityFromCli(
    const HttpSecurityFlagValues& flags, std::ostream& err) {
    HttpSecurityFileSettings from_file;
    if (!flags.config_path.empty()) {
        auto loaded = LoadHttpSecuritySettings(flags.config_path);
        if (loaded.IsErr()) {
            err << "Error: " << loaded.Error().message << "\n";
            return std::nullopt;
        }
        from_file = std::move(loaded).Value();
    }

    return ResolveHttpSecurity(
        Resolve(flags.cors_origin, "CORS_ORIGIN", from_file.cors_origin),
        Resolve(flags.allowed_hosts, "ALLOWED_HOSTS", from_file.allowed_hosts),
        Resolve(flags.auth_token, "AUTH_TOKEN", std::nullopt),
        Resolve(flags.auth_token_env, "AUTH_TOKEN_ENV", from_file.auth_token_env),
        flags.bind_host, err);
}

void PrintHttpSecurityPosture(const HttpSecurityOptions& options,
                              std::ostream& out) {
    out << "  hosts:   loopback, IP literals"
        << (options.allowed_hosts.empty()
                ? " (add with --allowed-hosts)"
                : ", " + Join(options.allowed_hosts, ""))
        << "\n";
    out << "  origins: same-origin, loopback"
        << (options.allowed_origins.empty()
                ? " (add with --cors-origin)"
                : ", " + Join(options.allowed_origins, ""))
        << "\n";
    out << "  auth:    "
        << (options.auth_token.empty() ? "none (--auth-token to require a bearer token)"
                                       : "bearer token required")
        << "\n";
}

}  // namespace erpl_adt
