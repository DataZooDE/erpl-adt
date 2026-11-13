#include <erpl_adt/config/mcp_server_config.hpp>

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>

namespace erpl_adt {

namespace {

Error MakeConfigError(const std::string& message) {
    return Error{"ConfigLoader", "", std::nullopt, message, std::nullopt};
}

}  // namespace

Result<McpServerFileSettings, Error> LoadMcpServerSettings(const std::string& path) {
    using R = Result<McpServerFileSettings, Error>;
    std::ifstream probe(path);
    if (!probe.good()) {
        return R::Err(MakeConfigError("Config file not found: " + path));
    }
    probe.close();

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        return R::Err(MakeConfigError("Failed to parse YAML: " + std::string(e.what())));
    }

    McpServerFileSettings settings;
    const auto mcp = root["mcp"];
    if (!mcp) {
        return R::Ok(std::move(settings));
    }
    const auto node = mcp["abapgit_root"];
    if (node) {
        if (!node.IsScalar() || node.as<std::string>("").empty()) {
            return R::Err(MakeConfigError("mcp.abapgit_root must be a folder path"));
        }
        std::filesystem::path dir(node.as<std::string>());
        if (dir.is_relative()) {
            std::error_code ec;
            dir = std::filesystem::absolute(path, ec).parent_path() / dir;
        }
        settings.abapgit_root = dir.lexically_normal().string();
    }
    return R::Ok(std::move(settings));
}

}  // namespace erpl_adt
