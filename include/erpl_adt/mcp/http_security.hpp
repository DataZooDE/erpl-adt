#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Access control for the MCP HTTP transport.
//
// The endpoint behind these checks executes writes against a live SAP system
// (adt_write_source, adt_delete_object, adt_activate, adt_release_transport,
// bw_delete_object). Two controls guard it, and they answer different threats:
//
//   Origin validation stops a *browser* the developer already trusts from
//   being turned into a confused deputy by any page they happen to visit.
//   Binding to 127.0.0.1 does not help there — the browser is inside the
//   loopback boundary.
//
//   The bearer token stops everything else that can reach the port.
//
// Both are permissive by default so that no existing deployment breaks: a
// request without an Origin header is not a browser and is allowed, and the
// token is only enforced once one is configured.
// ---------------------------------------------------------------------------
struct HttpSecurityOptions {
    // Extra origins allowed beyond same-origin and loopback. The single
    // entry "*" restores the historical allow-everything behaviour.
    std::vector<std::string> allowed_origins;
    // When non-empty, /mcp requires "Authorization: Bearer <token>".
    std::string auth_token;
};

// Why a request's Origin was accepted or rejected. Same-origin and loopback
// are separate outcomes so the caller can say which rule applied.
enum class OriginVerdict {
    NoOrigin,      // not a browser request — allow
    SameOrigin,    // Origin matches the request's own Host — allow
    Loopback,      // localhost / 127.0.0.1 / [::1] on any port — allow
    Allowlisted,   // named by --cors-origin — allow
    Wildcard,      // --cors-origin '*' — allow
    Denied,        // cross-origin and not allowed — 403
};

[[nodiscard]] constexpr bool IsAllowed(OriginVerdict verdict) {
    return verdict != OriginVerdict::Denied;
}

// Classify an Origin header against the request's own Host and the options.
// `origin` and `host` are the raw header values; an empty `origin` means the
// header was absent.
[[nodiscard]] OriginVerdict ClassifyOrigin(const std::string& origin,
                                           const std::string& host,
                                           const HttpSecurityOptions& options);

// True when `header` carries the configured bearer token. Comparison is
// constant-time so a token cannot be recovered one byte at a time by timing
// the response. An empty configured token means "no auth required" and every
// request passes.
[[nodiscard]] bool BearerTokenMatches(const std::string& authorization_header,
                                      const std::string& expected_token);

// Split a comma-separated --cors-origin value into individual origins,
// trimming whitespace and dropping empties.
[[nodiscard]] std::vector<std::string> ParseOriginList(const std::string& value);

// Build the options from CLI flag values, shared by `mcp --http` and
// `catalog webui`. Warns on `err` about the two configurations worth
// noticing — a wildcard origin, and binding somewhere other than loopback
// without a token — but does not refuse either, so nothing that runs today
// stops running. Returns nullopt only on an unusable configuration (an
// --auth-token-env naming a variable that is not set), having reported it.
[[nodiscard]] std::optional<HttpSecurityOptions> ResolveHttpSecurity(
    const std::string& cors_origin_flag,
    const std::string& auth_token_flag,
    const std::string& auth_token_env_flag,
    const std::string& bind_host,
    std::ostream& err);

}  // namespace erpl_adt
