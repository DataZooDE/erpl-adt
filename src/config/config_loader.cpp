#include <erpl_adt/config/config_loader.hpp>

#include <erpl_adt/core/version.hpp>

#include <argparse/argparse.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace erpl_adt {

namespace {

Error MakeConfigError(const std::string& message) {
    return Error{"ConfigLoader", "", std::nullopt, message, std::nullopt};
}

// Build a RepoConfig from parsed YAML node.
Result<RepoConfig, Error> ParseYamlRepo(const YAML::Node& node) {
    if (!node["name"]) {
        return Result<RepoConfig, Error>::Err(
            MakeConfigError("Repo entry missing 'name' field"));
    }
    if (!node["url"]) {
        return Result<RepoConfig, Error>::Err(
            MakeConfigError("Repo entry missing 'url' field"));
    }
    if (!node["package"]) {
        return Result<RepoConfig, Error>::Err(
            MakeConfigError("Repo entry missing 'package' field"));
    }

    auto url_str = node["url"].as<std::string>();
    auto url_result = RepoUrl::Create(url_str);
    if (url_result.IsErr()) {
        return Result<RepoConfig, Error>::Err(
            MakeConfigError("Invalid repo URL: " + url_result.Error()));
    }

    auto pkg_str = node["package"].as<std::string>();
    auto pkg_result = PackageName::Create(pkg_str);
    if (pkg_result.IsErr()) {
        return Result<RepoConfig, Error>::Err(
            MakeConfigError("Invalid package name: " + pkg_result.Error()));
    }

    std::optional<BranchRef> branch;
    if (node["branch"]) {
        auto branch_str = node["branch"].as<std::string>();
        auto branch_result = BranchRef::Create(branch_str);
        if (branch_result.IsErr()) {
            return Result<RepoConfig, Error>::Err(
                MakeConfigError("Invalid branch ref: " + branch_result.Error()));
        }
        branch = std::move(branch_result).Value();
    }

    bool activate = true;
    if (node["activate"]) {
        activate = node["activate"].as<bool>();
    }

    std::vector<std::string> depends_on;
    if (node["depends_on"]) {
        for (const auto& dep : node["depends_on"]) {
            depends_on.push_back(dep.as<std::string>());
        }
    }

    return Result<RepoConfig, Error>::Ok(RepoConfig{
        node["name"].as<std::string>(),
        std::move(url_result).Value(),
        std::move(branch),
        std::move(pkg_result).Value(),
        activate,
        std::move(depends_on),
    });
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// LoadFromYaml
// ---------------------------------------------------------------------------
Result<AppConfig, Error> LoadFromYaml(std::string_view file_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(std::string(file_path));
    } catch (const YAML::Exception& e) {
        return Result<AppConfig, Error>::Err(
            MakeConfigError("Failed to parse YAML file: " + std::string(e.what())));
    }

    AppConfig config;

    // -- Connection --
    if (root["connection"]) {
        const auto& conn = root["connection"];
        if (conn["host"]) {
            config.connection.host = conn["host"].as<std::string>();
        }
        if (conn["port"]) {
            config.connection.port = conn["port"].as<uint16_t>();
        }
        if (conn["https"]) {
            config.connection.use_https = conn["https"].as<bool>();
        }
        if (conn["client"]) {
            auto client_str = conn["client"].as<std::string>();
            auto client_result = SapClient::Create(client_str);
            if (client_result.IsErr()) {
                return Result<AppConfig, Error>::Err(
                    MakeConfigError("Invalid SAP client: " + client_result.Error()));
            }
            config.connection.client = std::move(client_result).Value();
        }
        if (conn["user"]) {
            config.connection.user = conn["user"].as<std::string>();
        }
        if (conn["password"]) {
            config.connection.password = conn["password"].as<std::string>();
        }
        if (conn["password_env"]) {
            config.connection.password_env = conn["password_env"].as<std::string>();
        }
    }

    // -- Repos --
    if (root["repos"]) {
        for (const auto& repo_node : root["repos"]) {
            auto repo_result = ParseYamlRepo(repo_node);
            if (repo_result.IsErr()) {
                return Result<AppConfig, Error>::Err(std::move(repo_result).Error());
            }
            config.repos.push_back(std::move(repo_result).Value());
        }
    }

    // -- Options --
    if (root["log_file"]) {
        config.log_file = root["log_file"].as<std::string>();
    }
    if (root["json_output"]) {
        config.json_output = root["json_output"].as<bool>();
    }
    if (root["verbose"]) {
        config.verbose = root["verbose"].as<bool>();
    }
    if (root["quiet"]) {
        config.quiet = root["quiet"].as<bool>();
    }
    if (root["timeout"]) {
        config.timeout_seconds = root["timeout"].as<int>();
    }

    return Result<AppConfig, Error>::Ok(std::move(config));
}

// ---------------------------------------------------------------------------
// LoadFromCli
// ---------------------------------------------------------------------------
Result<AppConfig, Error> LoadFromCli(int argc, const char* const* argv) {
    argparse::ArgumentParser program("erpl-adt", kVersion);

    // Connection flags
    program.add_argument("--host")
        .help("SAP system hostname");
    program.add_argument("--port")
        .help("SAP system port")
        .scan<'i', int>();
    program.add_argument("--https")
        .help("Use HTTPS")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--client")
        .help("SAP client (3 digits)");
    program.add_argument("--user")
        .help("SAP username");
    program.add_argument("--password")
        .help("SAP password");
    program.add_argument("--password-env")
        .help("Environment variable containing SAP password");

    // Repository flags (single-repo mode)
    program.add_argument("--repo")
        .help("Git repository URL");
    program.add_argument("--branch")
        .help("Git branch");
    program.add_argument("--package")
        .help("ABAP package name");

    // Options
    program.add_argument("-c", "--config")
        .help("Path to YAML config file");
    program.add_argument("--no-activate")
        .help("Skip activation step")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--timeout")
        .help("Timeout in seconds")
        .scan<'i', int>();
    program.add_argument("--json")
        .help("JSON output")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--log-file")
        .help("Log file path");
    program.add_argument("-v", "--verbose")
        .help("Verbose output")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-q", "--quiet")
        .help("Quiet output")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& e) {
        return Result<AppConfig, Error>::Err(
            MakeConfigError("CLI parse error: " + std::string(e.what())));
    }

    AppConfig config;

    // Connection
    if (auto val = program.present("--host")) {
        config.connection.host = *val;
    }
    if (auto val = program.present<int>("--port")) {
        config.connection.port = static_cast<uint16_t>(*val);
    }
    if (program.get<bool>("--https")) {
        config.connection.use_https = true;
    }
    if (auto val = program.present("--client")) {
        auto client_result = SapClient::Create(*val);
        if (client_result.IsErr()) {
            return Result<AppConfig, Error>::Err(
                MakeConfigError("Invalid --client: " + client_result.Error()));
        }
        config.connection.client = std::move(client_result).Value();
    }
    if (auto val = program.present("--user")) {
        config.connection.user = *val;
    }
    if (auto val = program.present("--password")) {
        config.connection.password = *val;
    }
    if (auto val = program.present("--password-env")) {
        config.connection.password_env = *val;
    }

    // Single-repo mode
    if (auto repo_url = program.present("--repo")) {
        auto url_result = RepoUrl::Create(*repo_url);
        if (url_result.IsErr()) {
            return Result<AppConfig, Error>::Err(
                MakeConfigError("Invalid --repo URL: " + url_result.Error()));
        }

        std::optional<BranchRef> branch;
        if (auto branch_val = program.present("--branch")) {
            auto branch_result = BranchRef::Create(*branch_val);
            if (branch_result.IsErr()) {
                return Result<AppConfig, Error>::Err(
                    MakeConfigError("Invalid --branch: " + branch_result.Error()));
            }
            branch = std::move(branch_result).Value();
        }

        PackageName package = PackageName::Create("$TMP").Value(); // default
        if (auto pkg_val = program.present("--package")) {
            auto pkg_result = PackageName::Create(*pkg_val);
            if (pkg_result.IsErr()) {
                return Result<AppConfig, Error>::Err(
                    MakeConfigError("Invalid --package: " + pkg_result.Error()));
            }
            package = std::move(pkg_result).Value();
        }

        bool activate = !program.get<bool>("--no-activate");

        config.repos.push_back(RepoConfig{
            "cli-repo",
            std::move(url_result).Value(),
            std::move(branch),
            std::move(package),
            activate,
            {},
        });
    }

    // Options
    if (auto val = program.present<int>("--timeout")) {
        config.timeout_seconds = *val;
    }
    if (program.get<bool>("--json")) {
        config.json_output = true;
    }
    if (auto val = program.present("--log-file")) {
        config.log_file = *val;
    }
    if (program.get<bool>("--verbose")) {
        config.verbose = true;
    }
    if (program.get<bool>("--quiet")) {
        config.quiet = true;
    }

    return Result<AppConfig, Error>::Ok(std::move(config));
}

// ---------------------------------------------------------------------------
// MergeConfigs
// ---------------------------------------------------------------------------
AppConfig MergeConfigs(const AppConfig& yaml_base, const AppConfig& cli_overrides) {
    AppConfig merged = yaml_base;

    // Connection overrides
    if (!cli_overrides.connection.host.empty()) {
        merged.connection.host = cli_overrides.connection.host;
    }
    if (cli_overrides.connection.port != 50000) {
        merged.connection.port = cli_overrides.connection.port;
    }
    if (cli_overrides.connection.use_https != false) {
        merged.connection.use_https = cli_overrides.connection.use_https;
    }
    if (!cli_overrides.connection.user.empty()) {
        merged.connection.user = cli_overrides.connection.user;
    }
    if (!cli_overrides.connection.password.empty()) {
        merged.connection.password = cli_overrides.connection.password;
    }
    if (cli_overrides.connection.password_env.has_value()) {
        merged.connection.password_env = cli_overrides.connection.password_env;
    }
    if (cli_overrides.connection.client.has_value()) {
        merged.connection.client = cli_overrides.connection.client;
    }

    // CLI repos override YAML repos if present
    if (!cli_overrides.repos.empty()) {
        merged.repos = cli_overrides.repos;
    }

    // Options
    if (cli_overrides.json_output) {
        merged.json_output = true;
    }
    if (cli_overrides.verbose) {
        merged.verbose = true;
    }
    if (cli_overrides.quiet) {
        merged.quiet = true;
    }
    if (cli_overrides.timeout_seconds != 600) {
        merged.timeout_seconds = cli_overrides.timeout_seconds;
    }
    if (cli_overrides.log_file.has_value()) {
        merged.log_file = cli_overrides.log_file;
    }

    return merged;
}

// ---------------------------------------------------------------------------
// ResolvePasswordEnv
// ---------------------------------------------------------------------------
Result<AppConfig, Error> ResolvePasswordEnv(AppConfig config) {
    if (config.connection.password.empty() &&
        config.connection.password_env.has_value()) {
        const auto& env_var = *config.connection.password_env;
        const char* env_val = std::getenv(env_var.c_str());
        if (env_val == nullptr) {
            return Result<AppConfig, Error>::Err(
                MakeConfigError("Environment variable '" + env_var +
                                "' not set (specified by password_env)"));
        }
        config.connection.password = env_val;
    }
    return Result<AppConfig, Error>::Ok(std::move(config));
}

// ---------------------------------------------------------------------------
// ValidateConfig
// ---------------------------------------------------------------------------
Result<void, Error> ValidateConfig(const AppConfig& config) {
    if (config.connection.host.empty()) {
        return Result<void, Error>::Err(MakeConfigError("Missing required field: host"));
    }
    if (config.connection.port == 0) {
        return Result<void, Error>::Err(MakeConfigError("Invalid port: 0"));
    }
    if (!config.connection.client.has_value()) {
        return Result<void, Error>::Err(MakeConfigError("Missing required field: client"));
    }
    if (config.connection.user.empty()) {
        return Result<void, Error>::Err(MakeConfigError("Missing required field: user"));
    }
    if (config.connection.password.empty() &&
        !config.connection.password_env.has_value()) {
        return Result<void, Error>::Err(
            MakeConfigError("Missing required field: password or password_env"));
    }
    if (config.repos.empty()) {
        return Result<void, Error>::Err(
            MakeConfigError("At least one repository must be configured"));
    }
    if (config.timeout_seconds <= 0) {
        return Result<void, Error>::Err(
            MakeConfigError("Timeout must be positive, got " +
                            std::to_string(config.timeout_seconds)));
    }
    if (config.verbose && config.quiet) {
        return Result<void, Error>::Err(
            MakeConfigError("Cannot use both --verbose and --quiet"));
    }
    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// SortReposByDependency — Kahn's algorithm (topological sort)
// ---------------------------------------------------------------------------
Result<std::vector<RepoConfig>, Error> SortReposByDependency(
    const std::vector<RepoConfig>& repos) {
    // Build name -> index map
    std::unordered_map<std::string, size_t> name_to_idx;
    for (size_t i = 0; i < repos.size(); ++i) {
        if (name_to_idx.count(repos[i].name)) {
            return Result<std::vector<RepoConfig>, Error>::Err(
                MakeConfigError("Duplicate repo name: " + repos[i].name));
        }
        name_to_idx[repos[i].name] = i;
    }

    // Build in-degree and adjacency
    std::vector<int> in_degree(repos.size(), 0);
    std::vector<std::vector<size_t>> dependents(repos.size());

    for (size_t i = 0; i < repos.size(); ++i) {
        for (const auto& dep_name : repos[i].depends_on) {
            auto it = name_to_idx.find(dep_name);
            if (it == name_to_idx.end()) {
                return Result<std::vector<RepoConfig>, Error>::Err(
                    MakeConfigError("Repo '" + repos[i].name +
                                    "' depends on unknown repo '" + dep_name + "'"));
            }
            dependents[it->second].push_back(i);
            in_degree[i]++;
        }
    }

    // Kahn's algorithm
    std::queue<size_t> ready;
    for (size_t i = 0; i < repos.size(); ++i) {
        if (in_degree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<RepoConfig> sorted;
    sorted.reserve(repos.size());

    while (!ready.empty()) {
        auto idx = ready.front();
        ready.pop();
        sorted.push_back(repos[idx]);
        for (auto dep_idx : dependents[idx]) {
            in_degree[dep_idx]--;
            if (in_degree[dep_idx] == 0) {
                ready.push(dep_idx);
            }
        }
    }

    if (sorted.size() != repos.size()) {
        // Find cycle participants for error message
        std::string cycle_repos;
        for (size_t i = 0; i < repos.size(); ++i) {
            if (in_degree[i] > 0) {
                if (!cycle_repos.empty()) {
                    cycle_repos += ", ";
                }
                cycle_repos += repos[i].name;
            }
        }
        return Result<std::vector<RepoConfig>, Error>::Err(
            MakeConfigError("Dependency cycle detected among repos: " + cycle_repos));
    }

    return Result<std::vector<RepoConfig>, Error>::Ok(std::move(sorted));
}

} // namespace erpl_adt
