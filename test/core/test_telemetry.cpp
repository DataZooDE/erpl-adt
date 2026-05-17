#include <erpl_adt/core/telemetry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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

// Regression: the future returned by TrackCommand must not block the caller
// at destruction. Prior implementation used std::async(std::launch::async, ...)
// whose destructor blocks until the task completes — defeating the wait_for
// timeout in main() and stretching CLI exit to the full HTTP timeout (~8s)
// whenever the PostHog endpoint is slow.
TEST_CASE("TrackCommand future destructor must not block on slow backend",
          "[telemetry][hardening]") {
    BackendGuard guard;

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

    // Slow backend: sleeps for 1 second, then signals completion. Uses a
    // shared_ptr so the test can outlive the lambda capture lifetime safely.
    struct Signal {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
    };
    auto sig = std::make_shared<Signal>();

    Telemetry::SetBackendForTesting([sig](const std::string&, const std::string&) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(sig->m);
        sig->done = true;
        sig->cv.notify_all();
    });

    const auto start = std::chrono::steady_clock::now();
    {
        auto fut = Telemetry::TrackCommand("bench", "destructor");
        REQUIRE(fut.valid());
        // Don't wait. Let the future go out of scope. With a blocking-destructor
        // future (std::async launch::async), this scope-exit takes ~1s. With a
        // detached worker, it should be near-instantaneous.
        (void)fut.wait_for(std::chrono::milliseconds(0));
    }
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

    // Generous bound: 200 ms is far below the 1-second sleep but safely above
    // CI noise. A blocking destructor would take >= 1000 ms.
    CHECK(elapsed_ms < 200);

    // Now wait for the detached worker to finish so the BackendGuard can
    // safely reset the backend (the worker captures `sig` by shared_ptr, so
    // it is safe even if we time out below).
    {
        std::unique_lock<std::mutex> lock(sig->m);
        sig->cv.wait_for(lock, std::chrono::seconds(5),
                         [&] { return sig->done; });
    }
}
