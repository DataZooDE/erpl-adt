#include <erpl_adt/core/telemetry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace erpl_adt;

namespace {

// Restore the default (real) backend after each test so tests don't leak state.
struct BackendGuard {
    ~BackendGuard() { Telemetry::SetBackendForTesting(nullptr); }
};

} // namespace

TEST_CASE("Telemetry is disabled when user passes --no-telemetry", "[telemetry]") {
    Telemetry::Initialize(/*user_disabled=*/true, "test");
    REQUIRE_FALSE(Telemetry::IsEnabled());

    auto fut = Telemetry::TrackCommand("search", "query");
    REQUIRE_FALSE(fut.valid());
}

TEST_CASE("Telemetry is disabled when ERPL_ADT_NO_TELEMETRY=1", "[telemetry]") {
#ifdef _WIN32
    _putenv_s("ERPL_ADT_NO_TELEMETRY", "1");
#else
    setenv("ERPL_ADT_NO_TELEMETRY", "1", 1);
#endif
    Telemetry::Initialize(/*user_disabled=*/false, "test");
    REQUIRE_FALSE(Telemetry::IsEnabled());

#ifdef _WIN32
    _putenv_s("ERPL_ADT_NO_TELEMETRY", "");
#else
    unsetenv("ERPL_ADT_NO_TELEMETRY");
#endif
}

TEST_CASE("Telemetry is disabled when DATAZOO_DISABLE_TELEMETRY=1", "[telemetry]") {
#ifdef _WIN32
    _putenv_s("DATAZOO_DISABLE_TELEMETRY", "1");
#else
    setenv("DATAZOO_DISABLE_TELEMETRY", "1", 1);
#endif
    Telemetry::Initialize(/*user_disabled=*/false, "test");
    REQUIRE_FALSE(Telemetry::IsEnabled());

#ifdef _WIN32
    _putenv_s("DATAZOO_DISABLE_TELEMETRY", "");
#else
    unsetenv("DATAZOO_DISABLE_TELEMETRY");
#endif
}

TEST_CASE("TrackCommand delivers group and action to test backend", "[telemetry]") {
    BackendGuard guard;

    // Temporarily unset env-level kill-switches so Initialize enables telemetry.
    // The real HTTP call is blocked by the test backend, not the kill-switch.
#ifndef _WIN32
    const char* saved_dz = getenv("DATAZOO_DISABLE_TELEMETRY");
    const char* saved_ea = getenv("ERPL_ADT_NO_TELEMETRY");
    unsetenv("DATAZOO_DISABLE_TELEMETRY");
    unsetenv("ERPL_ADT_NO_TELEMETRY");
#endif

    Telemetry::Initialize(/*user_disabled=*/false, "0.0.0-test");

#ifndef _WIN32
    if (saved_dz) setenv("DATAZOO_DISABLE_TELEMETRY", saved_dz, 1);
    if (saved_ea) setenv("ERPL_ADT_NO_TELEMETRY", saved_ea, 1);
#endif

    REQUIRE(Telemetry::IsEnabled());

    std::string captured_group;
    std::string captured_action;
    Telemetry::SetBackendForTesting([&](const std::string& g, const std::string& a) {
        captured_group  = g;
        captured_action = a;
    });

    auto fut = Telemetry::TrackCommand("ddic", "table");
    REQUIRE(fut.valid());
    fut.wait_for(std::chrono::seconds(5));

    CHECK(captured_group  == "ddic");
    CHECK(captured_action == "table");
}

TEST_CASE("TrackCommand is a no-op when telemetry is disabled", "[telemetry]") {
    BackendGuard guard;

    Telemetry::Initialize(/*user_disabled=*/true, "0.0.0-test");
    REQUIRE_FALSE(Telemetry::IsEnabled());

    bool called = false;
    Telemetry::SetBackendForTesting([&](const std::string&, const std::string&) {
        called = true;
    });

    auto fut = Telemetry::TrackCommand("search", "query");
    REQUIRE_FALSE(fut.valid());
    REQUIRE_FALSE(called);
}
