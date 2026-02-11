#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace erpl_adt;

// ===========================================================================
// Helper: RAII cleanup of .adt.creds in a temp directory
// ===========================================================================

namespace {

// Change to a temp directory and restore on destruction.
struct TempDirGuard {
    std::string original_dir;
    std::string temp_dir;

    explicit TempDirGuard(const std::string& dir) {
        char cwd[4096];
        auto* p = ::getcwd(cwd, sizeof(cwd));
        if (p) { original_dir = p; }
        temp_dir = dir;
        (void)::chdir(temp_dir.c_str());
    }
    ~TempDirGuard() {
        // Clean up .adt.creds if it exists.
        std::remove(".adt.creds");
        (void)::chdir(original_dir.c_str());
    }
};

std::string MakeTempDir() {
    char tmpl[] = "/tmp/erpl_adt_test_XXXXXX";
    auto* dir = ::mkdtemp(tmpl);
    return std::string(dir);
}

} // namespace

// ===========================================================================
// HandleLogin / HandleLogout via CLI dispatch
// ===========================================================================

TEST_CASE("login: saves .adt.creds file", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost.example.com",
        "--port", "44300",
        "--user", "DEVUSER",
        "--password", "secret123",
        "--client", "100",
        "--https=true"
    };
    int rc = HandleLogin(13, argv);
    CHECK(rc == 0);

    // Verify file exists and contains expected JSON.
    std::ifstream ifs(".adt.creds");
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    REQUIRE(!content.empty());

    // Parse and verify fields.
    auto j = nlohmann::json::parse(content);
    CHECK(j["host"] == "myhost.example.com");
    CHECK(j["port"] == 44300);
    CHECK(j["user"] == "DEVUSER");
    CHECK(j["password"] == "secret123");
    CHECK(j["client"] == "100");
    CHECK(j["use_https"] == true);
}

TEST_CASE("login: default port and client", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV",
        "--password", "pass"
    };
    int rc = HandleLogin(8, argv);
    CHECK(rc == 0);

    std::ifstream ifs(".adt.creds");
    REQUIRE(ifs.good());
    auto j = nlohmann::json::parse(ifs);
    CHECK(j["port"] == 50000);
    CHECK(j["client"] == "001");
    CHECK(j["use_https"] == false);
}

TEST_CASE("login: missing --host returns 99", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    const char* argv[] = {
        "erpl-adt", "login",
        "--user", "DEV",
        "--password", "pass"
    };
    int rc = HandleLogin(6, argv);
    CHECK(rc == 99);
}

TEST_CASE("login: missing --password returns 99", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    // Ensure SAP_PASSWORD env var doesn't interfere.
    unsetenv("SAP_PASSWORD");

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV"
    };
    int rc = HandleLogin(6, argv);
    CHECK(rc == 99);
}

TEST_CASE("logout: deletes .adt.creds file", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    // Create a dummy creds file.
    {
        std::ofstream ofs(".adt.creds");
        ofs << R"({"host":"x"})";
    }

    int rc = HandleLogout();
    CHECK(rc == 0);

    // File should be gone.
    std::ifstream ifs(".adt.creds");
    CHECK_FALSE(ifs.good());
}

TEST_CASE("logout: succeeds even when no creds file", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    int rc = HandleLogout();
    CHECK(rc == 0);
}

TEST_CASE("login: password-env fallback works", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    setenv("TEST_LOGIN_PW", "envpass", 1);

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV",
        "--password-env", "TEST_LOGIN_PW"
    };
    int rc = HandleLogin(8, argv);
    CHECK(rc == 0);

    std::ifstream ifs(".adt.creds");
    REQUIRE(ifs.good());
    auto j = nlohmann::json::parse(ifs);
    CHECK(j["password"] == "envpass");

    unsetenv("TEST_LOGIN_PW");
}

// ===========================================================================
// IsNewStyleCommand does NOT match login/logout
// ===========================================================================

TEST_CASE("IsNewStyleCommand: login is NOT a new-style command",
          "[cli][credentials]") {
    const char* argv[] = {"erpl-adt", "login", "--host", "x"};
    CHECK_FALSE(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: logout is NOT a new-style command",
          "[cli][credentials]") {
    const char* argv[] = {"erpl-adt", "logout"};
    CHECK_FALSE(IsNewStyleCommand(2, argv));
}
