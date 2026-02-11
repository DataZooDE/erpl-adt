#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/adt/xml_codec.hpp>
#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>
#include <erpl_adt/config/config_loader.hpp>
#include <erpl_adt/core/log.hpp>
#include <erpl_adt/core/terminal.hpp>
#include <erpl_adt/core/version.hpp>
#include <erpl_adt/mcp/mcp_server.hpp>
#include <erpl_adt/mcp/mcp_tool_handlers.hpp>
#include <erpl_adt/workflow/deploy_workflow.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Exit codes per spec section 7.
constexpr int kExitSuccess    = 0;
constexpr int kExitConnection = 1;
constexpr int kExitPackage    = 2;
constexpr int kExitClone      = 3;
constexpr int kExitPull       = 4;
constexpr int kExitActivation = 5;
constexpr int kExitTimeout    = 10;
constexpr int kExitInternal   = 99;

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

void PrintError(const erpl_adt::Error& error, bool json_output) {
    if (json_output) {
        std::cerr << R"({"error":{"operation":")" << error.operation
                  << R"(","message":")" << error.message << R"("})" << "\n";
    } else {
        std::cerr << "Error: " << error.ToString() << "\n";
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
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq != std::string_view::npos) {
                flags[std::string(arg.substr(2, eq - 2))] =
                    std::string(arg.substr(eq + 1));
            } else {
                auto key = std::string(arg.substr(2));
                // Boolean flags don't consume next arg.
                if (key == "https" || key == "insecure" ||
                    key == "json" || key == "color" || key == "no-color") {
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
            } catch (const nlohmann::json::exception&) {
                // Ignore malformed creds file.
            }
        }
    }

    auto host = get("host", saved_host.empty() ? "localhost" : saved_host);
    auto port_str = get("port",
                        saved_port != 50000 ? std::to_string(saved_port) : "50000");
    auto port = static_cast<uint16_t>(std::stoi(port_str));
    auto use_https = flags.count("https")
                         ? get("https") == "true"
                         : saved_https;
    auto user = get("user", saved_user.empty() ? "DEVELOPER" : saved_user);
    auto client_str = get("client", saved_client);
    auto password = get("password");

    // Resolve password: explicit flag > env var > saved creds.
    if (password.empty()) {
        auto env_var = get("password-env", "SAP_PASSWORD");
        const char* env_val = std::getenv(env_var.c_str());
        if (env_val != nullptr) {
            password = env_val;
        }
    }
    if (password.empty()) {
        password = saved_password;
    }

    auto sap_client = SapClient::Create(client_str).Value();

    AdtSessionOptions opts;
    if (!get("timeout").empty()) {
        opts.read_timeout = std::chrono::seconds(std::stoi(get("timeout")));
    }
    if (use_https && get("insecure") == "true") {
        opts.disable_tls_verify = true;
    }

    auto session = std::make_unique<AdtSession>(host, port, use_https, user,
                                                password, sap_client, opts);

    // Create tool registry and register all ADT tools.
    ToolRegistry registry;
    RegisterAdtTools(registry, *session);

    // Create and run the MCP server (blocks until EOF on stdin).
    McpServer server(std::move(registry));
    server.Run();

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
        return router.Dispatch(argc, argv);
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
    DeployWorkflow workflow(*session, *codec, config);
    auto result = workflow.Execute(subcommand);

    // Step 10-11: Output results and return exit code.
    if (result.IsErr()) {
        const auto& error = result.Error();
        PrintError(error, config.json_output);
        return ExitCodeFromError(error);
    }

    auto deploy_result = std::move(result).Value();
    PrintResult(deploy_result, config.json_output, config.quiet);

    return deploy_result.success ? kExitSuccess : kExitInternal;
}
