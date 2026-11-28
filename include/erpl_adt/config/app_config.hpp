#pragma once

#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/core/types.hpp>
#include <erpl_adt/config/authentication.hpp>

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
    std::optional<AuthenticationMode> auth; // unset preserves legacy Basic mode
    std::optional<std::string> client_certificate;
    std::optional<std::string> client_key;
    std::optional<std::string> ca_certificate;
    std::optional<std::string> client_certificate_thumbprint;
    std::optional<std::string> snc_library;
    std::optional<std::string> snc_partner;
    std::optional<std::string> snc_name;
    std::optional<std::string> sysnr;
    std::optional<std::string> snc_qop;

    std::optional<std::string> password_env; // env var name to read password from
    std::optional<SapLanguage> language;     // SAP logon language; defaults to EN
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
    int timeout_seconds = kDefaultReadTimeoutSeconds;
};

} // namespace erpl_adt
