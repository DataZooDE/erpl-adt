#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <stdlib.h>  // _putenv_s
#endif

using namespace erpl_adt;

// ===========================================================================
// Helper: RAII cleanup of .adt.creds in a temp directory
// ===========================================================================

namespace {

namespace fs = std::filesystem;

// Change to a temp directory and restore on destruction.
struct TempDirGuard {
    fs::path original_dir;
    fs::path temp_dir;

    explicit TempDirGuard(const fs::path& dir) {
        original_dir = fs::current_path();
        temp_dir = dir;
        fs::current_path(temp_dir);
    }
    ~TempDirGuard() {
        // Clean up .adt.creds if it exists.
        std::remove(".adt.creds");
        fs::current_path(original_dir);
    }
};

fs::path MakeTempDir() {
    auto base = fs::temp_directory_path() / "erpl_adt_test";
    fs::create_directories(base);
    // Use a unique subdir per call.
    static int counter = 0;
    auto dir = base / std::to_string(++counter);
    fs::create_directories(dir);
    return dir;
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
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
    UnsetEnv("SAP_PASSWORD");

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV"
    };
    int rc = HandleLogin(6, argv);
    CHECK(rc == 99);
}

TEST_CASE("login: invalid --port returns 99", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV",
        "--password", "pass",
        "--port", "not-a-number"
    };
    int rc = HandleLogin(10, argv);
    CHECK(rc == 99);

    std::ifstream ifs(".adt.creds");
    CHECK_FALSE(ifs.good());
}

TEST_CASE("login: invalid --client returns 99", "[cli][credentials]") {
    auto tmp = MakeTempDir();
    TempDirGuard guard(tmp);

    const char* argv[] = {
        "erpl-adt", "login",
        "--host", "myhost",
        "--user", "DEV",
        "--password", "pass",
        "--client", "12"
    };
    int rc = HandleLogin(10, argv);
    CHECK(rc == 99);

    std::ifstream ifs(".adt.creds");
    CHECK_FALSE(ifs.good());
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

    SetEnv("TEST_LOGIN_PW", "envpass");

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

    UnsetEnv("TEST_LOGIN_PW");
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
