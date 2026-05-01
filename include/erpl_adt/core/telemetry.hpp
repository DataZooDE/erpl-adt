#pragma once

#include <functional>
#include <future>
#include <string>

namespace erpl_adt {

// Lightweight, fire-and-forget telemetry that sends command+subcommand events
// to the Datazoo PostHog project. All events are anonymous (SHA256 of machine
// ID). No parameters, paths, or credentials are ever included.
//
// Opt-out (checked in order):
//   1. --no-telemetry CLI flag → call Initialize(/*user_disabled=*/true, ...)
//   2. ERPL_ADT_NO_TELEMETRY=1|true|yes  env var
//   3. DATAZOO_DISABLE_TELEMETRY=1|true|yes  env var (cross-product)
class Telemetry {
public:
    // Call once at program start. Applies all opt-out checks.
    static void Initialize(bool user_disabled, const std::string& version);

    // Fire an async HTTP event for group+action (e.g. "search", "query").
    // Returns a future; main() should wait_for(2s) before exiting so the
    // HTTP call can complete concurrently with the command itself.
    // Returns an invalid future when telemetry is disabled.
    [[nodiscard]] static std::future<void> TrackCommand(const std::string& group,
                                                        const std::string& action);

    static bool IsEnabled() noexcept;

    // Replace the HTTP backend for unit tests (pass nullptr to restore default).
    using Backend = std::function<void(const std::string& group,
                                       const std::string& action)>;
    static void SetBackendForTesting(Backend backend);

private:
    static std::string GetDistinctId();
    static std::string DetectPlatform();
    static void SendToPostHog(const std::string& group, const std::string& action);
};

} // namespace erpl_adt
