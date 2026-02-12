#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/adt_session.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

using namespace erpl_adt;

namespace {

// Helper: create an AdtSession that won't actually connect anywhere.
AdtSession MakeDummySession() {
    auto client = SapClient::Create("001").Value();
    return AdtSession("127.0.0.1", 1, false, "user", "pass", client);
}

// Helper: write arbitrary content to a temp file using mkstemp.
std::string WriteTempFile(const std::string& content) {
    char tmpl[] = "/tmp/erpl_adt_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) {
        if (::write(fd, content.c_str(), content.size()) < 0) { /* best-effort */ }
        ::close(fd);
    }
    return std::string(tmpl);
}

// Helper: create a unique temp file path (no content).
std::string MakeTempPath() {
    char tmpl[] = "/tmp/erpl_adt_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) { ::close(fd); }
    return std::string(tmpl);
}

} // namespace

// ===========================================================================
// SaveSession / LoadSession round-trip
// ===========================================================================

TEST_CASE("SaveSession writes JSON that LoadSession can restore",
          "[adt][session][persistence]") {
    auto session1 = MakeDummySession();

    // Put session into a known stateful state by loading a crafted file.
    auto seed_path = WriteTempFile(R"({
        "csrf_token": "abc123",
        "stateful": true,
        "context_id": "ctx-42",
        "cookies": {"SAP_SESSIONID": "sid1", "sap-usercontext": "uc1"}
    })");
    auto load_result = session1.LoadSession(seed_path);
    REQUIRE(load_result.IsOk());
    std::remove(seed_path.c_str());

    CHECK(session1.IsStateful());

    // Save to a new file.
    auto save_path = MakeTempPath();
    auto save_result = session1.SaveSession(save_path);
    REQUIRE(save_result.IsOk());

    // Load into a fresh session and verify state matches.
    auto session2 = MakeDummySession();
    CHECK_FALSE(session2.IsStateful());

    auto reload_result = session2.LoadSession(save_path);
    REQUIRE(reload_result.IsOk());
    CHECK(session2.IsStateful());

    std::remove(save_path.c_str());
}

TEST_CASE("LoadSession with missing file returns Err",
          "[adt][session][persistence]") {
    auto session = MakeDummySession();
    auto result = session.LoadSession("/nonexistent/path/session.json");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Failed to open") != std::string::npos);
}

TEST_CASE("LoadSession with malformed JSON returns Err",
          "[adt][session][persistence]") {
    auto path = WriteTempFile("{ not valid json }}}");
    auto session = MakeDummySession();
    auto result = session.LoadSession(path);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Malformed JSON") != std::string::npos);
    std::remove(path.c_str());
}

TEST_CASE("SaveSession to unwritable path returns Err",
          "[adt][session][persistence]") {
    auto session = MakeDummySession();
    auto result = session.SaveSession("/nonexistent/dir/session.json");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Failed to open") != std::string::npos);
}

TEST_CASE("LoadSession with empty JSON object keeps defaults",
          "[adt][session][persistence]") {
    auto path = WriteTempFile("{}");
    auto session = MakeDummySession();
    auto result = session.LoadSession(path);
    REQUIRE(result.IsOk());
    CHECK_FALSE(session.IsStateful());
    std::remove(path.c_str());
}
