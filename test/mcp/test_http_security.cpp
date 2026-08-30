#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/http_security.hpp>

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
