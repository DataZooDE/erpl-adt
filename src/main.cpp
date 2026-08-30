#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/adt/xml_codec.hpp>
#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>
#include <erpl_adt/config/config_loader.hpp>
#include <erpl_adt/core/connection_env.hpp>
#include <erpl_adt/core/log.hpp>
#include <erpl_adt/core/telemetry.hpp>
#include <erpl_adt/core/terminal.hpp>
#include <erpl_adt/core/version.hpp>
#include <erpl_adt/mcp/mcp_server.hpp>
#include <erpl_adt/mcp/catalog_tool_handlers.hpp>
#include <erpl_adt/mcp/http_security.hpp>
#include <erpl_adt/mcp/mcp_http_server.hpp>
#include <erpl_adt/mcp/mcp_tool_handlers.hpp>
#include <erpl_adt/storage/duckdb_catalog_store.hpp>
#include <erpl_adt/workflow/deploy_workflow.hpp>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <csignal>
#include <vector>
#include "datazoo_banner.hpp"

namespace {

// Drain telemetry on SIGTERM/SIGINT so a killed `mcp` server doesn't lose the
// session's tail (the library's at-exit handler discards by design). Flush is
// best-effort and bounded; we then terminate promptly.
void McpTelemetrySignalHandler(int sig) {
    erpl_adt::Telemetry::Flush();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// Exit codes per spec section 7.
constexpr int kExitSuccess    = 0;
constexpr int kExitConnection = 1;
constexpr int kExitPackage    = 2;
constexpr int kExitClone      = 3;
constexpr int kExitPull       = 4;
constexpr int kExitActivation = 5;
constexpr int kExitTimeout    = 10;
constexpr int kExitInternal   = 99;

bool ParseIntInRange(std::string_view raw,
                     int min_value,
                     int max_value,
                     const char* field_name,
                     int* out,
                     std::string* error) {
    if (raw.empty()) {
        if (error) {
            *error = std::string("Missing ") + field_name;
        }
        return false;
    }
    std::string s(raw);
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE ||
        value < min_value || value > max_value) {
        if (error) {
            *error = std::string("Invalid ") + field_name + ": " + s;
        }
        return false;
    }
    if (out) {
        *out = static_cast<int>(value);
    }
    return true;
}

bool ParsePort(std::string_view raw, uint16_t* out, std::string* error) {
    int value = 0;
    if (!ParseIntInRange(raw, 1, 65535, "--port", &value, error)) {
        return false;
    }
    if (out) {
        *out = static_cast<uint16_t>(value);
    }
    return true;
}

void PrintMcpHelp(std::ostream& out) {
    out << "erpl-adt mcp - Start MCP server (JSON-RPC over stdio)\n\n";
    out << "USAGE\n";
    out << "  erpl-adt mcp [global-flags]\n\n";
    out << "GLOBAL FLAGS\n";
    out << "  --host <host>        SAP hostname (default: localhost)\n";
    out << "  --port <port>        SAP port (default: 50000)\n";
    out << "  --user <user>        SAP username (default: DEVELOPER)\n";
    out << "  --password <pass>    SAP password\n";
    out << "  --password-env <var> Read password from env var (default: SAP_PASSWORD)\n";
    out << "  --client <num>       SAP client (default: 001)\n";
    out << "  --language <iso>     SAP logon language (2-letter ISO, e.g. EN, DE; default: EN)\n";
    out << "  --https              Use HTTPS\n";
    out << "  --insecure           Skip TLS verification (with --https)\n";
    out << "  --timeout <sec>      Request timeout in seconds\n";
    out << "  -v                   Verbose logging (INFO level)\n";
    out << "  -vv                  Debug logging (DEBUG level)\n\n";
    out << "TRANSPORT\n";
    out << "  --http               Serve JSON-RPC over HTTP at POST /mcp instead of stdio\n";
    out << "  --mcp-host <addr>    Address to bind with --http (default: 127.0.0.1)\n";
    out << "  --mcp-port <n>       Port to bind with --http (default: 8383)\n";
    out << "  --catalog-db <path>  Also expose the catalog_* tools over a DuckDB cache\n\n";
    out << "HTTP ACCESS CONTROL (--http only)\n";
    out << "  --cors-origin <list> Comma-separated extra origins allowed to call /mcp.\n";
    out << "                       Same-origin, loopback and non-browser (no Origin\n";
    out << "                       header) requests are always allowed; anything else is\n";
    out << "                       refused with 403. '*' allows every origin.\n";
    out << "  --auth-token <tok>   Require 'Authorization: Bearer <tok>' on /mcp\n";
    out << "  --auth-token-env <v> Read that token from an environment variable\n";
}

// Map an Error to an exit code based on the operation field.
int ExitCodeFromError(const erpl_adt::Error& error) {
    const auto& op = error.operation;
    if (op == "ConfigLoader") {
        return kExitInternal;
    }
    if (op.find("Connect") != std::string::npos ||
        op.find("Session") != std::string::npos ||
        op.find("Discovery") != std::string::npos ||
        op.find("CSRF") != std::string::npos) {
        return kExitConnection;
    }
    if (op.find("Package") != std::string::npos) {
        return kExitPackage;
    }
    if (op.find("Clone") != std::string::npos) {
        return kExitClone;
    }
    if (op.find("Pull") != std::string::npos) {
        return kExitPull;
    }
    if (op.find("Activat") != std::string::npos) {
        return kExitActivation;
    }
    if (op.find("Timeout") != std::string::npos ||
        op.find("Poll") != std::string::npos) {
        return kExitTimeout;
    }
    return kExitInternal;
}

static std::string_view GetFirstPositionalArg(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "-v" || arg == "-vv") continue;
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq == std::string_view::npos && i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i;
            }
            continue;
        }
        return arg;
    }
    return {};
}

// Parse the subcommand from argv. Returns the subcommand and the index of the
// first non-subcommand argument (so the rest can be passed to LoadFromCli).
struct SubcommandParse {
    erpl_adt::Subcommand cmd;
    bool found_subcommand;
};

SubcommandParse ParseSubcommand(int argc, const char* const* argv) {
    if (argc < 2) {
        return {erpl_adt::Subcommand::Deploy, false};
    }
    std::string_view arg1{argv[1]};
    if (arg1 == "deploy") {
        return {erpl_adt::Subcommand::Deploy, true};
    }
    if (arg1 == "status") {
        return {erpl_adt::Subcommand::Status, true};
    }
    if (arg1 == "pull") {
        return {erpl_adt::Subcommand::Pull, true};
    }
    if (arg1 == "activate") {
        return {erpl_adt::Subcommand::Activate, true};
    }
    if (arg1 == "discover") {
        return {erpl_adt::Subcommand::Discover, true};
    }
    // Not a subcommand — treat as flag/arg for the default "deploy" command.
    return {erpl_adt::Subcommand::Deploy, false};
}

// Resolve color mode for help output (stdout-based, before logger init).
bool ResolveColorForHelp(int argc, const char* const* argv) {
    bool force_color = false;
    bool force_no_color = false;
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--color" || arg == "--color=true") force_color = true;
        if (arg == "--no-color" || arg == "--color=false") force_no_color = true;
    }
    if (erpl_adt::NoColorEnvSet()) force_no_color = true;
    return !force_no_color && (force_color || erpl_adt::IsStdoutTty());
}

// Check for --version before the first positional (group) argument.
// Stops scanning at the first non-flag arg to avoid collisions with
// subcommand flags like `source read --version inactive`.
bool HandleVersionFlag(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--version") {
            std::cout << "erpl-adt " << erpl_adt::kVersion << "\n";
            return true;
        }
        // Stop at first positional (non-flag) argument.
        if (!arg.empty() && arg[0] != '-') break;
    }
    return false;
}

// Check for --help/-h before the first positional (group) argument.
// Returns true if help was printed (caller should exit 0).
// If a new-style group is detected, returns false (let the router handle it).
bool HandleHelpFlag(int argc, const char* const* argv) {
    bool found_help = false;
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--help" || arg == "-h") {
            found_help = true;
            continue;
        }
        // Skip short verbosity flags.
        if (arg == "-v" || arg == "-vv") continue;
        // Skip boolean flags that don't consume a value.
        if (arg == "--color" || arg == "--no-color" ||
            arg == "--json" || arg == "--https" || arg == "--insecure") continue;
        // Skip other flags.
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq == std::string_view::npos && i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i; // skip flag value
            }
            continue;
        }
        // First positional arg — if it's a new-style group, let router handle help.
        if (erpl_adt::IsNewStyleCommand(argc, argv)) {
            return false;
        }
        break;
    }
    if (found_help) {
        erpl_adt::CommandRouter router;
        erpl_adt::RegisterAllCommands(router);
        erpl_adt::PrintTopLevelHelp(router, std::cout, ResolveColorForHelp(argc, argv));
        return true;
    }
    return false;
}

// Build argv without the subcommand token, so LoadFromCli sees plain flags.
std::vector<const char*> StripSubcommand(int argc, const char* const* argv,
                                         bool has_subcommand) {
    std::vector<const char*> stripped;
    stripped.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (has_subcommand && i == 1) {
            continue; // skip the subcommand token
        }
        stripped.push_back(argv[i]);
    }
    return stripped;
}

// Identity for the feedback banner and the issue-link footer on failures.
static constexpr datazoo::BannerInfo kBanner {"erpl-adt", erpl_adt::kVersion,
                                              "https://github.com/DataZooDE/erpl-adt"};

void PrintError(const erpl_adt::Error& error, bool json_output) {
    if (json_output) {
        // JSON output is parsed by other programs; appending prose to it would
        // break them. Machine consumers get the error unchanged.
        std::cerr << error.ToJson() << "\n";
    } else {
        std::cerr << "Error: " << error.ToString() << "\n";
        // Every human-readable failure names the tracker. This is the single
        // choke point all error exits funnel through, which is why the hint
        // lives here rather than at each of the ~20 return sites.
        std::cerr << datazoo::IssueHint(kBanner).substr(1) << "\n";
    }
}

void PrintResult(const erpl_adt::DeployResult& result, bool json_output,
                 bool quiet) {
    if (json_output) {
        std::cout << R"({"success":)" << (result.success ? "true" : "false");
        std::cout << R"(,"repos":[)";
        for (size_t i = 0; i < result.repo_results.size(); ++i) {
            if (i > 0) {
                std::cout << ",";
            }
            const auto& r = result.repo_results[i];
            std::cout << R"({"name":")" << r.repo_name
                      << R"(","success":)" << (r.success ? "true" : "false")
                      << R"(,"message":")" << r.message
                      << R"(","elapsed_ms":)" << r.elapsed.count()
                      << "}";
        }
        std::cout << R"(],"summary":")" << result.summary << R"("})" << "\n";
    } else if (!quiet) {
        for (const auto& r : result.repo_results) {
            const char* status = r.success ? "OK" : "FAILED";
            std::cout << "[" << status << "] " << r.repo_name
                      << " - " << r.message
                      << " (" << r.elapsed.count() << "ms)\n";
        }
        std::cout << "\n" << result.summary << "\n";
    }
}

// Find "login" or "logout" as the first positional arg, skipping flags.
std::string FindLoginLogout(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "-v" || arg == "-vv") {
            continue;
        }
        // Skip boolean flags that don't consume a value.
        if (arg == "--color" || arg == "--no-color" ||
            arg == "--json" || arg == "--https" || arg == "--insecure") {
            continue;
        }
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq == std::string_view::npos && i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i; // skip flag value
            }
            continue;
        }
        if (arg == "login" || arg == "logout") {
            return std::string(arg);
        }
        break; // first positional isn't login/logout
    }
    return "";
}

// Find "mcp" as the first positional arg, skipping flags.
bool FindMcpCommand(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "-v" || arg == "-vv") {
            continue;
        }
        // Skip boolean flags that don't consume a value.
        if (arg == "--color" || arg == "--no-color" ||
            arg == "--json" || arg == "--https" || arg == "--insecure") {
            continue;
        }
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq == std::string_view::npos && i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i; // skip flag value
            }
            continue;
        }
        return arg == "mcp";
    }
    return false;
}

// Parse connection flags from argv (same logic as command_executor CreateSession,
// duplicated here because CreateSession is in an anonymous namespace there).
int HandleMcpServer(int argc, const char* const* argv) {
    using namespace erpl_adt;

    // Parse flags manually — "mcp" is a single-word command, not group+action.
    std::map<std::string, std::string> flags;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "mcp" || arg == "-v" || arg == "-vv") {
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            PrintMcpHelp(std::cout);
            return 0;
        }
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq != std::string_view::npos) {
                flags[std::string(arg.substr(2, eq - 2))] =
                    std::string(arg.substr(eq + 1));
            } else {
                auto key = std::string(arg.substr(2));
                // Boolean flags don't consume next arg.
                if (key == "https" || key == "insecure" ||
                    key == "json" || key == "color" || key == "no-color" ||
                    key == "http") {
                    flags[key] = "true";
                } else if (i + 1 < argc &&
                    std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                    flags[key] = argv[i + 1];
                    ++i;
                } else {
                    flags[key] = "true";
                }
            }
        }
    }

    auto get = [&](const std::string& key,
                    const std::string& def = "") -> std::string {
        auto it = flags.find(key);
        return (it != flags.end()) ? it->second : def;
    };

    // Load saved credentials as fallback.
    constexpr const char* kCredsFile = ".adt.creds";
    std::string saved_host, saved_user, saved_password, saved_client = "001";
    std::string saved_language;
    uint16_t saved_port = 50000;
    bool saved_https = false;
    {
        std::ifstream ifs(kCredsFile);
        if (ifs) {
            try {
                auto j = nlohmann::json::parse(ifs);
                saved_host = j.value("host", "");
                saved_port = j.value("port", static_cast<uint16_t>(50000));
                saved_user = j.value("user", "");
                saved_password = j.value("password", "");
                saved_client = j.value("client", "001");
                saved_https = j.value("use_https", false);
                saved_language = j.value("language", "");
            } catch (const nlohmann::json::exception&) {
                // Ignore malformed creds file.
            }
        }
    }

    // Connection settings: explicit flag > environment (ERPL_ADT_* / SAP_*) >
    // saved credentials > default.
    auto resolve = [&](const std::string& flag, const std::string& env_setting,
                       const std::string& saved, const std::string& fallback) {
        if (flags.count(flag)) {
            return get(flag);
        }
        auto env = ConnectionEnvValue(env_setting);
        if (env.has_value()) {
            return *env;
        }
        return saved.empty() ? fallback : saved;
    };

    auto host = resolve("host", "HOST", saved_host, "localhost");
    auto port_str = resolve(
        "port", "PORT",
        saved_port != 50000 ? std::to_string(saved_port) : "", "50000");
    uint16_t port = 0;
    std::string parse_error;
    if (!ParsePort(port_str, &port, &parse_error)) {
        std::cerr << "Error: " << parse_error << "\n";
        return kExitInternal;
    }
    auto use_https = flags.count("https")
                         ? get("https") == "true"
                         : saved_https;
    auto user = resolve("user", "USER", saved_user, "DEVELOPER");
    auto client_str = resolve("client", "CLIENT", saved_client, "001");
    auto password = get("password");

    // Resolve password: explicit flag > --password-env > ERPL_ADT_PASSWORD /
    // SAP_PASSWORD > saved creds.
    if (password.empty() && flags.count("password-env")) {
        const char* env_val = std::getenv(get("password-env").c_str());
        if (env_val != nullptr) {
            password = env_val;
        }
    }
    if (password.empty()) {
        auto env = ConnectionEnvValue("PASSWORD");
        if (env.has_value()) {
            password = *env;
        }
    }
    if (password.empty()) {
        password = saved_password;
    }

    auto client_result = SapClient::Create(client_str);
    if (client_result.IsErr()) {
        std::cerr << "Error: Invalid --client: " << client_result.Error() << "\n";
        return kExitInternal;
    }
    auto sap_client = std::move(client_result).Value();

    AdtSessionOptions opts;
    // SAP logon language: explicit --language flag > saved creds > default (EN).
    auto language_str = resolve("language", "LANGUAGE", saved_language, "");
    if (!language_str.empty()) {
        auto lang_result = SapLanguage::Create(language_str);
        if (lang_result.IsErr()) {
            std::cerr << "Error: Invalid --language: " << lang_result.Error() << "\n";
            return kExitInternal;
        }
        opts.language = std::move(lang_result).Value();
    }
    if (!get("timeout").empty()) {
        int timeout_seconds = 0;
        if (!ParseIntInRange(get("timeout"),
                             1,
                             std::numeric_limits<int>::max(),
                             "--timeout",
                             &timeout_seconds,
                             &parse_error)) {
            std::cerr << "Error: " << parse_error << "\n";
            return kExitInternal;
        }
        opts.read_timeout = std::chrono::seconds(timeout_seconds);
    }
    if (use_https && get("insecure") == "true") {
        opts.disable_tls_verify = true;
    }

    auto session = std::make_unique<AdtSession>(host, port, use_https, user,
                                                password, sap_client, opts);

    // Create tool registry and register all ADT tools.
    ToolRegistry registry;
    RegisterAdtTools(registry, *session);

    // catalog_* tools (BRD.md FR-MCP-1) — opened once here, for the process
    // lifetime (NFR-1). Optional: absent unless --catalog-db is passed,
    // since not every deployment has a sunk catalog.
    //
    // Opened read-write, not read-only: catalog_annotate (P3 curation)
    // shares this same connection, and DuckDB file-locking doesn't allow a
    // second read-write connection from another process while this one is
    // open. Trade-off accepted for now — a concurrent external `catalog
    // sync`/`catalog annotate` CLI run against the same file while this MCP
    // server holds it open will fail to open, rather than the NFR-2 ideal
    // of never blocking. Revisit (e.g. two connections, or MVCC-based
    // snapshot reads) once incremental sync (P4) needs true concurrent
    // read+write.
    if (!get("catalog-db").empty()) {
        auto store_result = DuckDbCatalogStore::Open(get("catalog-db"));
        if (store_result.IsErr()) {
            std::cerr << "Error: --catalog-db: " << store_result.Error().ToString() << "\n";
            return kExitInternal;
        }
        std::shared_ptr<DuckDbCatalogStore> catalog_store(std::move(store_result).Value());
        RegisterCatalogStoreTools(registry, catalog_store);
    }

    const bool http_transport = get("http") == "true";

    // Flush on SIGTERM/SIGINT — a long-lived server must explicitly drain.
    std::signal(SIGTERM, McpTelemetrySignalHandler);
    std::signal(SIGINT, McpTelemetrySignalHandler);

    if (http_transport) {
        uint16_t mcp_port = 8383;
        if (!get("mcp-port").empty()) {
            std::string port_parse_error;
            if (!ParsePort(get("mcp-port"), &mcp_port, &port_parse_error)) {
                std::cerr << "Error: --mcp-port: " << port_parse_error << "\n";
                return kExitInternal;
            }
        }
        auto mcp_host = get("mcp-host", "127.0.0.1");

        auto security = ResolveHttpSecurity(get("cors-origin"), get("auth-token"),
                                            get("auth-token-env"), mcp_host, std::cerr);
        if (!security.has_value()) {
            return kExitInternal;
        }

        // server_started { transport, tool_count }: same tool contract as
        // stdio (BRD.md FR-MCP-2) — a thin HTTP shim, not a second registry.
        Telemetry::ServerStarted("http", static_cast<int>(registry.Tools().size()));

        McpHttpServer server(std::move(registry), /*serve_webui=*/false, *security);
        std::cerr << "erpl-adt MCP HTTP server listening on http://" << mcp_host << ":"
                  << mcp_port << "/mcp\n";
        if (!server.Run(mcp_host, mcp_port)) {
            std::cerr << "Error: failed to bind " << mcp_host << ":" << mcp_port << "\n";
            Telemetry::Flush();
            return kExitInternal;
        }
    } else {
        // server_started { transport, tool_count }: counts/kinds only. The MCP
        // server speaks JSON-RPC over stdio. One $session_id spans this whole uptime.
        Telemetry::ServerStarted("stdio", static_cast<int>(registry.Tools().size()));

        // Create and run the MCP server (blocks until EOF on stdin).
        McpServer server(std::move(registry));
        server.Run();
    }

    // Clean shutdown: drain buffered events before exit.
    Telemetry::Flush();
    return 0;
}

} // anonymous namespace

int main(int argc, const char* argv[]) {
    using namespace erpl_adt;

    // No arguments: print top-level help.
    if (argc == 1) {
        CommandRouter router;
        RegisterAllCommands(router);
        PrintTopLevelHelp(router, std::cout, ResolveColorForHelp(argc, argv));
        return kExitSuccess;
    }

    // --version: print and exit before any parsing.
    if (HandleVersionFlag(argc, argv)) {
        return kExitSuccess;
    }

    // --help/-h: print top-level help if no new-style group is present.
    if (HandleHelpFlag(argc, argv)) {
        return kExitSuccess;
    }

    // Parse verbosity and color flags.
    auto log_level = LogLevel::Warn;
    bool force_color = false;
    bool force_no_color = false;
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "-vv") { log_level = LogLevel::Debug; }
        else if (arg == "-v")  { log_level = LogLevel::Info; }
        else if (arg == "--color" || arg == "--color=true") { force_color = true; }
        else if (arg == "--no-color" || arg == "--color=false") { force_no_color = true; }
    }
    // NO_COLOR env var (https://no-color.org/).
    if (NoColorEnvSet()) { force_no_color = true; }
    bool use_color = !force_no_color && (force_color || IsStderrTty());
    InitGlobalLogger(std::make_unique<ColorConsoleSink>(use_color), log_level);

    // Telemetry: initialize once, respecting --no-telemetry flag and env vars.
    // install_kind is "server" for the long-lived `mcp` subcommand, "cli"
    // otherwise. A single Flush() at exit is registered as the safety net (the
    // library's own at-exit handler discards buffered events by design).
    {
        bool no_telemetry = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--no-telemetry") {
                no_telemetry = true;
                break;
            }
        }
        auto kind = FindMcpCommand(argc, argv) ? InstallKind::Server
                                               : InstallKind::Cli;
        Telemetry::Initialize(no_telemetry, kVersion, kind);
        std::atexit([] { Telemetry::Flush(); });
    }

    // Once-a-day feedback nudge. Silent unless both streams are terminals and
    // the ~/.duckdb stamp is over a day old, so piped and scripted invocations
    // -- the overwhelming majority for a CLI -- see nothing. Machine-readable
    // and quiet runs suppress it outright.
    {
        bool machine_readable = false;
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string_view{argv[i]};
            if (arg == "--json" || arg == "--quiet" || arg == "-q" || arg == "--no-banner") {
                machine_readable = true;
                break;
            }
        }
        datazoo::ShowBannerStandalone(kBanner, machine_readable);
    }

    // login/logout: special top-level commands.
    auto login_cmd = FindLoginLogout(argc, argv);
    if (login_cmd == "login") {
        // Check for --help in login args.
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string_view{argv[i]};
            if (arg == "--help" || arg == "-h") {
                PrintLoginHelp(std::cout, ResolveColorForHelp(argc, argv));
                return kExitSuccess;
            }
        }
        return HandleLogin(argc, argv);
    }
    if (login_cmd == "logout") {
        // Check for --help in logout args.
        for (int i = 1; i < argc; ++i) {
            auto arg = std::string_view{argv[i]};
            if (arg == "--help" || arg == "-h") {
                PrintLogoutHelp(std::cout, ResolveColorForHelp(argc, argv));
                return kExitSuccess;
            }
        }
        return HandleLogout();
    }

    // MCP server mode.
    if (FindMcpCommand(argc, argv)) {
        return HandleMcpServer(argc, argv);
    }

    // New-style commands (search, object, source, etc.) via CommandRouter.
    if (IsNewStyleCommand(argc, argv)) {
        CommandRouter router;
        RegisterAllCommands(router);
        // BW group: intercept help requests for the detailed, categorized help.
        if (IsBwHelpRequest(argc, argv)) {
            PrintBwGroupHelp(router, std::cout, ResolveColorForHelp(argc, argv));
            return kExitSuccess;
        }
        // cli_started { command, args_shape }: command is the group; args_shape
        // records WHICH flags were present, never their values. Per-capability
        // feature_used events are emitted inside the handlers themselves.
        auto parse_result = CommandRouter::Parse(argc, argv);
        std::string tel_group;
        if (parse_result.IsOk() && Telemetry::IsEnabled()) {
            const auto& cmd = parse_result.Value();
            tel_group = cmd.group;
            std::string args_shape;
            for (const auto& kv : cmd.flags) {  // std::map → keys already sorted
                if (!args_shape.empty()) args_shape += ",";
                args_shape += kv.first;
            }
            Telemetry::CliStarted(cmd.group, args_shape);
        }
        int exit_code = router.Dispatch(argc, argv);
        // Central $exception: map the enumerated exit code to an error_class so
        // every failure is captured once without touching each handler. No
        // message or identifier is ever included — only the enum.
        if (Telemetry::IsEnabled()) {
            auto ec = ErrorClassForExitCode(exit_code);
            if (!ec.empty()) Telemetry::Error(ec, /*feature=*/"");
        }
        Telemetry::Flush();
        return exit_code;
    }

    // Catch unknown command groups before falling through to the legacy deploy path.
    // e.g. "erpl-adt read-adso FOO" should error, not silently try to deploy.
    {
        auto first = GetFirstPositionalArg(argc, argv);
        static const std::set<std::string_view> kLegacySubcommands = {
            "deploy", "status", "pull", "activate", "discover"};
        if (!first.empty() && !kLegacySubcommands.count(first)) {
            std::cerr << "Error: unknown command '" << first
                      << "'. Run 'erpl-adt --help' to see all available command groups.\n";
            return kExitInternal;
        }
    }

    // === Legacy deploy workflow path ===

    // Detect subcommand.
    auto [subcommand, has_subcommand] = ParseSubcommand(argc, argv);

    // Strip subcommand from argv so LoadFromCli sees only flags.
    auto stripped = StripSubcommand(argc, argv, has_subcommand);
    auto stripped_argc = static_cast<int>(stripped.size());
    auto stripped_argv = stripped.data();

    // Step 1-2: Parse CLI args (handles --help internally via argparse).
    auto cli_result = LoadFromCli(stripped_argc, stripped_argv);
    if (cli_result.IsErr()) {
        PrintError(cli_result.Error(), false);
        return kExitInternal;
    }
    auto cli_config = std::move(cli_result).Value();

    // Step 3: Load YAML config if -c/--config provided, merge with CLI.
    AppConfig config;
    // Check if config file was specified by looking for -c/--config in args.
    // LoadFromCli doesn't extract the config path into AppConfig, so we
    // scan argv directly.
    std::string config_path;
    for (int i = 0; i < stripped_argc; ++i) {
        std::string_view arg{stripped_argv[i]};
        if ((arg == "-c" || arg == "--config") && i + 1 < stripped_argc) {
            config_path = stripped_argv[i + 1];
            break;
        }
        // Handle --config=path form.
        if (arg.substr(0, 9) == "--config=") {
            config_path = std::string(arg.substr(9));
            break;
        }
    }

    if (!config_path.empty()) {
        auto yaml_result = LoadFromYaml(config_path);
        if (yaml_result.IsErr()) {
            PrintError(yaml_result.Error(), cli_config.json_output);
            return kExitInternal;
        }
        config = MergeConfigs(std::move(yaml_result).Value(), cli_config);
    } else {
        config = std::move(cli_config);
    }

    // Step 4: Resolve password_env.
    auto resolved = ResolvePasswordEnv(std::move(config));
    if (resolved.IsErr()) {
        PrintError(resolved.Error(), config.json_output);
        return kExitInternal;
    }
    config = std::move(resolved).Value();

    // Step 5: Validate config.
    // For "discover" subcommand, repos are not required — relax validation.
    if (subcommand != Subcommand::Discover) {
        auto valid = ValidateConfig(config);
        if (valid.IsErr()) {
            PrintError(valid.Error(), config.json_output);
            return kExitInternal;
        }
    } else {
        // Discover only needs connection info.
        if (config.connection.host.empty()) {
            PrintError(Error{"ConfigLoader", "", std::nullopt,
                             "Missing required field: host for discover", std::nullopt},
                       config.json_output);
            return kExitInternal;
        }
    }

    // Step 6: Sort repos by dependency order.
    if (!config.repos.empty()) {
        auto sorted = SortReposByDependency(config.repos);
        if (sorted.IsErr()) {
            PrintError(sorted.Error(), config.json_output);
            return kExitInternal;
        }
        config.repos = std::move(sorted).Value();
    }

    // Step 7: Create AdtSession.
    SapClient sap_client = config.connection.client.value_or(
        SapClient::Create("001").Value());
    AdtSessionOptions session_opts;
    session_opts.read_timeout = std::chrono::seconds(config.timeout_seconds);
    session_opts.language = config.connection.language;

    auto session = std::make_unique<AdtSession>(
        config.connection.host,
        config.connection.port,
        config.connection.use_https,
        config.connection.user,
        config.connection.password,
        sap_client,
        session_opts);

    // Step 8: Create XmlCodec.
    auto codec = std::make_unique<XmlCodec>();

    // Step 9: Create DeployWorkflow and execute.
    // deploy_run { outcome, object_count_bucket, duration_ms } — bounded counts
    // only; the repo count is bucketed, never repo names/URLs.
    Telemetry::CliStarted(std::string(GetFirstPositionalArg(argc, argv)), /*args_shape=*/"");
    const auto deploy_start = std::chrono::steady_clock::now();
    auto DeployTelemetry = [&](bool ok) {
        if (!Telemetry::IsEnabled()) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - deploy_start).count();
        Telemetry::Feature(feature::kDeployRun,
            {TelemetryProp::Str("outcome", ok ? "ok" : "error"),
             TelemetryProp::Str("object_count_bucket",
                                BucketCount(static_cast<long long>(config.repos.size()))),
             TelemetryProp::Num("duration_ms", static_cast<long long>(ms))});
    };

    DeployWorkflow workflow(*session, *codec, config);
    auto result = workflow.Execute(subcommand);

    // Step 10-11: Output results and return exit code.
    if (result.IsErr()) {
        const auto& error = result.Error();
        DeployTelemetry(/*ok=*/false);
        PrintError(error, config.json_output);
        return ExitCodeFromError(error);
    }

    auto deploy_result = std::move(result).Value();
    DeployTelemetry(deploy_result.success);
    PrintResult(deploy_result, config.json_output, config.quiet);

    return deploy_result.success ? kExitSuccess : kExitInternal;
}
