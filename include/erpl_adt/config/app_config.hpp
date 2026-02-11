#pragma once

#include <erpl_adt/core/types.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct ConnectionConfig {
    std::string host;
    uint16_t port = 50000;
    bool use_https = false;
    std::optional<SapClient> client;
    std::string user;
    std::string password;
    std::optional<std::string> password_env; // env var name to read password from
};

struct RepoConfig {
    std::string name;
    RepoUrl url;
    std::optional<BranchRef> branch; // defaults to main
    PackageName package;
    bool activate = true;
    std::vector<std::string> depends_on;
};

struct AppConfig {
    ConnectionConfig connection;
    std::vector<RepoConfig> repos;
    std::optional<std::string> log_file;
    bool json_output = false;
    bool verbose = false;
    bool quiet = false;
    int timeout_seconds = 600;
};

} // namespace erpl_adt
