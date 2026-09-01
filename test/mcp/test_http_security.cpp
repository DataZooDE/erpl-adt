#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/http_security.hpp>

#include <sstream>
#include <string>

using namespace erpl_adt;

// ===========================================================================
// Origin classification
//
// The endpoint behind this executes SAP writes, so the rules are worth
// pinning: everything that works today must keep working, and a cross-origin
// browser request must not.
// ===========================================================================

TEST_CASE("ClassifyOrigin: no Origin header is allowed", "[mcp][security]") {
    // curl, the CLI, every native MCP client. Not a browser, not the threat.
    HttpSecurityOptions options;
    CHECK(ClassifyOrigin("", "localhost:8383", options) == OriginVerdict::NoOrigin);
    CHECK(ClassifyOrigin("   ", "localhost:8383", options) == OriginVerdict::NoOrigin);
}

TEST_CASE("ClassifyOrigin: same-origin is allowed on any address", "[mcp][security]") {
    // The embedded web UI is served from this very server, so its POSTs carry
    // an Origin equal to the Host — whatever address the user reached it by.
    HttpSecurityOptions options;
    CHECK(ClassifyOrigin("http://localhost:8383", "localhost:8383", options) ==
          OriginVerdict::SameOrigin);
    CHECK(ClassifyOrigin("http://192.168.1.5:8383", "192.168.1.5:8383", options) ==
          OriginVerdict::SameOrigin);
    CHECK(ClassifyOrigin("https://catalog.internal", "catalog.internal", options) ==
          OriginVerdict::SameOrigin);
}

TEST_CASE("ClassifyOrigin: loopback origins are allowed", "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyOrigin("http://localhost:5173", "127.0.0.1:8383", options) ==
          OriginVerdict::Loopback);
    CHECK(ClassifyOrigin("http://127.0.0.1:3000", "localhost:8383", options) ==
          OriginVerdict::Loopback);
    CHECK(ClassifyOrigin("http://[::1]:9000", "localhost:8383", options) ==
          OriginVerdict::Loopback);
}

TEST_CASE("ClassifyOrigin: a cross-origin page is denied", "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyOrigin("https://evil.example", "localhost:8383", options) ==
          OriginVerdict::Denied);
    // A host that merely *contains* a loopback name is still a foreign origin.
    CHECK(ClassifyOrigin("https://localhost.evil.example", "localhost:8383", options) ==
          OriginVerdict::Denied);
    CHECK(ClassifyOrigin("http://127.0.0.1.evil.example", "localhost:8383", options) ==
          OriginVerdict::Denied);
}

TEST_CASE("ClassifyOrigin: an allowlisted origin is allowed", "[mcp][security]") {
    HttpSecurityOptions options;
    options.allowed_origins = {"https://catalog.example"};
    CHECK(ClassifyOrigin("https://catalog.example", "localhost:8383", options) ==
          OriginVerdict::Allowlisted);
    CHECK(ClassifyOrigin("https://other.example", "localhost:8383", options) ==
          OriginVerdict::Denied);
}

TEST_CASE("ClassifyOrigin: allowlist entries compare by authority", "[mcp][security]") {
    // A user writing "catalog.example" or a trailing slash means the same
    // thing as the scheme-qualified form.
    HttpSecurityOptions options;
    options.allowed_origins = {"catalog.example:9000/"};
    CHECK(ClassifyOrigin("https://catalog.example:9000", "localhost:8383", options) ==
          OriginVerdict::Allowlisted);
}

TEST_CASE("ClassifyOrigin: '*' restores allow-everything", "[mcp][security]") {
    // The documented escape hatch for anyone depending on the old wildcard.
    HttpSecurityOptions options;
    options.allowed_origins = {"*"};
    CHECK(ClassifyOrigin("https://evil.example", "localhost:8383", options) ==
          OriginVerdict::Wildcard);
    CHECK(IsAllowed(OriginVerdict::Wildcard));
}

TEST_CASE("ClassifyOrigin: comparison ignores case and trailing slash",
          "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyOrigin("HTTP://LocalHost:8383/", "localhost:8383", options) ==
          OriginVerdict::SameOrigin);
}

TEST_CASE("IsAllowed: only Denied blocks", "[mcp][security]") {
    CHECK(IsAllowed(OriginVerdict::NoOrigin));
    CHECK(IsAllowed(OriginVerdict::SameOrigin));
    CHECK(IsAllowed(OriginVerdict::Loopback));
    CHECK(IsAllowed(OriginVerdict::Allowlisted));
    CHECK(!IsAllowed(OriginVerdict::Denied));
}

// ===========================================================================
// Bearer token
// ===========================================================================

TEST_CASE("BearerTokenMatches: no configured token means auth is off",
          "[mcp][security]") {
    CHECK(BearerTokenMatches("", ""));
    CHECK(BearerTokenMatches("Bearer whatever", ""));
}

TEST_CASE("BearerTokenMatches: accepts the right token only", "[mcp][security]") {
    CHECK(BearerTokenMatches("Bearer s3cret", "s3cret"));
    CHECK(!BearerTokenMatches("Bearer wrong", "s3cret"));
    CHECK(!BearerTokenMatches("", "s3cret"));
    CHECK(!BearerTokenMatches("s3cret", "s3cret"));          // missing scheme
    CHECK(!BearerTokenMatches("Basic s3cret", "s3cret"));    // wrong scheme
}

TEST_CASE("BearerTokenMatches: scheme is case-insensitive, token is not",
          "[mcp][security]") {
    CHECK(BearerTokenMatches("bearer s3cret", "s3cret"));
    CHECK(!BearerTokenMatches("Bearer S3CRET", "s3cret"));
}

TEST_CASE("BearerTokenMatches: a prefix of the token is rejected",
          "[mcp][security]") {
    CHECK(!BearerTokenMatches("Bearer s3c", "s3cret"));
    CHECK(!BearerTokenMatches("Bearer s3cretlonger", "s3cret"));
}

// ===========================================================================
// --cors-origin parsing
// ===========================================================================

TEST_CASE("ParseOriginList: splits and trims", "[mcp][security]") {
    const auto parsed = ParseOriginList(" https://a.example , https://b.example ");
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0] == "https://a.example");
    CHECK(parsed[1] == "https://b.example");
}

TEST_CASE("ParseOriginList: empty pieces are dropped", "[mcp][security]") {
    CHECK(ParseOriginList("").empty());
    CHECK(ParseOriginList(" , ,").empty());
    const auto parsed = ParseOriginList("https://a.example,,");
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0] == "https://a.example");
}

// ===========================================================================
// Host classification
//
// Origin validation alone does not stop DNS rebinding. Once a page at
// evil.example makes rebind.evil.example resolve to 127.0.0.1, the browser
// considers the request same-origin and sends Host: rebind.evil.example with
// either a matching Origin or none at all — and both of those are allowed by
// the Origin rules above. The Host header is the half the attacker cannot
// launder, so it is checked on its own.
//
// An IP literal is a deliberate pass: there is no name to rebind, which is
// what keeps `catalog webui --host 0.0.0.0` reachable at a LAN address with
// no configuration.
// ===========================================================================

TEST_CASE("ClassifyHost: loopback names are recognised", "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyHost("localhost:8383", options) == HostVerdict::Loopback);
    CHECK(ClassifyHost("127.0.0.1:8383", options) == HostVerdict::Loopback);
    CHECK(ClassifyHost("[::1]:8383", options) == HostVerdict::Loopback);
    CHECK(ClassifyHost("LocalHost", options) == HostVerdict::Loopback);
}

TEST_CASE("ClassifyHost: an IP literal cannot be rebound", "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyHost("192.168.1.5:8383", options) == HostVerdict::IpLiteral);
    CHECK(ClassifyHost("10.0.0.7", options) == HostVerdict::IpLiteral);
    CHECK(ClassifyHost("[fe80::1]:8383", options) == HostVerdict::IpLiteral);
}

TEST_CASE("ClassifyHost: an unknown name is the rebinding shape",
          "[mcp][security]") {
    HttpSecurityOptions options;
    CHECK(ClassifyHost("rebind.evil.example:8383", options) ==
          HostVerdict::Unrecognised);
    // A name that merely contains a loopback label is still a name.
    CHECK(ClassifyHost("localhost.evil.example", options) ==
          HostVerdict::Unrecognised);
    CHECK(ClassifyHost("127.0.0.1.evil.example", options) ==
          HostVerdict::Unrecognised);
}

TEST_CASE("ClassifyHost: a configured host is allowed", "[mcp][security]") {
    HttpSecurityOptions options;
    options.allowed_hosts = {"mcp.internal.example"};
    CHECK(ClassifyHost("mcp.internal.example:8383", options) ==
          HostVerdict::Allowlisted);
    CHECK(ClassifyHost("mcp.internal.example", options) == HostVerdict::Allowlisted);
    CHECK(ClassifyHost("other.internal.example", options) == HostVerdict::Unrecognised);
}

TEST_CASE("ClassifyHost: a configured host may name a port", "[mcp][security]") {
    // "host:port" and a bare host both mean the same thing to a user; only
    // the port-qualified form is picky, and then only about that port.
    HttpSecurityOptions options;
    options.allowed_hosts = {"mcp.internal.example:8383"};
    CHECK(ClassifyHost("mcp.internal.example:8383", options) ==
          HostVerdict::Allowlisted);
    CHECK(ClassifyHost("mcp.internal.example:9999", options) ==
          HostVerdict::Unrecognised);
}

TEST_CASE("ClassifyHost: '*' allows any host", "[mcp][security]") {
    HttpSecurityOptions options;
    options.allowed_hosts = {"*"};
    CHECK(ClassifyHost("rebind.evil.example", options) == HostVerdict::Wildcard);
}

TEST_CASE("ClassifyHost: a missing Host header is not a browser",
          "[mcp][security]") {
    // Browsers always send Host, so an absent one cannot be the rebinding
    // shape — it is an HTTP/1.0 client or a hand-rolled request.
    HttpSecurityOptions options;
    CHECK(ClassifyHost("", options) == HostVerdict::NoHost);
    CHECK(ClassifyHost("  ", options) == HostVerdict::NoHost);
}

TEST_CASE("ClassifyHost: only Unrecognised is ever refused", "[mcp][security]") {
    CHECK(IsAllowed(HostVerdict::NoHost));
    CHECK(IsAllowed(HostVerdict::Loopback));
    CHECK(IsAllowed(HostVerdict::IpLiteral));
    CHECK(IsAllowed(HostVerdict::Allowlisted));
    CHECK(IsAllowed(HostVerdict::Wildcard));
    CHECK(!IsAllowed(HostVerdict::Unrecognised));
}

// ===========================================================================
// ResolveHttpSecurity — the flag-to-options mapping
// ===========================================================================

TEST_CASE("ResolveHttpSecurity: enforcement is off until --allowed-hosts",
          "[mcp][security]") {
    // The default has to stay warn-only: every deployment reached by a DNS
    // name today keeps working, and enforcement is one flag away.
    std::ostringstream err;
    auto options = ResolveHttpSecurity("", "", "", "", "127.0.0.1", err);
    REQUIRE(options.has_value());
    CHECK(!options->enforce_hosts);

    std::ostringstream err2;
    auto enforcing =
        ResolveHttpSecurity("", "mcp.internal.example", "", "", "0.0.0.0", err2);
    REQUIRE(enforcing.has_value());
    CHECK(enforcing->enforce_hosts);
    CHECK(ClassifyHost("mcp.internal.example", *enforcing) ==
          HostVerdict::Allowlisted);
}

TEST_CASE("ResolveHttpSecurity: the bind host is allowed without naming it twice",
          "[mcp][security]") {
    std::ostringstream err;
    auto options = ResolveHttpSecurity("", "other.example", "", "",
                                       "mcp.internal.example", err);
    REQUIRE(options.has_value());
    CHECK(ClassifyHost("mcp.internal.example:8383", *options) ==
          HostVerdict::Allowlisted);
}

TEST_CASE("ResolveHttpSecurity: --allowed-hosts '*' warns", "[mcp][security]") {
    std::ostringstream err;
    auto options = ResolveHttpSecurity("", "*", "", "", "0.0.0.0", err);
    REQUIRE(options.has_value());
    CHECK(err.str().find("--allowed-hosts") != std::string::npos);
}
