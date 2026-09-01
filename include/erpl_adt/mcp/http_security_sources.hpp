#pragma once

#include <erpl_adt/mcp/http_security.hpp>

#include <iosfwd>
#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Where the access-control settings come from.
//
// `mcp --http` and `catalog webui` parse their flags separately — one is a
// single-word command, the other goes through the router — but they must
// resolve these settings identically, or the same YAML file would mean two
// different things. This is that one resolution.
//
// Precedence per setting: flag > environment > YAML > default, matching how
// every connection setting already behaves.
// ---------------------------------------------------------------------------
struct HttpSecurityFlagValues {
    std::string cors_origin;     // --cors-origin
    std::string allowed_hosts;   // --allowed-hosts
    std::string auth_token;      // --auth-token
    std::string auth_token_env;  // --auth-token-env
    std::string config_path;     // -c / --config (optional)
    std::string bind_host;       // the address being bound
};

// Resolve the options, reporting an unusable configuration on `err` and
// returning nullopt — an unreadable --config, or an --auth-token-env naming a
// variable that is not set. Environment names are the usual pair, project
// prefix first: ERPL_ADT_ALLOWED_HOSTS / SAP_ALLOWED_HOSTS, and likewise for
// CORS_ORIGIN and AUTH_TOKEN.
[[nodiscard]] std::optional<HttpSecurityOptions> ResolveHttpSecurityFromCli(
    const HttpSecurityFlagValues& flags, std::ostream& err);

// One line per control, printed after the "listening on" line. A default that
// refuses requests should be visible in the log of the server that refuses
// them, not only in the docs.
void PrintHttpSecurityPosture(const HttpSecurityOptions& options,
                              std::ostream& out);

}  // namespace erpl_adt
