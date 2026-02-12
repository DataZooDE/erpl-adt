#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/config/config_loader.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>  // _putenv_s
#endif

using namespace erpl_adt;

namespace {
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
} // namespace

// ===========================================================================
// Helper: path to test data files
// ===========================================================================

// Tests are run from the build directory; testdata is relative to project root.
// CMake sets the working directory to the build dir, but we need the source tree.
// Use __FILE__ to get the absolute path of this test file and derive testdata path.
namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.rfind('/');
    auto test_dir = this_file.substr(0, last_slash);   // .../test/config
    auto test_root = test_dir.substr(0, test_dir.rfind('/'));  // .../test
    return test_root + "/testdata/" + filename;
}

} // anonymous namespace

// ===========================================================================
// LoadFromYaml
// ===========================================================================

TEST_CASE("LoadFromYaml: valid full config", "[config][yaml]") {
    auto result = LoadFromYaml(TestDataPath("valid_config.yaml"));
    REQUIRE(result.IsOk());
    const auto& config = result.Value();

    CHECK(config.connection.host == "localhost");
    CHECK(config.connection.port == 50000);
    CHECK(config.connection.use_https == false);
    REQUIRE(config.connection.client.has_value());
    CHECK(config.connection.client->Value() == "001");
    CHECK(config.connection.user == "DEVELOPER");
    CHECK(config.connection.password.empty());
    REQUIRE(config.connection.password_env.has_value());
    CHECK(*config.connection.password_env == "SAP_PASSWORD");

    REQUIRE(config.repos.size() == 2);

    CHECK(config.repos[0].name == "flight");
    CHECK(config.repos[0].url.Value() ==
          "https://github.com/SAP-samples/abap-platform-refscen-flight.git");
    REQUIRE(config.repos[0].branch.has_value());
    CHECK(config.repos[0].branch->Value() == "main");
    CHECK(config.repos[0].package.Value() == "/DMO/FLIGHT");
    CHECK(config.repos[0].depends_on.empty());

    CHECK(config.repos[1].name == "rap-generator");
    REQUIRE(config.repos[1].depends_on.size() == 1);
    CHECK(config.repos[1].depends_on[0] == "flight");
}

TEST_CASE("LoadFromYaml: minimal config", "[config][yaml]") {
    auto result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(result.IsOk());
    const auto& config = result.Value();

    CHECK(config.connection.host == "sap.example.com");
    CHECK(config.connection.port == 50000);   // default
    CHECK(config.connection.use_https == false);  // default
    CHECK(config.connection.user == "ADMIN");
    CHECK(config.connection.password == "secret123");
    CHECK_FALSE(config.connection.password_env.has_value());

    REQUIRE(config.repos.size() == 1);
    CHECK(config.repos[0].name == "myrepo");
    CHECK_FALSE(config.repos[0].branch.has_value());
    CHECK(config.repos[0].activate == true);  // default
}

TEST_CASE("LoadFromYaml: nonexistent file", "[config][yaml]") {
    auto result = LoadFromYaml("/nonexistent/path/config.yaml");
    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "ConfigLoader");
}

TEST_CASE("LoadFromYaml: empty repos section is OK", "[config][yaml]") {
    // An empty YAML with just connection is parseable (validation catches missing repos)
    auto result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(result.IsOk());
}

// ===========================================================================
// LoadFromCli
// ===========================================================================

TEST_CASE("LoadFromCli: minimal args", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--host", "myhost",
        "--client", "001",
        "--user", "DEV",
        "--password", "pass",
        "--repo", "https://github.com/test/repo.git",
        "--package", "ZTEST"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    const auto& config = result.Value();

    CHECK(config.connection.host == "myhost");
    REQUIRE(config.connection.client.has_value());
    CHECK(config.connection.client->Value() == "001");
    CHECK(config.connection.user == "DEV");
    CHECK(config.connection.password == "pass");

    REQUIRE(config.repos.size() == 1);
    CHECK(config.repos[0].url.Value() == "https://github.com/test/repo.git");
    CHECK(config.repos[0].package.Value() == "ZTEST");
    CHECK(config.repos[0].activate == true);
}

TEST_CASE("LoadFromCli: no-activate flag", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--repo", "https://github.com/test/repo.git",
        "--no-activate"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().repos.size() == 1);
    CHECK(result.Value().repos[0].activate == false);
}

TEST_CASE("LoadFromCli: verbose and json flags", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "-v",
        "--json",
        "--timeout", "300"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().verbose == true);
    CHECK(result.Value().json_output == true);
    CHECK(result.Value().timeout_seconds == 300);
}

TEST_CASE("LoadFromCli: port and https flags", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--port", "8443",
        "--https"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().connection.port == 8443);
    CHECK(result.Value().connection.use_https == true);
}

TEST_CASE("LoadFromCli: password-env flag", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--password-env", "MY_SECRET"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().connection.password_env.has_value());
    CHECK(*result.Value().connection.password_env == "MY_SECRET");
}

TEST_CASE("LoadFromCli: invalid client value", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--client", "XYZ"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("--client") != std::string::npos);
}

TEST_CASE("LoadFromCli: invalid repo URL", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--repo", "not-a-url"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("--repo") != std::string::npos);
}

TEST_CASE("LoadFromCli: default package is $TMP", "[config][cli]") {
    const char* argv[] = {
        "erpl-adt",
        "--repo", "https://github.com/test/repo.git"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto result = LoadFromCli(argc, argv);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().repos.size() == 1);
    CHECK(result.Value().repos[0].package.Value() == "$TMP");
}

// ===========================================================================
// MergeConfigs
// ===========================================================================

TEST_CASE("MergeConfigs: CLI overrides YAML values", "[config][merge]") {
    // Build a "YAML" config
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto yaml_config = yaml_result.Value();

    // Build a "CLI" config with host override
    const char* argv[] = {
        "erpl-adt",
        "--host", "override-host",
        "--user", "OVERRIDE_USER",
        "--timeout", "120"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    auto cli_result = LoadFromCli(argc, argv);
    REQUIRE(cli_result.IsOk());
    auto cli_config = cli_result.Value();

    auto merged = MergeConfigs(yaml_config, cli_config);

    CHECK(merged.connection.host == "override-host");
    CHECK(merged.connection.user == "OVERRIDE_USER");
    CHECK(merged.timeout_seconds == 120);
    // YAML repos preserved when CLI has none
    CHECK(merged.repos.size() == 1);
    CHECK(merged.repos[0].name == "myrepo");
}

TEST_CASE("MergeConfigs: CLI repos replace YAML repos", "[config][merge]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());

    const char* argv[] = {
        "erpl-adt",
        "--repo", "https://github.com/test/cli-repo.git",
        "--package", "ZCLI"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    auto cli_result = LoadFromCli(argc, argv);
    REQUIRE(cli_result.IsOk());

    auto merged = MergeConfigs(yaml_result.Value(), cli_result.Value());

    REQUIRE(merged.repos.size() == 1);
    CHECK(merged.repos[0].url.Value() == "https://github.com/test/cli-repo.git");
    CHECK(merged.repos[0].package.Value() == "ZCLI");
}

TEST_CASE("MergeConfigs: YAML values preserved when CLI not set", "[config][merge]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());

    const char* argv[] = {"erpl-adt"};
    int argc = 1;
    auto cli_result = LoadFromCli(argc, argv);
    REQUIRE(cli_result.IsOk());

    auto merged = MergeConfigs(yaml_result.Value(), cli_result.Value());

    CHECK(merged.connection.host == "sap.example.com");
    CHECK(merged.connection.user == "ADMIN");
    CHECK(merged.connection.password == "secret123");
    CHECK(merged.repos.size() == 1);
}

// ===========================================================================
// ResolvePasswordEnv
// ===========================================================================

TEST_CASE("ResolvePasswordEnv: resolves from environment", "[config][env]") {
    // Set a test env var
    SetEnv("TEST_SAP_PASSWORD", "env_password_123");

    auto yaml_result = LoadFromYaml(TestDataPath("valid_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.password_env = "TEST_SAP_PASSWORD";
    config.connection.password.clear();

    auto result = ResolvePasswordEnv(std::move(config));
    REQUIRE(result.IsOk());
    CHECK(result.Value().connection.password == "env_password_123");

    UnsetEnv("TEST_SAP_PASSWORD");
}

TEST_CASE("ResolvePasswordEnv: error when env var not set", "[config][env]") {
    auto yaml_result = LoadFromYaml(TestDataPath("valid_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.password_env = "NONEXISTENT_VAR_FOR_TESTING";
    config.connection.password.clear();

    auto result = ResolvePasswordEnv(std::move(config));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("NONEXISTENT_VAR_FOR_TESTING") != std::string::npos);
}

TEST_CASE("ResolvePasswordEnv: skips when password already set", "[config][env]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    // password is "secret123", password_env not set

    auto result = ResolvePasswordEnv(std::move(config));
    REQUIRE(result.IsOk());
    CHECK(result.Value().connection.password == "secret123");
}

// ===========================================================================
// ValidateConfig
// ===========================================================================

TEST_CASE("ValidateConfig: valid config passes", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());

    auto result = ValidateConfig(yaml_result.Value());
    REQUIRE(result.IsOk());
}

TEST_CASE("ValidateConfig: missing host", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.host.clear();

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("host") != std::string::npos);
}

TEST_CASE("ValidateConfig: missing user", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.user.clear();

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("user") != std::string::npos);
}

TEST_CASE("ValidateConfig: missing password and password_env", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.password.clear();
    config.connection.password_env = std::nullopt;

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("password") != std::string::npos);
}

TEST_CASE("ValidateConfig: password_env alone is sufficient", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.password.clear();
    config.connection.password_env = "SOME_VAR";

    auto result = ValidateConfig(config);
    REQUIRE(result.IsOk());
}

TEST_CASE("ValidateConfig: missing client", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.client = std::nullopt;

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("client") != std::string::npos);
}

TEST_CASE("ValidateConfig: empty repos", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.repos.clear();

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("repository") != std::string::npos);
}

TEST_CASE("ValidateConfig: invalid port zero", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.connection.port = 0;

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("port") != std::string::npos);
}

TEST_CASE("ValidateConfig: invalid timeout", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.timeout_seconds = -1;

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Timeout") != std::string::npos);
}

TEST_CASE("ValidateConfig: verbose and quiet conflict", "[config][validate]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.verbose = true;
    config.quiet = true;

    auto result = ValidateConfig(config);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("verbose") != std::string::npos);
}

// ===========================================================================
// SortReposByDependency
// ===========================================================================

TEST_CASE("SortReposByDependency: correct topological order", "[config][topo]") {
    auto result = LoadFromYaml(TestDataPath("valid_config.yaml"));
    REQUIRE(result.IsOk());

    auto sort_result = SortReposByDependency(result.Value().repos);
    REQUIRE(sort_result.IsOk());
    const auto& sorted = sort_result.Value();

    REQUIRE(sorted.size() == 2);
    CHECK(sorted[0].name == "flight");
    CHECK(sorted[1].name == "rap-generator");
}

TEST_CASE("SortReposByDependency: no dependencies preserves order", "[config][topo]") {
    auto result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(result.IsOk());

    auto sort_result = SortReposByDependency(result.Value().repos);
    REQUIRE(sort_result.IsOk());
    CHECK(sort_result.Value().size() == 1);
    CHECK(sort_result.Value()[0].name == "myrepo");
}

TEST_CASE("SortReposByDependency: cycle detected", "[config][topo]") {
    auto result = LoadFromYaml(TestDataPath("cycle_config.yaml"));
    REQUIRE(result.IsOk());

    auto sort_result = SortReposByDependency(result.Value().repos);
    REQUIRE(sort_result.IsErr());
    CHECK(sort_result.Error().message.find("cycle") != std::string::npos);
}

TEST_CASE("SortReposByDependency: unknown dependency", "[config][topo]") {
    auto yaml_result = LoadFromYaml(TestDataPath("minimal_config.yaml"));
    REQUIRE(yaml_result.IsOk());
    auto config = yaml_result.Value();
    config.repos[0].depends_on.push_back("nonexistent");

    auto sort_result = SortReposByDependency(config.repos);
    REQUIRE(sort_result.IsErr());
    CHECK(sort_result.Error().message.find("nonexistent") != std::string::npos);
}

TEST_CASE("SortReposByDependency: diamond dependency", "[config][topo]") {
    // A -> B, A -> C, B -> D, C -> D
    // Expected order: D first, then B and C in some order, then A last
    auto url = RepoUrl::Create("https://github.com/test/repo.git").Value();
    auto pkg = PackageName::Create("ZTEST").Value();

    std::vector<RepoConfig> repos;
    repos.push_back(RepoConfig{"A", url, std::nullopt, pkg, true, {"B", "C"}});
    repos.push_back(RepoConfig{"B", url, std::nullopt, pkg, true, {"D"}});
    repos.push_back(RepoConfig{"C", url, std::nullopt, pkg, true, {"D"}});
    repos.push_back(RepoConfig{"D", url, std::nullopt, pkg, true, {}});

    auto sort_result = SortReposByDependency(repos);
    REQUIRE(sort_result.IsOk());
    const auto& sorted = sort_result.Value();
    REQUIRE(sorted.size() == 4);

    // D must come before B and C; B and C must come before A
    size_t pos_a = 0, pos_b = 0, pos_c = 0, pos_d = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i].name == "A") pos_a = i;
        if (sorted[i].name == "B") pos_b = i;
        if (sorted[i].name == "C") pos_c = i;
        if (sorted[i].name == "D") pos_d = i;
    }
    CHECK(pos_d < pos_b);
    CHECK(pos_d < pos_c);
    CHECK(pos_b < pos_a);
    CHECK(pos_c < pos_a);
}

TEST_CASE("SortReposByDependency: empty list", "[config][topo]") {
    std::vector<RepoConfig> repos;
    auto sort_result = SortReposByDependency(repos);
    REQUIRE(sort_result.IsOk());
    CHECK(sort_result.Value().empty());
}
