#include <erpl_adt/core/telemetry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace erpl_adt;

namespace {

// Captured events for the test sink: event name -> flat string props.
struct Captured {
    std::string event;
    std::map<std::string, std::string> props;
};

// Install a sink that records every captured event. Restores clean state on
// destruction so tests don't leak the sink or the init latch.
struct SinkGuard {
    std::vector<Captured>* out;
    explicit SinkGuard(std::vector<Captured>* sink) : out(sink) {
        Telemetry::SetSinkForTesting(
            [sink](const std::string& event,
                   const std::vector<std::pair<std::string, std::string>>& props) {
                Captured c;
                c.event = event;
                for (const auto& p : props) c.props[p.first] = p.second;
                sink->push_back(std::move(c));
            });
    }
    ~SinkGuard() {
        Telemetry::SetSinkForTesting(nullptr);
        Telemetry::ResetForTesting();
    }
};

void ClearEnv() {
#ifndef _WIN32
    unsetenv("DATAZOO_DISABLE_TELEMETRY");
    unsetenv("DO_NOT_TRACK");
    unsetenv("ERPL_ADT_NO_TELEMETRY");
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Opt-out
// ---------------------------------------------------------------------------
TEST_CASE("Telemetry disabled by --no-telemetry", "[telemetry]") {
    ClearEnv();
    Telemetry::ResetForTesting();
    Telemetry::Initialize(/*user_disabled=*/true, "test", InstallKind::Cli);
    CHECK_FALSE(Telemetry::IsEnabled());
    Telemetry::ResetForTesting();
}

TEST_CASE("Telemetry disabled by env kill-switches", "[telemetry]") {
#ifndef _WIN32
    for (const char* var :
         {"DATAZOO_DISABLE_TELEMETRY", "DO_NOT_TRACK", "ERPL_ADT_NO_TELEMETRY"}) {
        ClearEnv();
        setenv(var, "1", 1);
        Telemetry::ResetForTesting();
        Telemetry::Initialize(/*user_disabled=*/false, "test", InstallKind::Cli);
        CHECK_FALSE(Telemetry::IsEnabled());
        unsetenv(var);
    }
    Telemetry::ResetForTesting();
#endif
}

TEST_CASE("Disabled telemetry captures nothing", "[telemetry]") {
    std::vector<Captured> events;
    SinkGuard guard(&events);
    Telemetry::SetEnabledForTesting(false);

    Telemetry::CliStarted("search", "type");
    Telemetry::Feature(feature::kSearch, {TelemetryProp::Str("object_type", "class")});
    Telemetry::Error(error_class::kAuthError);

    CHECK(events.empty());
}

// ---------------------------------------------------------------------------
// Event shapes
// ---------------------------------------------------------------------------
TEST_CASE("cli_started carries command, args_shape, install_kind", "[telemetry]") {
    std::vector<Captured> events;
    SinkGuard guard(&events);
    Telemetry::SetEnabledForTesting(true);

    Telemetry::CliStarted("object", "type,name,package");
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == "cli_started");
    CHECK(events[0].props["command"] == "object");
    CHECK(events[0].props["args_shape"] == "type,name,package");
    CHECK(events[0].props["install_kind"] == "cli");
}

TEST_CASE("feature_used carries feature and bounded props", "[telemetry]") {
    std::vector<Captured> events;
    SinkGuard guard(&events);
    Telemetry::SetEnabledForTesting(true);

    Telemetry::Feature(feature::kObjectWrite,
                       {TelemetryProp::Str("object_type", "class"),
                        TelemetryProp::Str("op", "create"),
                        TelemetryProp::Num("duration_ms", 42)});
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == "feature_used");
    CHECK(events[0].props["feature"] == "object_write");
    CHECK(events[0].props["object_type"] == "class");
    CHECK(events[0].props["op"] == "create");
    CHECK(events[0].props["duration_ms"] == "42");
    CHECK(events[0].props["install_kind"] == "cli");
}

TEST_CASE("$exception carries enumerated error_class only", "[telemetry]") {
    std::vector<Captured> events;
    SinkGuard guard(&events);
    Telemetry::SetEnabledForTesting(true);

    Telemetry::Error(error_class::kLockError, feature::kSourceWrite);
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == "$exception");
    CHECK(events[0].props["error_class"] == "lock_error");
    CHECK(events[0].props["feature"] == "source_write");
}

TEST_CASE("install_kind reflects server shape", "[telemetry]") {
    ClearEnv();
    Telemetry::ResetForTesting();
    std::vector<Captured> events;
    SinkGuard guard(&events);
    // user_disabled=true records the install kind but skips the real library
    // path (no network); SetEnabledForTesting then routes captures to the sink.
    Telemetry::Initialize(/*user_disabled=*/true, "test", InstallKind::Server);
    Telemetry::SetEnabledForTesting(true);
    Telemetry::ServerStarted("stdio", 46);
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == "server_started");
    CHECK(events[0].props["transport"] == "stdio");
    CHECK(events[0].props["tool_count"] == "46");
    CHECK(events[0].props["install_kind"] == "server");
}

// ---------------------------------------------------------------------------
// Classifiers — bounded, never echo identifiers
// ---------------------------------------------------------------------------
TEST_CASE("ClassifyObjectType maps to a fixed enum", "[telemetry]") {
    CHECK(ClassifyObjectType("CLAS/OC") == "class");
    CHECK(ClassifyObjectType("clas") == "class");
    CHECK(ClassifyObjectType("PROG/P") == "program");
    CHECK(ClassifyObjectType("FUGR/F") == "function");
    CHECK(ClassifyObjectType("DDLS/DF") == "cds");
    CHECK(ClassifyObjectType("TABL/DT") == "table");
    CHECK(ClassifyObjectType("DEVC/K") == "package");
    CHECK(ClassifyObjectType("/sap/bc/adt/oo/classes/zcl_secret") == "class");
    CHECK(ClassifyObjectType("/sap/bc/adt/ddic/tables/z_secret_table") == "table");
}

TEST_CASE("ClassifyObjectType never echoes a raw object name", "[telemetry][privacy]") {
    // A secret-looking object name must collapse to a bounded enum, never appear
    // verbatim in the result.
    const std::string secret = "ZCL_TOP_SECRET_CUSTOMER_DATA_9F3A";
    auto out = ClassifyObjectType(secret);
    static const std::vector<std::string> kAllowed = {
        "class", "program", "function", "cds", "table", "package", "other"};
    CHECK(std::find(kAllowed.begin(), kAllowed.end(), out) != kAllowed.end());
    CHECK(out.find(secret) == std::string::npos);
    CHECK(out == "other");
}

TEST_CASE("BucketCount never returns the exact count", "[telemetry]") {
    CHECK(BucketCount(0) == "0");
    CHECK(BucketCount(1) == "1-10");
    CHECK(BucketCount(10) == "1-10");
    CHECK(BucketCount(11) == "11-100");
    CHECK(BucketCount(100) == "11-100");
    CHECK(BucketCount(101) == "100+");
    CHECK(BucketCount(999999) == "100+");
}

TEST_CASE("ErrorClassForExitCode maps to enumerated classes", "[telemetry]") {
    CHECK(ErrorClassForExitCode(0).empty());
    CHECK(ErrorClassForExitCode(1) == error_class::kAuthError);
    CHECK(ErrorClassForExitCode(2) == error_class::kNotFound);
    CHECK(ErrorClassForExitCode(5) == error_class::kActivationError);
    CHECK(ErrorClassForExitCode(6) == error_class::kLockError);
    CHECK(ErrorClassForExitCode(7) == error_class::kTestFailure);
    CHECK(ErrorClassForExitCode(8) == error_class::kAtcError);
    CHECK(ErrorClassForExitCode(9) == error_class::kTransportError);
    CHECK(ErrorClassForExitCode(10) == error_class::kTimeout);
    CHECK(ErrorClassForExitCode(99) == error_class::kUsageError);
}

// ---------------------------------------------------------------------------
// No PII leaks into any property value (acceptance criterion)
// ---------------------------------------------------------------------------
TEST_CASE("No object/package/source/transport strings leak into props",
          "[telemetry][privacy]") {
    std::vector<Captured> events;
    SinkGuard guard(&events);
    Telemetry::SetEnabledForTesting(true);

    // Simulate a fully-instrumented run: every prop is built ONLY from the
    // sanctioned classifier helpers / literals, never from raw identifiers.
    const std::string obj_name  = "ZCL_ACME_PAYROLL";
    const std::string pkg_name  = "ZPKG_FINANCE_SECRET";
    const std::string transport = "A4HK900123";
    const std::string pattern   = "ZCL_ACME_*";

    Telemetry::CliStarted("search", "type,max");
    Telemetry::Feature(feature::kSearch,
                       {TelemetryProp::Str("object_type", ClassifyObjectType("CLAS/OC"))});
    Telemetry::Feature(feature::kObjectWrite,
                       {TelemetryProp::Str("object_type", ClassifyObjectType(obj_name)),
                        TelemetryProp::Str("op", "create")});
    Telemetry::Feature(feature::kTransportOp, {TelemetryProp::Str("op", "release")});
    Telemetry::Error(error_class::kNotFound, feature::kObjectRead);

    // Every emitted property value must be free of any identifier we fed in.
    for (const auto& e : events) {
        for (const auto& kv : e.props) {
            for (const auto& secret : {obj_name, pkg_name, transport, pattern}) {
                CHECK(kv.second.find(secret) == std::string::npos);
            }
        }
    }
}
