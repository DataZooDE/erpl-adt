#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/config/mcp_server_config.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace erpl_adt;
namespace fs = std::filesystem;

namespace {

struct TempConfig {
    fs::path dir;
    fs::path file;
    explicit TempConfig(const std::string& yaml) {
        dir = fs::temp_directory_path() /
              ("erpl_adt_mcp_cfg_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        file = dir / "erpl-adt.yaml";
        std::ofstream(file) << yaml;
    }
    ~TempConfig() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    TempConfig(const TempConfig&) = delete;
    TempConfig& operator=(const TempConfig&) = delete;
};

}  // namespace

TEST_CASE("LoadMcpServerSettings: relative abapgit_root is relative to the config file",
          "[config][mcp]") {
    TempConfig cfg("mcp:\n  abapgit_root: repos\n");
    auto s = LoadMcpServerSettings(cfg.file.string());
    REQUIRE(s.IsOk());
    REQUIRE(s.Value().abapgit_root.has_value());
    CHECK(fs::path(*s.Value().abapgit_root) == (cfg.dir / "repos").lexically_normal());
}

TEST_CASE("LoadMcpServerSettings: absolute abapgit_root is kept", "[config][mcp]") {
#ifdef _WIN32
    // "/srv/abapgit" has no drive on Windows and is therefore relative there.
    const std::string absolute = "C:\\srv\\abapgit";
#else
    const std::string absolute = "/srv/abapgit";
#endif
    TempConfig cfg("mcp:\n  abapgit_root: '" + absolute + "'\n");
    auto s = LoadMcpServerSettings(cfg.file.string());
    REQUIRE(s.IsOk());
    CHECK(*s.Value().abapgit_root == absolute);
}

TEST_CASE("LoadMcpServerSettings: no mcp block is not an error", "[config][mcp]") {
    TempConfig cfg("http:\n  allowed_hosts: [a]\n");
    auto s = LoadMcpServerSettings(cfg.file.string());
    REQUIRE(s.IsOk());
    CHECK_FALSE(s.Value().abapgit_root.has_value());
}

TEST_CASE("LoadMcpServerSettings: missing file and invalid value are errors", "[config][mcp]") {
    CHECK(LoadMcpServerSettings("/no/such/erpl-adt.yaml").IsErr());
    TempConfig cfg("mcp:\n  abapgit_root: [a, b]\n");
    CHECK(LoadMcpServerSettings(cfg.file.string()).IsErr());
}
