#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace erpl_adt {

// ===========================================================================
// Telemetry facade over the shared DataZooDE/posthog-telemetry C++ library.
//
// Every event follows the shared DataZoo schema (telemetry_schema 2): a common
// envelope (product=erpl_adt, version, edition, os/arch, is_ci, is_container,
// $session_id, $groups) plus bounded, enumerated event properties. The
// distinct_id is the SHA-256 of the OS machine id — identical to erpl / flapi /
// anofox-statistics, so one machine correlates across the whole DataZoo stack.
//
// HARD RULE — no PII, ever. Property values may ONLY be values from a fixed,
// code-controlled enumeration (object_type, op, outcome, kind, tool name, …) or
// numeric buckets/durations. NEVER an ABAP object/package/table/CDS name, source
// code, transport id, search pattern, SAP host/user, file path, or ADT error
// text. Use the classifier helpers below so only bounded values are ever built.
//
// Opt-out (any one disables both CLI and MCP paths, enforced at the transport):
//   - --no-telemetry CLI flag        (passed as user_disabled)
//   - DATAZOO_DISABLE_TELEMETRY=1|true|yes   (cross-product kill-switch)
//   - DO_NOT_TRACK=1|true|yes                (https://consoledonottrack.com/)
//   - ERPL_ADT_NO_TELEMETRY=1|true|yes       (product-local, legacy)
// ===========================================================================

// Envelope discriminator: one-shot CLI vs. long-lived MCP server.
enum class InstallKind { Cli, Server };

// A single bounded, non-PII telemetry property.
struct TelemetryProp {
    enum class Kind { Str, Num, Bool };
    std::string key;
    Kind        kind = Kind::Str;
    std::string str;            // Kind::Str  — MUST be an enumerated value
    long long   num = 0;        // Kind::Num  — a bucket/duration/count
    bool        boolean = false; // Kind::Bool

    static TelemetryProp Str(std::string k, std::string v);
    static TelemetryProp Num(std::string k, long long v);
    static TelemetryProp Bool(std::string k, bool v);
};
using TelemetryProps = std::vector<TelemetryProp>;

class Telemetry {
public:
    // Initialise once at process start. Applies every opt-out check, sets the
    // product envelope, associates the `deployment` group, records install_kind,
    // registers the at-exit flush safety net, and prints a one-line first-run
    // notice (to stderr) pointing at TELEMETRY.md. No-op if already initialised.
    static void Initialize(bool user_disabled, const std::string& version,
                           InstallKind kind);

    static bool IsEnabled() noexcept;

    // ---- Lifecycle events -------------------------------------------------
    // cli_started { command, args_shape, install_kind }
    // `command` is the subcommand/group name; `args_shape` lists WHICH flags were
    // present (never their values).
    static void CliStarted(const std::string& command,
                           const std::string& args_shape);

    // server_started { transport, tool_count, install_kind }
    static void ServerStarted(const std::string& transport, int tool_count);

    // ---- Feature + error --------------------------------------------------
    // feature_used { feature, <props…>, install_kind }. `feature` MUST be one of
    // the erpl_adt::feature::* constants. `props` MUST be enum/number only.
    static void Feature(const std::string& feature, TelemetryProps props = {});

    // $exception { error_class, feature?, install_kind }. `error_class` MUST be
    // one of erpl_adt::error_class::* — never a message or identifier.
    static void Error(const std::string& error_class,
                      const std::string& feature = "");

    // Client-side sampling rate in [0,1] for very high tool-call volume; the
    // effective rate is stamped on events so counts scale back up.
    static void SetSampling(double rate);

    // Synchronously drain buffered events (bounded). MUST be called before every
    // exit path and on SIGTERM: the library's at-exit handler discards by design.
    static void Flush();

    // Test seam: when set, captured events are delivered here (event name +
    // flattened string properties) and NOT sent over the network. Pass nullptr
    // to restore normal behaviour.
    using TestSink = std::function<void(
        const std::string& event,
        const std::vector<std::pair<std::string, std::string>>& props)>;
    static void SetSinkForTesting(TestSink sink);

    // Test seam: force-set the enabled flag without re-running Initialize.
    static void SetEnabledForTesting(bool enabled);

    // Test seam: clear the once-only init latch so a later Initialize() takes
    // effect again (lets a test exercise opt-out permutations).
    static void ResetForTesting();
};

// ===========================================================================
// Fixed enumerations. Extend ONLY by adding members — never emit a free-form
// value. These are the sole allowed property values (§ cardinality rule).
// ===========================================================================
namespace feature {
inline constexpr const char* kSearch        = "search";
inline constexpr const char* kObjectRead    = "object_read";
inline constexpr const char* kObjectWrite   = "object_write";
inline constexpr const char* kSourceRead    = "source_read";
inline constexpr const char* kSourceWrite   = "source_write";
inline constexpr const char* kActivate      = "activate";
inline constexpr const char* kAbapunitRun   = "abapunit_run";
inline constexpr const char* kAtcRun        = "atc_run";
inline constexpr const char* kTransportOp   = "transport_op";
inline constexpr const char* kDdicRead      = "ddic_read";
inline constexpr const char* kPackageRead   = "package_read";
inline constexpr const char* kDeployRun     = "deploy_run";
inline constexpr const char* kMcpToolCalled = "mcp_tool_called";
} // namespace feature

namespace error_class {
inline constexpr const char* kAdtHttpError    = "adt_http_error";
inline constexpr const char* kAuthError       = "auth_error";
inline constexpr const char* kLockError       = "lock_error";
inline constexpr const char* kActivationError = "activation_error";
inline constexpr const char* kNotFound        = "not_found";
inline constexpr const char* kTestFailure     = "test_failure";
inline constexpr const char* kAtcError        = "atc_error";
inline constexpr const char* kTransportError  = "transport_error";
inline constexpr const char* kTimeout         = "timeout";
inline constexpr const char* kUsageError      = "usage_error";
inline constexpr const char* kInternalError   = "internal_error";
} // namespace error_class

// ===========================================================================
// Classifier helpers — the ONLY sanctioned way to derive property values from
// runtime data. Each maps an arbitrary (possibly PII-bearing) input to a value
// from a fixed, small enumeration, so no identifier can ever reach a property.
// ===========================================================================

// ADT object type (e.g. "CLAS/OC", "PROG/P", "DDLS/DF", a bare "CLAS", a URI, …)
// -> {class, program, function, cds, table, package, other}. Never echoes input.
std::string ClassifyObjectType(const std::string& adt_type);

// Bucket a raw count into {"0","1-10","11-100","100+"} — never the exact number.
std::string BucketCount(long long count);

// Map a process exit code (see CLAUDE.md exit-code table) to an enumerated
// error_class, or return "" for success (0). Used for central $exception capture.
std::string ErrorClassForExitCode(int exit_code);

} // namespace erpl_adt
