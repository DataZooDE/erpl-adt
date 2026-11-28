#include <catch2/catch_test_macros.hpp>
#include <erpl_adt/config/config_loader.hpp>

#include <filesystem>

using namespace erpl_adt;

TEST_CASE("X509 YAML permits passwordless deploy configuration", "[config][sso]") {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "testdata/x509_config.yaml";
    auto loaded = LoadFromYaml(path.string());
    REQUIRE(loaded.IsOk());
    const auto valid = ValidateConfig(loaded.Value());
    INFO((valid.IsErr() ? valid.Error().message : "valid"));
    CHECK(valid.IsOk());
    const auto resolved = ResolvePasswordEnv(loaded.Value());
    INFO((resolved.IsErr() ? resolved.Error().message : "resolved"));
    REQUIRE(resolved.IsOk());
    CHECK(resolved.Value().connection.password.empty());
}

TEST_CASE("X509 command line accepts explicit certificate authentication", "[config][sso]") {
    const char* argv[] = {"erpl-adt", "--host", "sap.example.test", "--https",
        "--client", "040", "--auth", "x509", "--client-certificate", "client.pem",
        "--client-key", "client.key", "--ca-certificate", "ca.pem",
        "--repo", "https://github.com/example/example.git", "--package", "ZEXAMPLE"};
    auto loaded = LoadFromCli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    INFO((loaded.IsErr() ? loaded.Error().message : "loaded"));
    REQUIRE(loaded.IsOk());
    CHECK(ValidateConfig(loaded.Value()).IsOk());
}

TEST_CASE("SNC command line accepts a passwordless RFC destination", "[config][sso]") {
    const char* argv[] = {"erpl-adt", "--host", "sap.example.test", "--client", "040",
        "--auth", "snc", "--sysnr", "00", "--snc-library", "sapcrypto.dll",
        "--snc-partner", "p:CN=SAPSERVER", "--snc-qop", "9",
        "--repo", "https://github.com/example/example.git", "--package", "ZEXAMPLE"};
    auto loaded = LoadFromCli(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    INFO((loaded.IsErr() ? loaded.Error().message : "loaded"));
    REQUIRE(loaded.IsOk());
    CHECK(ValidateConfig(loaded.Value()).IsOk());
}
