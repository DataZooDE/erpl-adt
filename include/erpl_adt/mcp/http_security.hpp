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
//   Host validation stops the same browser being aimed here by DNS
//   rebinding, which Origin validation cannot see: once evil.example makes
//   rebind.evil.example resolve to 127.0.0.1, the browser treats the call as
//   same-origin — so it may read the answers, not only send writes — and the
//   Origin it sends matches the Host it sends, because the attacker wrote
//   both. The Host header is the half they cannot launder: the browser sends
//   the name the page used, never the IP it resolved to.
//
//   The bearer token stops everything else that can reach the port.
//
// The Host check applies to browser-originated requests only — those carrying
// an Origin or a Sec-Fetch-* header. That is not a weakening: rebinding is a
// browser attack by construction, /mcp accepts POST only, and a browser
// always sends Origin on a POST (Fetch, "Origin header" — set for every
// method other than GET/HEAD, same-origin included). So no rebinding request
// can reach /mcp without announcing itself, while curl, native MCP clients
// and server-to-server callers that address the server by a hostname are
// untouched.
//
// Origin and token stay permissive in the same spirit: a request without an
// Origin header is not a browser and is allowed, and the token is only
// enforced once one is configured.
// ---------------------------------------------------------------------------
struct HttpSecurityOptions {
    // Extra origins allowed beyond same-origin and loopback. The single
    // entry "*" restores the historical allow-everything behaviour.
    std::vector<std::string> allowed_origins;
    // Hosts allowed beyond loopback and IP literals — the bind host and
    // anything named by --allowed-hosts. "*" allows every host.
    std::vector<std::string> allowed_hosts;
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

// Why a request's Host was accepted or refused.
enum class HostVerdict {
    NoHost,        // no Host header — browsers always send one, so not a browser
    Loopback,      // localhost / 127.0.0.1 / [::1]
    IpLiteral,     // an IP address — there is no name to rebind
    Allowlisted,   // the bind host, or named by --allowed-hosts
    Wildcard,      // --allowed-hosts '*'
    Unrecognised,  // a DNS name we were not told about — the rebinding shape
};

[[nodiscard]] constexpr bool IsAllowed(HostVerdict verdict) {
    return verdict != HostVerdict::Unrecognised;
}

// Classify a Host header against the options. `host` is the raw header value
// ("name" or "name:port"); an empty one means the header was absent.
[[nodiscard]] HostVerdict ClassifyHost(const std::string& host,
                                       const HttpSecurityOptions& options);

// Did this request come from a browser? An Origin header or any Sec-Fetch-*
// header says yes, and only those are held to the Host allowlist — see the
// reasoning at the top of this file. `origin` is the raw header value.
[[nodiscard]] bool IsBrowserRequest(const std::string& origin,
                                    bool has_sec_fetch_header);

// True when `header` carries the configured bearer token. Comparison is
// constant-time so a token cannot be recovered one byte at a time by timing
// the response. An empty configured token means "no auth required" and every
// request passes.
[[nodiscard]] bool BearerTokenMatches(const std::string& authorization_header,
                                      const std::string& expected_token);

// Split a comma-separated flag value into pieces, trimming whitespace and
// dropping empties.
[[nodiscard]] std::vector<std::string> ParseCommaList(const std::string& value);

// Historical name, kept because --cors-origin is the older flag.
[[nodiscard]] inline std::vector<std::string> ParseOriginList(
    const std::string& value) {
    return ParseCommaList(value);
}

// Build the options from CLI flag values, shared by `mcp --http` and
// `catalog webui`. Warns on `err` about the configurations worth noticing —
// a wildcard origin or host, and binding somewhere other than loopback
// without a token — but does not refuse any of them, so nothing that runs
// today stops running. Returns nullopt only on an unusable configuration (an
// --auth-token-env naming a variable that is not set), having reported it.
[[nodiscard]] std::optional<HttpSecurityOptions> ResolveHttpSecurity(
    const std::string& cors_origin_flag,
    const std::string& allowed_hosts_flag,
    const std::string& auth_token_flag,
    const std::string& auth_token_env_flag,
    const std::string& bind_host,
    std::ostream& err);

}  // namespace erpl_adt
