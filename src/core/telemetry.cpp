#include <erpl_adt/core/telemetry.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

#if defined(ERPL_ADT_TELEMETRY_ENABLED)
#include <telemetry.hpp>   // DataZooDE/posthog-telemetry (namespace duckdb)
#endif

namespace erpl_adt {

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_initialized{false};
InstallKind       g_kind = InstallKind::Cli;
std::mutex        g_mutex;
Telemetry::TestSink g_sink;

bool EnvTrue(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    std::string s{v};
    return s == "1" || s == "true" || s == "yes";
}

const char* InstallKindStr(InstallKind k) {
    return k == InstallKind::Server ? "server" : "cli";
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Print a one-line, first-run-only notice pointing at TELEMETRY.md. Persisted by
// a marker file so it shows at most once per machine/user. Best-effort: any I/O
// failure just means it may print again — never fatal, never blocks.
void MaybePrintFirstRunNotice() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    if (!home || !*home) return;  // no place to persist — stay silent, don't spam
    std::string dir  = std::string(home) + "/.erpl-adt";
    std::string mark = dir + "/telemetry-notice-shown";
    {
        std::ifstream existing(mark);
        if (existing.good()) return;  // already shown
    }
    // Attempt to create the marker (mkdir is best-effort via ofstream in an
    // existing dir; if the dir is missing the write fails silently and the
    // notice may show again — acceptable).
    std::cerr << "erpl-adt collects anonymous usage telemetry (no code, names, "
                 "or identifiers). Opt out with --no-telemetry or "
                 "DATAZOO_DISABLE_TELEMETRY=1. See TELEMETRY.md.\n";
    std::ofstream(mark) << "1\n";  // if dir missing this no-ops harmlessly
    // Fallback: also try to create the directory then the marker.
    if (!std::ifstream(mark).good()) {
        std::system(("mkdir -p " + dir + " 2>/dev/null").c_str());
        std::ofstream(mark) << "1\n";
    }
}

// Flatten props for the test sink.
std::vector<std::pair<std::string, std::string>> Flatten(const TelemetryProps& props) {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(props.size());
    for (const auto& p : props) {
        switch (p.kind) {
            case TelemetryProp::Kind::Str:  out.emplace_back(p.key, p.str); break;
            case TelemetryProp::Kind::Num:  out.emplace_back(p.key, std::to_string(p.num)); break;
            case TelemetryProp::Kind::Bool: out.emplace_back(p.key, p.boolean ? "true" : "false"); break;
        }
    }
    return out;
}

#if defined(ERPL_ADT_TELEMETRY_ENABLED)
duckdb::PropertyMap ToLibraryProps(const TelemetryProps& props) {
    duckdb::PropertyMap m;
    for (const auto& p : props) {
        switch (p.kind) {
            case TelemetryProp::Kind::Str:  m[p.key] = duckdb::PropertyValue(p.str); break;
            case TelemetryProp::Kind::Num:  m[p.key] = duckdb::PropertyValue(static_cast<long long>(p.num)); break;
            case TelemetryProp::Kind::Bool: m[p.key] = duckdb::PropertyValue(p.boolean); break;
        }
    }
    return m;
}
#endif

// Central capture: injects install_kind, honours the test sink and enabled flag,
// then forwards to the shared library. `event` is one of cli_started /
// server_started / feature_used / $exception.
void Capture(const std::string& event, TelemetryProps props) {
    if (!g_enabled.load()) return;

    // install_kind rides on every event (the library envelope has no such field).
    props.push_back(TelemetryProp::Str("install_kind", InstallKindStr(g_kind)));

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_sink) {
            g_sink(event, Flatten(props));
            return;  // tests never hit the network
        }
    }

#if defined(ERPL_ADT_TELEMETRY_ENABLED)
    auto& t = duckdb::PostHogTelemetry::Instance();
    auto m = ToLibraryProps(props);
    if (event == "feature_used") {
        // feature name is already in props as "feature"; the library's
        // CaptureFeature expects it as the first arg.
        std::string feature;
        for (const auto& p : props) {
            if (p.key == "feature") { feature = p.str; break; }
        }
        m.erase("feature");
        t.CaptureFeature(feature, std::move(m));
    } else if (event == "$exception") {
        std::string ec;
        for (const auto& p : props) {
            if (p.key == "error_class") { ec = p.str; break; }
        }
        m.erase("error_class");
        t.CaptureError(ec, std::move(m));
    } else {
        t.Capture(event, std::move(m));
    }
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// TelemetryProp factories
// ---------------------------------------------------------------------------
TelemetryProp TelemetryProp::Str(std::string k, std::string v) {
    TelemetryProp p; p.kind = Kind::Str; p.key = std::move(k); p.str = std::move(v); return p;
}
TelemetryProp TelemetryProp::Num(std::string k, long long v) {
    TelemetryProp p; p.kind = Kind::Num; p.key = std::move(k); p.num = v; return p;
}
TelemetryProp TelemetryProp::Bool(std::string k, bool v) {
    TelemetryProp p; p.kind = Kind::Bool; p.key = std::move(k); p.boolean = v; return p;
}

// ---------------------------------------------------------------------------
// Telemetry facade
// ---------------------------------------------------------------------------
void Telemetry::Initialize(bool user_disabled, const std::string& version,
                           InstallKind kind) {
    if (g_initialized.exchange(true)) return;  // once only
    g_kind = kind;

    const bool disabled = user_disabled ||
                          EnvTrue("DATAZOO_DISABLE_TELEMETRY") ||
                          EnvTrue("DO_NOT_TRACK") ||
                          EnvTrue("ERPL_ADT_NO_TELEMETRY");
    if (disabled) {
        g_enabled.store(false);
#if defined(ERPL_ADT_TELEMETRY_ENABLED)
        duckdb::PostHogTelemetry::Instance().SetEnabled(false);
#endif
        return;
    }

    g_enabled.store(true);

#if defined(ERPL_ADT_TELEMETRY_ENABLED)
    auto& t = duckdb::PostHogTelemetry::Instance();
    t.SetEnabled(true);
    // product / product_version / product_edition envelope. erpl-adt ships a
    // single OSS edition today; if a licensed edition is ever added, pass
    // "enterprise" here and AssociateGroup("account", sha256(license_id)).
    t.SetProduct("erpl_adt", version, "oss");
    // Deployment-level analytics (retention, active installs). Key = distinct_id
    // (SHA-256 of the machine id) — the same pseudonymous id the whole stack uses.
    t.AssociateGroup("deployment", duckdb::PostHogTelemetry::GetDistinctId());
#else
    (void)version;
#endif

    MaybePrintFirstRunNotice();
}

bool Telemetry::IsEnabled() noexcept { return g_enabled.load(); }

void Telemetry::CliStarted(const std::string& command,
                           const std::string& args_shape) {
    TelemetryProps p;
    p.push_back(TelemetryProp::Str("command", command));
    p.push_back(TelemetryProp::Str("args_shape", args_shape));
    Capture("cli_started", std::move(p));
}

void Telemetry::ServerStarted(const std::string& transport, int tool_count) {
    TelemetryProps p;
    p.push_back(TelemetryProp::Str("transport", transport));
    p.push_back(TelemetryProp::Num("tool_count", tool_count));
    Capture("server_started", std::move(p));
}

void Telemetry::Feature(const std::string& feature, TelemetryProps props) {
    props.push_back(TelemetryProp::Str("feature", feature));
    Capture("feature_used", std::move(props));
}

void Telemetry::Error(const std::string& error_class, const std::string& feature) {
    TelemetryProps p;
    p.push_back(TelemetryProp::Str("error_class", error_class));
    if (!feature.empty()) p.push_back(TelemetryProp::Str("feature", feature));
    Capture("$exception", std::move(p));
}

void Telemetry::SetSampling(double rate) {
#if defined(ERPL_ADT_TELEMETRY_ENABLED)
    duckdb::PostHogTelemetry::Instance().SetSampling(rate);
#else
    (void)rate;
#endif
}

void Telemetry::Flush() {
    if (!g_enabled.load()) return;
#if defined(ERPL_ADT_TELEMETRY_ENABLED)
    duckdb::PostHogTelemetry::Instance().Flush();
#endif
}

void Telemetry::SetSinkForTesting(TestSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

void Telemetry::SetEnabledForTesting(bool enabled) {
    g_enabled.store(enabled);
}

void Telemetry::ResetForTesting() {
    g_initialized.store(false);
    g_enabled.store(false);
    g_kind = InstallKind::Cli;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = nullptr;
}

// ---------------------------------------------------------------------------
// Classifiers — the only sanctioned path from runtime data to a property value.
// ---------------------------------------------------------------------------
std::string ClassifyObjectType(const std::string& adt_type) {
    // ADT URIs: classify by the fixed path token only — never the object name.
    if (adt_type.find("/sap/bc/adt/") != std::string::npos ||
        adt_type.find('/') == 0) {
        if (adt_type.find("/oo/classes")    != std::string::npos) return "class";
        if (adt_type.find("/oo/interfaces") != std::string::npos) return "class";
        if (adt_type.find("/programs")      != std::string::npos) return "program";
        if (adt_type.find("/functions")     != std::string::npos) return "function";
        if (adt_type.find("/ddic/ddlsources") != std::string::npos) return "cds";
        if (adt_type.find("/ddic/tables")   != std::string::npos) return "table";
        if (adt_type.find("/packages")      != std::string::npos) return "package";
        if (adt_type.find("/vit/wb/object_type/devck") != std::string::npos) return "package";
    }

    // ABAP type code, possibly "CLAS/OC" or "clas". Take the prefix before '/'.
    std::string code = ToUpper(adt_type);
    auto slash = code.find('/');
    if (slash != std::string::npos) code = code.substr(0, slash);

    if (code == "CLAS" || code == "INTF")            return "class";
    if (code == "PROG" || code == "REPS")            return "program";
    if (code == "FUGR" || code == "FUNC")            return "function";
    if (code == "DDLS" || code == "DCLS" || code == "DDLX" ||
        code == "BDEF" || code == "SRVD" || code == "SRVB") return "cds";
    if (code == "TABL")                              return "table";
    if (code == "DEVC")                              return "package";
    return "other";
}

std::string BucketCount(long long count) {
    if (count <= 0)   return "0";
    if (count <= 10)  return "1-10";
    if (count <= 100) return "11-100";
    return "100+";
}

std::string ErrorClassForExitCode(int exit_code) {
    switch (exit_code) {
        case 0:  return "";
        case 1:  return error_class::kAuthError;       // connection/auth
        case 2:  return error_class::kNotFound;        // package/not found
        case 3:  return error_class::kAdtHttpError;    // clone
        case 4:  return error_class::kAdtHttpError;    // pull
        case 5:  return error_class::kActivationError; // activation
        case 6:  return error_class::kLockError;       // lock conflict
        case 7:  return error_class::kTestFailure;     // test failure
        case 8:  return error_class::kAtcError;        // ATC check error
        case 9:  return error_class::kTransportError;  // transport error
        case 10: return error_class::kTimeout;         // timeout
        case 99: return error_class::kUsageError;      // usage/validation/internal
        default: return error_class::kInternalError;
    }
}

} // namespace erpl_adt
