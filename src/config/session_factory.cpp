#include <erpl_adt/config/session_factory.hpp>
#include <erpl_adt/core/connection_env.hpp>
#include <erpl_adt/adt/rfc_tunnel_session.hpp>

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace erpl_adt {
namespace {
Error Invalid(const std::string& message) {
    Error error;
    error.category = ErrorCategory::Validation;
    error.message = message;
    return error;
}
Result<int, Error> Integer(const std::string& value, int maximum, const std::string& name) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed <= 0 || parsed > maximum) {
        return Result<int, Error>::Err(Invalid("Invalid " + name + ": " + value));
    }
    return Result<int, Error>::Ok(parsed);
}
// Resolve the identity from SAP over the authenticated transport. This wrapper
// keeps callers from confusing a configured name with a certificate mapping.
class IdentifiedSession final : public IAdtSession {
public:
    IdentifiedSession(std::unique_ptr<IAdtSession> session, std::string client, std::string expected_user)
        : session_(std::move(session)), client_(std::move(client)), expected_user_(std::move(expected_user)) {}
    Result<void, Error> Identify() {
        user_.clear();
        identity_error_.reset();
        auto response = session_->Get("/sap/bc/adt/core/http/systeminformation", {{"Accept", "application/vnd.sap.adt.core.http.systeminformation.v1+json"}});
        if (response.IsErr()) return Fail(response.Error());
        if (response.Value().status_code != 200) {
            return Fail(Error::FromHttpStatus("ResolveSsoIdentity", "/sap/bc/adt/core/http/systeminformation",
                                             response.Value().status_code, response.Value().body));
        }
        try {
            const auto info = nlohmann::json::parse(response.Value().body);
            const auto user = info.at("userName").get<std::string>();
            const auto client = info.at("client").get<std::string>();
            if (user.empty() || client != client_) return Fail(Invalid("SSO identity is empty or SAP authenticated a different client"));
            auto upper = [](std::string value) {
                for (auto& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return value;
            };
            if (!expected_user_.empty() && upper(user) != upper(expected_user_)) {
                return Fail(Invalid("SSO authenticated a different SAP user than --user"));
            }
            user_ = user;
        } catch (const nlohmann::json::exception&) {
            return Fail(Invalid("Cannot determine SSO identity: SAP systeminformation must return userName and client"));
        }
        return Result<void, Error>::Ok();
    }
    std::string LogonUserName() const override { return user_; }
    Result<HttpResponse, Error> Get(std::string_view path, const HttpHeaders& headers) override {
        if (identity_error_) return Result<HttpResponse, Error>::Err(*identity_error_);
        return session_->Get(path, headers);
    }
    Result<HttpResponse, Error> Post(std::string_view path, std::string_view body,
        std::string_view content_type, const HttpHeaders& headers) override {
        if (identity_error_) return Result<HttpResponse, Error>::Err(*identity_error_);
        return session_->Post(path, body, content_type, headers);
    }
    Result<HttpResponse, Error> Put(std::string_view path, std::string_view body,
        std::string_view content_type, const HttpHeaders& headers) override {
        if (identity_error_) return Result<HttpResponse, Error>::Err(*identity_error_);
        return session_->Put(path, body, content_type, headers);
    }
    Result<HttpResponse, Error> Delete(std::string_view path, const HttpHeaders& headers) override {
        if (identity_error_) return Result<HttpResponse, Error>::Err(*identity_error_);
        return session_->Delete(path, headers);
    }
    void SetStateful(bool value) override { session_->SetStateful(value); }
    bool IsStateful() const override { return session_->IsStateful(); }
    Result<std::string, Error> FetchCsrfToken() override {
        if (identity_error_) return Result<std::string, Error>::Err(*identity_error_);
        return session_->FetchCsrfToken();
    }
    Result<PollResult, Error> PollUntilComplete(std::string_view path, std::chrono::seconds timeout) override {
        if (identity_error_) return Result<PollResult, Error>::Err(*identity_error_);
        return session_->PollUntilComplete(path, timeout);
    }
    Result<void, Error> SaveSession(const std::string& path) const override { return session_->SaveSession(path); }
    Result<void, Error> LoadSession(const std::string& path) override {
        auto loaded = session_->LoadSession(path);
        if (loaded.IsErr()) return loaded;
        return Identify();
    }
    void RecoverForNextOperation() override {
        session_->RecoverForNextOperation();
        const auto identified = Identify();
        (void)identified; // Failure is retained and returned by the next operation.
    }
private:
    Result<void, Error> Fail(Error error) {
        identity_error_ = std::move(error);
        return Result<void, Error>::Err(*identity_error_);
    }
    std::unique_ptr<IAdtSession> session_;
    std::string client_;
    std::string expected_user_;
    std::string user_;
    std::optional<Error> identity_error_;
};
}

Result<SessionConfiguration, Error> ResolveSessionConfiguration(
    const std::map<std::string, std::string>& flags, const std::string& credentials_path) {
    nlohmann::json saved = nlohmann::json::object();
    std::ifstream file(credentials_path);
    if (file) {
        try { saved = nlohmann::json::parse(file); }
        catch (const nlohmann::json::exception&) {
            return Result<SessionConfiguration, Error>::Err(Invalid("Cannot read saved connection profile: " + credentials_path));
        }
    }
    if (!saved.is_object()) return Result<SessionConfiguration, Error>::Err(Invalid("Saved connection profile must be an object"));
    const auto config_file = flags.find("config");
    if (config_file != flags.end()) {
        try {
            const auto yaml = YAML::LoadFile(config_file->second);
            const auto connection = yaml["connection"];
            if (connection && !connection.IsMap()) {
                return Result<SessionConfiguration, Error>::Err(Invalid("YAML connection must be a mapping"));
            }
            if (connection) {
                for (const auto& item : connection) {
                    auto name = item.first.as<std::string>();
                    if (name == "https") name = "use_https";
                    saved[name] = item.second.as<std::string>();
                }
            }
            if (yaml["timeout"]) saved["timeout"] = yaml["timeout"].as<std::string>();
        } catch (const YAML::Exception& error) {
            return Result<SessionConfiguration, Error>::Err(Invalid("Invalid connection configuration: " + std::string(error.what())));
        }
    }
    auto resolve = [&](const std::string& flag, const std::string& environment,
                       const std::string& key, const std::string& fallback = "") -> std::string {
        const auto found = flags.find(flag);
        if (found != flags.end()) return found->second;
        if (!environment.empty()) {
            auto value = ConnectionEnvValue(environment);
            if (value) return *value;
        }
        const auto found_saved = saved.find(key);
        if (found_saved == saved.end() || found_saved->is_null()) return fallback;
        if (found_saved->is_string()) return found_saved->get<std::string>();
        if (found_saved->is_number_integer() || found_saved->is_boolean()) return found_saved->dump();
        return fallback;
    };
    SessionConfiguration result;
    auto& connection = result.connection;
    const auto mode = ParseAuthenticationMode(resolve("auth", "AUTH", "auth", "basic"));
    if (mode.IsErr()) return Result<SessionConfiguration, Error>::Err(mode.Error());
    connection.auth = mode.Value();
    if (mode.Value() == AuthenticationMode::Snc && flags.count("session-file")) {
        return Result<SessionConfiguration, Error>::Err(Invalid("RFC/SNC does not support --session-file; keep lock/write/unlock in one MCP process"));
    }
    const bool basic = mode.Value() == AuthenticationMode::Basic;
    connection.host = resolve("host", "HOST", "host", "localhost");
    const auto port = Integer(resolve("port", "PORT", "port", "50000"), 65535, "--port");
    if (port.IsErr()) return Result<SessionConfiguration, Error>::Err(port.Error());
    connection.port = static_cast<uint16_t>(port.Value());
    connection.use_https = resolve("https", "HTTPS", "use_https", "false") == "true";
    connection.user = resolve("user", "USER", "user", basic ? "DEVELOPER" : "");
    const auto client = SapClient::Create(resolve("client", "CLIENT", "client", "001"));
    if (client.IsErr()) return Result<SessionConfiguration, Error>::Err(Invalid("Invalid --client: " + client.Error()));
    connection.client = client.Value();
    const auto language = resolve("language", "LANGUAGE", "language");
    if (!language.empty()) {
        auto parsed = SapLanguage::Create(language);
        if (parsed.IsErr()) return Result<SessionConfiguration, Error>::Err(Invalid("Invalid --language: " + parsed.Error()));
        connection.language = parsed.Value();
        result.options.language = parsed.Value();
    }
    const auto timeout = Integer(resolve("timeout", "", "timeout", std::to_string(kDefaultReadTimeoutSeconds)),
                                 std::numeric_limits<int>::max(), "--timeout");
    if (timeout.IsErr()) return Result<SessionConfiguration, Error>::Err(timeout.Error());
    result.options.read_timeout = std::chrono::seconds(timeout.Value());
    result.options.disable_tls_verify = resolve("insecure", "", "insecure", "false") == "true";
    if (basic) {
        const auto password_flag = flags.find("password");
        if (password_flag != flags.end()) connection.password = password_flag->second;
        if (connection.password.empty()) {
            const auto variable = resolve("password-env", "", "password_env");
            if (!variable.empty()) {
                const auto* value = std::getenv(variable.c_str());
                if (!value) return Result<SessionConfiguration, Error>::Err(Invalid("Password environment variable is not set: " + variable));
                connection.password = value;
            }
        }
        if (connection.password.empty()) connection.password = resolve("password", "PASSWORD", "password");
    }
    auto certificate = resolve("client-certificate", "CLIENT_CERTIFICATE", "client_certificate");
    auto key = resolve("client-key", "CLIENT_KEY", "client_key");
    auto ca = resolve("ca-certificate", "CA_CERTIFICATE", "ca_certificate");
    if (!certificate.empty()) connection.client_certificate = std::move(certificate);
    if (!key.empty()) connection.client_key = std::move(key);
    if (!ca.empty()) connection.ca_certificate = std::move(ca);
    const auto client_certificate_thumbprint = resolve("client-certificate-thumbprint", "CLIENT_CERTIFICATE_THUMBPRINT", "client_certificate_thumbprint");
    if (!client_certificate_thumbprint.empty()) connection.client_certificate_thumbprint = client_certificate_thumbprint;
    const auto snc_library = resolve("snc-library", "SNC_LIBRARY", "snc_library");
    if (!snc_library.empty()) connection.snc_library = snc_library;
    const auto snc_partner = resolve("snc-partner", "SNC_PARTNER", "snc_partner");
    if (!snc_partner.empty()) connection.snc_partner = snc_partner;
    const auto snc_name = resolve("snc-name", "SNC_NAME", "snc_name");
    if (!snc_name.empty()) connection.snc_name = snc_name;
    const auto sysnr = resolve("sysnr", "SYSNR", "sysnr");
    if (!sysnr.empty()) connection.sysnr = sysnr;
    const auto snc_qop = resolve("snc-qop", "SNC_QOP", "snc_qop");
    if (!snc_qop.empty()) connection.snc_qop = snc_qop;
    return Result<SessionConfiguration, Error>::Ok(std::move(result));
}

Result<std::unique_ptr<IAdtSession>, Error> CreateAdtSession(const SessionConfiguration& configuration) {
    const auto& connection = configuration.connection;
    const auto mode = connection.auth.value_or(AuthenticationMode::Basic);

    if (connection.host.empty() || connection.port == 0 || !connection.client) {
        return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("Connection requires host, port and SAP client"));
    }
    if (mode != AuthenticationMode::Snc && (connection.snc_library || connection.snc_partner ||
        connection.snc_name || connection.snc_qop || connection.sysnr)) {
        return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("SNC settings require --auth snc"));
    }
    if (mode == AuthenticationMode::Snc) {
        if (connection.use_https || configuration.options.disable_tls_verify || connection.ca_certificate) {
            return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("HTTPS settings cannot be used with RFC/SNC"));
        }
        const auto number = connection.sysnr.value_or("00");
        const auto qop = connection.snc_qop.value_or("9");
        if (number.size() != 2 || number.find_first_not_of("0123456789") != std::string::npos ||
            !connection.snc_library || connection.snc_library->empty() ||
            !connection.snc_partner || connection.snc_partner->empty() ||
            (qop != "1" && qop != "2" && qop != "3" && qop != "9")) {
            return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("SNC requires --snc-library, --snc-partner, two-digit --sysnr and --snc-qop 1, 2, 3 or 9"));
        }
        if (connection.client_certificate || connection.client_key || connection.client_certificate_thumbprint) {
            return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("X.509 credentials cannot be used with RFC/SNC"));
        }
        std::map<std::string, std::string> parameters{
            {"ashost", connection.host}, {"sysnr", number}, {"client", connection.client->Value()},
            {"lang", connection.language ? connection.language->Value() : "EN"},
            {"snc_mode", "1"}, {"snc_sso", "1"}, {"snc_qop", qop},
            {"snc_lib", *connection.snc_library}, {"snc_partnername", *connection.snc_partner},
            {"snc_myname", connection.snc_name.value_or("")},
            {"erpl_connect_timeout", std::to_string(configuration.options.connect_timeout.count())},
            {"erpl_io_timeout", std::to_string(configuration.options.read_timeout.count())}};
        auto client = CreateRfcTunnelClient(parameters);
        if (client.IsErr()) return Result<std::unique_ptr<IAdtSession>, Error>::Err(client.Error());
        if (!connection.user.empty()) {
            auto actual = client.Value()->AuthenticatedUser();
            auto expected = connection.user;
            for (auto& c : actual) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            for (auto& c : expected) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (actual != expected) return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("SNC authenticated a different SAP user than --user"));
        }
        std::unique_ptr<IAdtSession> session = std::make_unique<RfcTunnelSession>(std::move(client).Value(), configuration.options.poll_interval);
        return Result<std::unique_ptr<IAdtSession>, Error>::Ok(std::move(session));
    }
    auto options = configuration.options;
    options.language = connection.language;
    options.ca_certificate_path = connection.ca_certificate;
    if (mode == AuthenticationMode::X509) {
        const bool pem = connection.client_certificate && !connection.client_certificate->empty() &&
                         connection.client_key && !connection.client_key->empty();
        const bool native = connection.client_certificate_thumbprint && !connection.client_certificate_thumbprint->empty();
        if (!connection.use_https || pem == native ||
            (native && (connection.client_certificate || connection.client_key || connection.ca_certificate))) {
            return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("X.509 requires HTTPS and either PEM certificate/key or Windows certificate thumbprint (OS trust)"));
        }
        options.client_certificate_path = connection.client_certificate;
        options.client_key_path = connection.client_key;
        options.windows_certificate_thumbprint = connection.client_certificate_thumbprint;
    } else if (connection.client_certificate || connection.client_key || connection.client_certificate_thumbprint) {
        return Result<std::unique_ptr<IAdtSession>, Error>::Err(Invalid("Client certificate/key requires --auth x509"));
    }
    std::unique_ptr<IAdtSession> session = std::make_unique<AdtSession>(connection.host, connection.port,
        connection.use_https, mode == AuthenticationMode::Basic ? connection.user : "",
        mode == AuthenticationMode::Basic ? connection.password : "", *connection.client, options);
    if (mode == AuthenticationMode::X509) {
        auto identified = std::make_unique<IdentifiedSession>(std::move(session), connection.client->Value(), connection.user);
        const auto identity = identified->Identify();
        if (identity.IsErr()) return Result<std::unique_ptr<IAdtSession>, Error>::Err(identity.Error());
        session = std::move(identified);
    }
    return Result<std::unique_ptr<IAdtSession>, Error>::Ok(std::move(session));
}
} // namespace erpl_adt
