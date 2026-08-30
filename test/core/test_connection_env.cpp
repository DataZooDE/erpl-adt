#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/connection_env.hpp>
#include <erpl_adt/core/result.hpp>

#include <cstdlib>
#include <string>

using namespace erpl_adt;

namespace {

// RAII helper: set an environment variable for the duration of a test.
class ScopedEnv {
public:
    ScopedEnv(std::string name, const std::string& value) : name_(std::move(name)) {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        ::setenv(name_.c_str(), value.c_str(), 1);
#endif
    }
    ~ScopedEnv() {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        ::unsetenv(name_.c_str());
#endif
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
};

} // anonymous namespace

TEST_CASE("ConnectionEnvValue: reads the ERPL_ADT_ prefix", "[core][env]") {
    ScopedEnv guard("ERPL_ADT_HOST", "sap.example.com");
    auto value = ConnectionEnvValue("HOST");
    REQUIRE(value.has_value());
    CHECK(*value == "sap.example.com");
}

TEST_CASE("ConnectionEnvValue: reads the SAP_ prefix", "[core][env]") {
    ScopedEnv guard("SAP_HOST", "sap-legacy.example.com");
    auto value = ConnectionEnvValue("HOST");
    REQUIRE(value.has_value());
    CHECK(*value == "sap-legacy.example.com");
}

TEST_CASE("ConnectionEnvValue: ERPL_ADT_ wins over SAP_", "[core][env]") {
    ScopedEnv sap("SAP_USER", "LEGACY");
    ScopedEnv erpl("ERPL_ADT_USER", "PREFERRED");
    auto value = ConnectionEnvValue("USER");
    REQUIRE(value.has_value());
    CHECK(*value == "PREFERRED");
}

TEST_CASE("ConnectionEnvValue: empty values are ignored", "[core][env]") {
    ScopedEnv empty("ERPL_ADT_CLIENT", "");
    ScopedEnv fallback("SAP_CLIENT", "100");
    auto value = ConnectionEnvValue("CLIENT");
    REQUIRE(value.has_value());
    CHECK(*value == "100");
}

TEST_CASE("ConnectionEnvValue: unset returns nullopt", "[core][env]") {
    CHECK(!ConnectionEnvValue("NO_SUCH_SETTING_XYZ").has_value());
}

TEST_CASE("ConnectionEnvValue: covers every connection setting", "[core][env]") {
    ScopedEnv host("ERPL_ADT_HOST", "h");
    ScopedEnv port("ERPL_ADT_PORT", "44300");
    ScopedEnv user("ERPL_ADT_USER", "u");
    ScopedEnv client("ERPL_ADT_CLIENT", "001");
    ScopedEnv password("ERPL_ADT_PASSWORD", "p");
    ScopedEnv language("ERPL_ADT_LANGUAGE", "DE");

    CHECK(ConnectionEnvValue("HOST") == std::optional<std::string>("h"));
    CHECK(ConnectionEnvValue("PORT") == std::optional<std::string>("44300"));
    CHECK(ConnectionEnvValue("USER") == std::optional<std::string>("u"));
    CHECK(ConnectionEnvValue("CLIENT") == std::optional<std::string>("001"));
    CHECK(ConnectionEnvValue("PASSWORD") == std::optional<std::string>("p"));
    CHECK(ConnectionEnvValue("LANGUAGE") == std::optional<std::string>("DE"));
}

TEST_CASE("ConnectionEnvNames: reports both spellings for a setting", "[core][env]") {
    auto names = ConnectionEnvNames("HOST");
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "ERPL_ADT_HOST");
    CHECK(names[1] == "SAP_HOST");
}

// ---------------------------------------------------------------------------
// A 401 must say where credentials came from: a stale .adt.creds outliving a
// system restart is the failure the hint exists for (issue #41).
// ---------------------------------------------------------------------------

TEST_CASE("FromHttpStatus: 401 explains credential resolution", "[core][env]") {
    auto error = Error::FromHttpStatus("Get", "/sap/bc/adt/discovery", 401);
    CHECK(error.category == ErrorCategory::Authentication);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find(".adt.creds") != std::string::npos);
    CHECK(error.hint->find("ERPL_ADT_PASSWORD") != std::string::npos);
}
