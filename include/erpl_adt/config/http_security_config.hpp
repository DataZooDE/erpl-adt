#pragma once

#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// The `http:` block of a YAML config — access control for the two HTTP
// servers (`mcp --http`, `catalog webui`).
//
// Neither of those builds an AppConfig: `mcp` parses its own flags because it
// is a single-word command, and `catalog webui` goes through CommandArgs. So
// this reads just the block they need rather than dragging the deploy config
// into either path.
//
//   http:
//     allowed_hosts: [mcp.internal.example, buildbox.corp]
//     cors_origin: [https://catalog.example]
//     auth_token_env: ERPL_ADT_MCP_TOKEN
//
// Each key accepts a list or a comma-separated string, and comes back in the
// comma-separated form the flags already take, so there is one parser
// downstream (mcp/http_security.hpp's ParseCommaList) rather than two.
//
// There is deliberately no `auth_token` key: a secret in a file that lives
// next to the code is a secret that gets committed. Name the variable that
// holds it instead.
// ---------------------------------------------------------------------------
struct HttpSecurityFileSettings {
    std::optional<std::string> allowed_hosts;
    std::optional<std::string> cors_origin;
    std::optional<std::string> auth_token_env;
};

// Read the `http:` block from `path`. A config with no such block is not an
// error — every deploy config written so far is in that shape — and yields a
// settings object with nothing set. Err only when the file cannot be read or
// is not valid YAML.
[[nodiscard]] Result<HttpSecurityFileSettings, Error> LoadHttpSecuritySettings(
    const std::string& path);

}  // namespace erpl_adt
