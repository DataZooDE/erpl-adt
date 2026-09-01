#include <erpl_adt/mcp/http_security.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ostream>

namespace erpl_adt {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// The authority of an origin or Host header — everything after the scheme,
// without a trailing slash. "http://localhost:8383" and "localhost:8383" both
// yield "localhost:8383", which is what makes an Origin comparable to a Host.
std::string Authority(const std::string& value) {
    auto out = Trim(value);
    const auto scheme = out.find("://");
    if (scheme != std::string::npos) {
        out = out.substr(scheme + 3);
    }
    while (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return ToLower(out);
}

// The host part of an authority, minus the port. IPv6 literals keep their
// brackets ("[::1]:8383" -> "[::1]") — the colons inside them are not a port
// separator.
std::string HostOf(const std::string& authority) {
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close != std::string::npos) {
            return authority.substr(0, close + 1);
        }
        return authority;
    }
    const auto colon = authority.find(':');
    return (colon == std::string::npos) ? authority : authority.substr(0, colon);
}

bool IsLoopbackHost(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
           host == "::1";
}

// Is this host an IP address rather than a name? Only names can be pointed at
// 127.0.0.1 by an attacker's DNS server, so an IP literal cannot carry a
// rebinding attack — which is what lets `--host 0.0.0.0` stay reachable at a
// LAN address with no configuration.
//
// Deliberately loose: a bracketed value is IPv6, and anything made only of
// digits and dots is IPv4. Both tests are cheap and neither can be satisfied
// by a registrable domain name, which is the only property that matters here.
bool IsIpLiteral(const std::string& host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        return true;
    }
    if (host.empty()) {
        return false;
    }
    return host.find_first_not_of("0123456789.") == std::string::npos &&
           host.find('.') != std::string::npos;
}

}  // namespace

OriginVerdict ClassifyOrigin(const std::string& origin, const std::string& host,
                             const HttpSecurityOptions& options) {
    // No Origin header: not a browser. curl, the CLI and every native MCP
    // client land here, which is why adding this check breaks none of them.
    if (Trim(origin).empty()) {
        return OriginVerdict::NoOrigin;
    }

    for (const auto& allowed : options.allowed_origins) {
        if (Trim(allowed) == "*") {
            return OriginVerdict::Wildcard;
        }
    }

    const auto origin_authority = Authority(origin);

    // Same-origin, established from the request itself rather than from a
    // configured list: this is what keeps the embedded web UI working on
    // whatever address the user reached it by — localhost, a LAN IP, a
    // hostname behind a proxy.
    if (!origin_authority.empty() && origin_authority == Authority(host)) {
        return OriginVerdict::SameOrigin;
    }

    if (IsLoopbackHost(HostOf(origin_authority))) {
        return OriginVerdict::Loopback;
    }

    for (const auto& allowed : options.allowed_origins) {
        if (Authority(allowed) == origin_authority) {
            return OriginVerdict::Allowlisted;
        }
    }

    return OriginVerdict::Denied;
}

HostVerdict ClassifyHost(const std::string& host,
                         const HttpSecurityOptions& options) {
    const auto authority = Authority(host);
    if (authority.empty()) {
        // Browsers always send Host, so an absent one is not the shape this
        // check exists for — it is an HTTP/1.0 or hand-rolled client.
        return HostVerdict::NoHost;
    }

    for (const auto& allowed : options.allowed_hosts) {
        if (Trim(allowed) == "*") {
            return HostVerdict::Wildcard;
        }
    }

    const auto name = HostOf(authority);
    if (IsLoopbackHost(name)) {
        return HostVerdict::Loopback;
    }
    if (IsIpLiteral(name)) {
        return HostVerdict::IpLiteral;
    }

    for (const auto& allowed : options.allowed_hosts) {
        const auto entry = Authority(allowed);
        // An entry naming a port is held to it; a bare name matches any port,
        // because "the host I serve on" is what the operator meant to say.
        const bool matches = (HostOf(entry) == entry) ? (entry == name)
                                                      : (entry == authority);
        if (matches) {
            return HostVerdict::Allowlisted;
        }
    }

    return HostVerdict::Unrecognised;
}

bool IsBrowserRequest(const std::string& origin, bool has_sec_fetch_header) {
    return has_sec_fetch_header || !Trim(origin).empty();
}

bool BearerTokenMatches(const std::string& authorization_header,
                        const std::string& expected_token) {
    if (expected_token.empty()) {
        return true;  // no token configured — auth is off
    }

    constexpr const char* kPrefix = "Bearer ";
    constexpr size_t kPrefixLen = 7;
    const auto header = Trim(authorization_header);
    if (header.size() <= kPrefixLen ||
        ToLower(header.substr(0, kPrefixLen)) != ToLower(kPrefix)) {
        return false;
    }
    const auto presented = header.substr(kPrefixLen);

    // Constant-time in the length of the expected token: compare every byte
    // and fold the result, so a wrong token cannot be recovered one byte at a
    // time from response timing. The length check is folded in rather than
    // short-circuited for the same reason.
    unsigned char diff = presented.size() == expected_token.size() ? 0 : 1;
    for (size_t i = 0; i < expected_token.size(); ++i) {
        const unsigned char lhs =
            i < presented.size() ? static_cast<unsigned char>(presented[i]) : 0;
        diff |= static_cast<unsigned char>(lhs ^
                static_cast<unsigned char>(expected_token[i]));
    }
    return diff == 0;
}

std::optional<HttpSecurityOptions> ResolveHttpSecurity(
    const std::string& cors_origin_flag, const std::string& allowed_hosts_flag,
    const std::string& auth_token_flag, const std::string& auth_token_env_flag,
    const std::string& bind_host, std::ostream& err) {
    HttpSecurityOptions options;
    options.allowed_origins = ParseCommaList(cors_origin_flag);
    options.allowed_hosts = ParseCommaList(allowed_hosts_flag);
    // The address the operator bound to is allowed without having to name it
    // twice. Loopback and IP literals are already allowed by ClassifyHost.
    const auto bind_name = HostOf(Authority(bind_host));
    if (!bind_name.empty() && !IsLoopbackHost(bind_name) && !IsIpLiteral(bind_name)) {
        options.allowed_hosts.push_back(bind_name);
    }
    options.auth_token = auth_token_flag;

    if (options.auth_token.empty() && !auth_token_env_flag.empty()) {
        const char* value = std::getenv(auth_token_env_flag.c_str());
        if (value == nullptr || *value == '\0') {
            err << "Error: --auth-token-env names '" << auth_token_env_flag
                << "', which is not set.\n";
            return std::nullopt;
        }
        options.auth_token = value;
    }

    for (const auto& origin : options.allowed_origins) {
        if (origin == "*") {
            err << "Warning: --cors-origin '*' allows any web page to call this "
                   "server. Name the origins you trust instead.\n";
            break;
        }
    }

    for (const auto& host : options.allowed_hosts) {
        if (host == "*") {
            err << "Warning: --allowed-hosts '*' accepts any Host header, which "
                   "turns off the DNS-rebinding defence entirely — any web page "
                   "your browser loads can then reach this server. Name the "
                   "hosts you serve on instead.\n";
            break;
        }
    }

    if (options.auth_token.empty() && !IsLoopbackHost(HostOf(Authority(bind_host)))) {
        err << "Warning: binding " << bind_host
            << " exposes this server beyond this machine with no authentication. "
               "Pass --auth-token to require one.\n";
    }

    return options;
}

std::vector<std::string> ParseCommaList(const std::string& value) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= value.size()) {
        const auto comma = value.find(',', pos);
        const auto piece = Trim(value.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (!piece.empty()) {
            out.push_back(piece);
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return out;
}

}  // namespace erpl_adt
