#include <erpl_adt/adt/adt_session.hpp>
#include "adt_utils.hpp"
#include "http_transport.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/log.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace erpl_adt {

namespace {

// Errors built down here escape the per-operation AddBwHint() calls in the BW
// modules, so a failure the session layer raises for a BW path (a rejected CSRF
// fetch against an unactivated service, say) would otherwise arrive with no
// hint at all. AddBwHint self-gates on the endpoint, so calling it on every
// session error is safe.
Error MakeSessionError(const std::string& operation,
                       const std::string& endpoint,
                       std::optional<int> http_status,
                       const std::string& message,
                       ErrorCategory category = ErrorCategory::Connection) {
    Error error{operation, endpoint, http_status, message, std::nullopt, category};
    AddBwHint(error);
    return error;
}

// Same, for errors derived from an HTTP status.
Error MakeSessionHttpError(const std::string& operation,
                           const std::string& endpoint,
                           int status_code,
                           const std::string& body) {
    auto error = Error::FromHttpStatus(operation, endpoint, status_code, body);
    AddBwHint(error);
    return error;
}

// Check if a 403 response body contains a SAP application error (XML).
// BW lock conflicts return 403 with <exc:message> — these should NOT trigger
// CSRF retry because they are real application errors, not token expiry.
// Genuine CSRF expiry returns bare 403 with no XML error detail (often plain
// text).
//
// The body must look like XML (start with '<' after optional whitespace) AND
// contain a <message> or <exc:message> opening tag. The leading-'<' guard
// prevents false positives on plain-text 403 bodies that incidentally mention
// the literal text "<message>".
bool HasSapErrorInBody(const std::string& body) {
    if (body.empty()) return false;
    const auto first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || body[first] != '<') {
        return false;
    }
    return body.find("<exc:message") != std::string::npos ||
           body.find("<message") != std::string::npos;
}

ErrorCategory CategoryFromHttpTransportError(httplib::Error error) {
    switch (error) {
        case httplib::Error::Timeout:
        case httplib::Error::ConnectionTimeout:
        case httplib::Error::Read:
            return ErrorCategory::Timeout;
        default:
            return ErrorCategory::Connection;
    }
}

// Resolve the Accept-Language header value from an optional SAP logon
// language. SAP selects the logon language from this header; the 2-letter
// ISO code is sent lower-cased per HTTP convention (e.g. "de"). Defaults to
// English when unset.
std::string ResolveAcceptLanguage(const std::optional<SapLanguage>& language) {
    if (!language.has_value()) {
        return "en";
    }
    std::string lang = language->Value();
    std::transform(lang.begin(), lang.end(), lang.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lang;
}

// Convert httplib::Headers to our HttpHeaders map.
HttpHeaders ToHttpHeaders(const httplib::Headers& hdrs) {
    HttpHeaders result;
    for (const auto& [key, value] : hdrs) {
        result[key] = value;
    }
    return result;
}

// Build httplib::Headers from our HttpHeaders map plus SAP-specific headers.
httplib::Headers BuildRequestHeaders(
    const HttpHeaders& extra,
    const std::string& sap_client,
    const std::string& accept_language,
    const std::optional<std::string>& csrf_token,
    const std::map<std::string, std::string>& cookies = {},
    bool stateful_mode = false) {
    httplib::Headers hdrs;
    hdrs.emplace("sap-client", sap_client);
    hdrs.emplace("Accept-Language", accept_language);
    if (csrf_token.has_value()) {
        hdrs.emplace("x-csrf-token", *csrf_token);
    }
    if (!cookies.empty()) {
        std::string cookie_str;

        const auto append_cookie = [&](const std::string& name) {
            auto it = cookies.find(name);
            if (it == cookies.end()) return;
            if (!cookie_str.empty()) cookie_str += "; ";
            cookie_str += it->first + "=" + it->second;
        };

        if (!stateful_mode) {
            // Non-stateful requests: forward all cookies for CSRF/session
            // continuity across standard ADT operations.
            for (const auto& [name, _] : cookies) {
                append_cookie(name);
            }
        } else {
            // Stateful requests: pin context/session cookies only.
            append_cookie("sap-contextid");
            for (const auto& [name, _] : cookies) {
                if (name.rfind("SAP_SESSIONID_", 0) == 0 ||
                    name.rfind("sap-XSRF_", 0) == 0 ||
                    name.rfind("SAP-XSRF_", 0) == 0) {
                    append_cookie(name);
                }
            }
            append_cookie("sap-usercontext");
            for (const auto& [name, _] : cookies) {
                if (name == "sap-contextid" || name == "sap-usercontext" ||
                    name.rfind("SAP_SESSIONID_", 0) == 0 ||
                    name.rfind("sap-XSRF_", 0) == 0 ||
                    name.rfind("SAP-XSRF_", 0) == 0) {
                    continue;
                }
                append_cookie(name);
            }
        }
        hdrs.emplace("Cookie", cookie_str);
    }
    for (const auto& [key, value] : extra) {
        hdrs.emplace(key, value);
    }
    return hdrs;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Impl — pimpl body holding the httplib::Client and session state.
// ---------------------------------------------------------------------------
struct AdtSession::Impl {
    std::unique_ptr<httplib::Client> client;
    std::unique_ptr<detail::IHttpTransport> native_transport;
    std::string sap_client;
    std::string logon_user;
    std::string accept_language;                 // resolved SAP logon language
    std::optional<std::string> csrf_token;       // ADT paths (/sap/bc/adt/)
    std::optional<std::string> bw_csrf_token_;   // BW paths (/sap/bw/modeling/)
    AdtSessionOptions options;
    bool stateful_ = false;
    std::string sap_context_id_;
    std::map<std::string, std::string> cookies_;
    // Set when the session was saved to or loaded from a --session-file:
    // another erpl-adt process continues this logon, so it must not be
    // logged off when this object goes away.
    bool keep_logon_ = false;
    std::optional<Error> terminal_error_;
    // Invalid credentials/configuration must also block the logoff escape hatch.
    std::optional<Error> configuration_error_;
    // Kept for error messages: a transport failure that does not name the
    // server it tried is the hardest kind to diagnose.
    std::string base_url_;

    void RememberSessionFailure(const char* operation, std::string_view path,
                                int status, const std::string& body) {
        if (terminal_error_ || (status != 400 && status != 429)) return;
        auto error = Error::FromHttpStatus(operation, std::string(path), status, body);
        if (error.IsSessionFailure()) terminal_error_ = std::move(error);
    }

    Impl(const std::string& host,
         uint16_t port,
         bool use_https,
         const std::string& user,
         const std::string& password,
         const std::string& sap_client_value,
         const AdtSessionOptions& opts)
        : sap_client(sap_client_value),
          logon_user(user),
          accept_language(ResolveAcceptLanguage(opts.language)),
          options(opts) {
        base_url_ = (use_https ? "https://" : "http://") + host + ":" +
                    std::to_string(port);
        client = std::make_unique<httplib::Client>(base_url_);

        const bool certificate_auth = opts.client_certificate_path.has_value() ||
                                      opts.client_key_path.has_value();
        const auto reject = [&](const std::string& message) {
            configuration_error_ = MakeSessionError("Connect", base_url_, std::nullopt,
                                                    message, ErrorCategory::Authentication);
        };
        if ((certificate_auth || opts.windows_certificate_thumbprint || opts.ca_certificate_path) && !use_https) {
            reject("Client certificates and custom CA trust require HTTPS");
            return;
        }
        if (opts.windows_certificate_thumbprint) {
            if (certificate_auth) {
                reject("Cannot combine Windows certificate-store authentication with PEM identity files");
                return;
            }
            auto transport = detail::CreateWindowsCertificateTransport(host, port, opts);
            if (transport.IsErr()) {
                configuration_error_ = transport.Error();
                return;
            }
            native_transport = std::move(transport).Value();
            return;
        }
        if (certificate_auth) {
            if (!opts.client_certificate_path || opts.client_certificate_path->empty() ||
                !opts.client_key_path || opts.client_key_path->empty()) {
                reject("Certificate authentication requires both a PEM client certificate and private key");
                return;
            }
            // Never allow OpenSSL's default interactive password prompt in CLI/MCP.
            std::ifstream key_file(*opts.client_key_path);
            if (!key_file) {
                reject("Cannot read the PEM client private key");
                return;
            }
            std::string line;
            while (std::getline(key_file, line)) {
                if (line.find("ENCRYPTED") != std::string::npos) {
                    reject("Encrypted PEM private keys are not supported; use an unencrypted PEM key with restricted file permissions");
                    return;
                }
            }
            auto* tls = client->ssl_context();
            if (!tls ||
                SSL_CTX_use_certificate_chain_file(tls, opts.client_certificate_path->c_str()) != 1 ||
                SSL_CTX_use_PrivateKey_file(tls, opts.client_key_path->c_str(), SSL_FILETYPE_PEM) != 1 ||
                SSL_CTX_check_private_key(tls) != 1) {
                reject("Cannot load PEM client certificate and matching private key");
                return;
            }
        } else {
            client->set_basic_auth(user, password);
        }
        if (opts.ca_certificate_path) {
            if (!client->ssl_context() || opts.ca_certificate_path->empty() ||
                SSL_CTX_load_verify_locations(client->ssl_context(),
                                              opts.ca_certificate_path->c_str(), nullptr) != 1) {
                reject("Cannot load the CA certificate file");
                return;
            }
            client->set_ca_cert_path(*opts.ca_certificate_path);
        }
        client->set_connection_timeout(opts.connect_timeout);
        client->set_read_timeout(opts.read_timeout);

        if (use_https && opts.disable_tls_verify) {
            client->enable_server_certificate_verification(false);
        }
    }

    // Shape a transport failure (no HTTP response at all) into an Error.
    //
    // A read timeout used to arrive as "HTTP request failed: Failed to read
    // connection", which named neither the timeout nor the flag that raises
    // it — and the CLI then invited the user to file a bug for a limit they
    // had simply hit (issue #42). Worse, the ABAP behind a timed-out
    // classrun keeps running and usually completes, so the failure is about
    // our patience, not their work.
    Error MakeTransportError(const std::string& operation,
                             const std::string& path,
                             httplib::Error http_error) const {
        const auto category = CategoryFromHttpTransportError(http_error);

        // Never reached the server at all: the limit that expired is the
        // connection timeout, and --timeout would not have helped — saying
        // otherwise sends people to raise the wrong number.
        if (http_error == httplib::Error::ConnectionTimeout) {
            auto error = MakeSessionError(
                operation, path, std::nullopt,
                "connection timed out after " +
                    std::to_string(options.connect_timeout.count()) +
                    "s (connecting to " + base_url_ + ")",
                category);
            if (!error.hint.has_value()) {
                error.hint = "The server did not answer. Check the host, port "
                             "and that it is reachable from here.";
            }
            return error;
        }

        // The request was sent and the answer did not arrive in time.
        if (http_error == httplib::Error::Read ||
            http_error == httplib::Error::Timeout) {
            auto error = MakeSessionError(
                operation, path, std::nullopt,
                "request timed out after " +
                    std::to_string(options.read_timeout.count()) +
                    "s (connecting to " + base_url_ + ")",
                category);
            if (!error.hint.has_value()) {
                error.hint = "Raise it with --timeout <seconds>. Work already "
                             "started on the server may still be running and "
                             "may complete — check before repeating it.";
            }
            return error;
        }

        return MakeSessionError(operation, path, std::nullopt,
                                "HTTP request failed: " +
                                    httplib::to_string(http_error) +
                                    " (connecting to " + base_url_ + ")",
                                category);
    }

    Result<httplib::Response, Error> Exchange(
            const char* operation, const std::string& method, std::string_view path,
            const httplib::Headers& headers, std::string_view body = {},
            std::string_view content_type = {}) {
        using Outcome = Result<httplib::Response, Error>;
        // Async Location can be absolute, but credentials belong to exactly one origin.
        if (path.substr(0, 7) == "http://" || path.substr(0, 8) == "https://") {
            if (path == base_url_) {
                path = "/";
            } else if (path.size() > base_url_.size() &&
                       path.substr(0, base_url_.size()) == base_url_ && path[base_url_.size()] == '/') {
                path.remove_prefix(base_url_.size());
            } else {
                return Outcome::Err(MakeSessionError(operation, std::string(path), std::nullopt,
                    "Refusing an HTTP request outside the configured origin (including HTTPS downgrade)",
                    ErrorCategory::Authentication));
            }
        }
        if (path.empty() || path.front() != '/' || path.substr(0, 2) == "//" ||
            path.find_first_of("\r\n") != std::string_view::npos) {
            return Outcome::Err(MakeSessionError(operation, std::string(path), std::nullopt,
                "HTTP request path must be relative to the configured origin", ErrorCategory::Authentication));
        }
        if (native_transport) {
            detail::TransportRequest request;
            request.method = method;
            request.path = std::string(path);
            request.headers.assign(headers.begin(), headers.end());
            request.body = std::string(body);
            if (!content_type.empty()) request.headers.emplace_back("Content-Type", content_type);
            auto received = native_transport->Send(request);
            if (received.IsErr()) {
                auto error = received.Error();
                error.operation = operation;
                error.endpoint = std::string(path);
                AddBwHint(error);
                return Outcome::Err(std::move(error));
            }
            auto response = std::move(received).Value();
            httplib::Response converted;
            converted.status = response.status;
            converted.headers.insert(response.headers.begin(), response.headers.end());
            converted.body = std::move(response.body);
            return Outcome::Ok(std::move(converted));
        }
        const auto send = [&]() {
            if (method == "POST") return client->Post(std::string(path), headers, std::string(body), std::string(content_type));
            if (method == "PUT") return client->Put(std::string(path), headers, std::string(body), std::string(content_type));
            if (method == "DELETE") return client->Delete(std::string(path), headers);
            return client->Get(std::string(path), headers);
        };
        auto response = send();
        if (!response) return Outcome::Err(MakeTransportError(operation, std::string(path), response.error()));
        return Outcome::Ok(std::move(*response));
    }

    nlohmann::json SessionBinding() const {
        std::string authentication = "basic";
        std::string identity = logon_user;
        if (options.windows_certificate_thumbprint) {
            authentication = "windows-certificate";
            identity = *options.windows_certificate_thumbprint;
            std::transform(identity.begin(), identity.end(), identity.begin(),
                           [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        } else if (options.client_certificate_path || options.client_key_path) {
            authentication = "x509-pem";
            identity.clear();
            const auto* certificate = client->ssl_context() ? SSL_CTX_get0_certificate(client->ssl_context()) : nullptr;
            unsigned char digest[EVP_MAX_MD_SIZE]{};
            unsigned int length = 0;
            if (certificate && X509_digest(certificate, EVP_sha256(), digest, &length) == 1) {
                constexpr char hex[] = "0123456789abcdef";
                for (unsigned int index = 0; index < length; ++index) {
                    identity += hex[digest[index] >> 4];
                    identity += hex[digest[index] & 15];
                }
            }
        }
        return {{"version", 1}, {"origin", base_url_}, {"client", sap_client},
                {"authentication", authentication}, {"identity", identity}};
    }

    // Check if a request path targets the BW Modeling API.
    static bool IsBwPath(std::string_view path) {
        // "/sap/bw/modeling/" = 17 chars
        return path.substr(0, 17) == "/sap/bw/modeling/" ||
               path == "/sap/bw/modeling";
    }

    // Return the appropriate CSRF token for the given request path.
    const std::optional<std::string>& CsrfTokenFor(std::string_view path) const {
        return IsBwPath(path) ? bw_csrf_token_ : csrf_token;
    }

    // Capture sap-contextid from response headers for stateful sessions.
    void CaptureContextId(const httplib::Headers& hdrs) {
        if (!stateful_) return;
        for (const auto& [key, value] : hdrs) {
            auto lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(),
                           lower_key.begin(), ::tolower);
            if (lower_key == "sap-contextid") {
                sap_context_id_ = value;
                return;
            }
        }
    }

    // Capture set-cookie headers from response for session persistence.
    void CaptureCookies(const httplib::Headers& hdrs) {
        for (const auto& [key, value] : hdrs) {
            if (!adt_utils::IEquals(key, "set-cookie")) {
                continue;
            }
            auto semi = value.find(';');
            auto nv = value.substr(0, semi);
            auto eq = nv.find('=');
            if (eq != std::string::npos) {
                cookies_[nv.substr(0, eq)] = nv.substr(eq + 1);
            }
        }
    }

    // Inject stateful session headers into the request.
    // The X-sap-adt-sessiontype header is required for lock/write/unlock flows.
    // Context is carried via cookies (set-cookie: sap-contextid), not headers.
    void InjectStatefulHeaders(httplib::Headers& hdrs) const {
        if (!stateful_) return;
        hdrs.emplace("X-sap-adt-sessiontype", "stateful");
    }

    // Inject BW default headers expected by BW modeling services.
    void InjectBwHeaders(std::string_view path, httplib::Headers& hdrs) const {
        if (!IsBwPath(path)) return;
        auto has_bwmt_level = std::any_of(
            hdrs.begin(), hdrs.end(),
            [](const auto& kv) { return adt_utils::IEquals(kv.first, "bwmt-level"); });
        if (!has_bwmt_level) {
            hdrs.emplace("bwmt-level", "50");
        }
    }

    static bool IsSensitiveHeader(std::string_view key) {
        std::string lower_key(key);
        std::transform(lower_key.begin(), lower_key.end(),
                       lower_key.begin(), ::tolower);
        return lower_key == "cookie" ||
               lower_key == "authorization" ||
               lower_key == "sap-contextid" ||
               lower_key == "x-csrf-token";
    }

    // Log request headers at DEBUG level.
    static void LogRequestHeaders(const httplib::Headers& hdrs) {
        for (const auto& [k, v] : hdrs) {
            if (IsSensitiveHeader(k)) {
                LogDebug("http", "  > " + k + ": <redacted>");
            } else {
                LogDebug("http", "  > " + k + ": " + v);
            }
        }
    }

    // Log response status, notable headers, and body for errors.
    static void LogResponse(int status, const httplib::Headers& hdrs,
                            const std::string& body) {
        LogInfo("http", "  < " + std::to_string(status));
        for (const auto& [k, v] : hdrs) {
            if (adt_utils::IEquals(k, "set-cookie") || IsSensitiveHeader(k)) {
                LogDebug("http", "  < " + k + ": <redacted>");
            }
        }
        // Log response body at debug level for error responses.
        if (status >= 400 && !body.empty()) {
            constexpr size_t kMaxBodyLog = 2000;
            if (body.size() <= kMaxBodyLog) {
                LogDebug("http", "  < body: " + body);
            } else {
                LogDebug("http", "  < body: " + body.substr(0, kMaxBodyLog) + "... (truncated)");
            }
        }
    }

    // Execute a GET, returning our HttpResponse.
    Result<HttpResponse, Error> DoGet(std::string_view path,
                                      const HttpHeaders& extra_headers) {
        if (configuration_error_) return Result<HttpResponse, Error>::Err(*configuration_error_);
        if (terminal_error_ && path != "/sap/public/bc/icf/logoff") {
            return Result<HttpResponse, Error>::Err(*terminal_error_);
        }
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client, accept_language,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "GET " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = Exchange("Get", "GET", path, hdrs);
        if (!res) {
            return Result<HttpResponse, Error>::Err(res.Error());
        }
        LogResponse(res.Value().status, res.Value().headers, res.Value().body);
        CaptureContextId(res.Value().headers);
        CaptureCookies(res.Value().headers);
        RememberSessionFailure("Get", path, res.Value().status, res.Value().body);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res.Value().status, ToHttpHeaders(res.Value().headers), res.Value().body});
    }

    // Execute a POST, returning our HttpResponse.
    Result<HttpResponse, Error> DoPost(std::string_view path,
                                       std::string_view body,
                                       std::string_view content_type,
                                       const HttpHeaders& extra_headers) {
        if (configuration_error_) return Result<HttpResponse, Error>::Err(*configuration_error_);
        if (terminal_error_) return Result<HttpResponse, Error>::Err(*terminal_error_);
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client, accept_language,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "POST " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = Exchange("Post", "POST", path, hdrs, body, content_type);
        if (!res) {
            return Result<HttpResponse, Error>::Err(res.Error());
        }
        LogResponse(res.Value().status, res.Value().headers, res.Value().body);
        CaptureContextId(res.Value().headers);
        CaptureCookies(res.Value().headers);
        RememberSessionFailure("Post", path, res.Value().status, res.Value().body);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res.Value().status, ToHttpHeaders(res.Value().headers), res.Value().body});
    }

    // Execute a PUT, returning our HttpResponse.
    Result<HttpResponse, Error> DoPut(std::string_view path,
                                      std::string_view body,
                                      std::string_view content_type,
                                      const HttpHeaders& extra_headers) {
        if (configuration_error_) return Result<HttpResponse, Error>::Err(*configuration_error_);
        if (terminal_error_) return Result<HttpResponse, Error>::Err(*terminal_error_);
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client, accept_language,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "PUT " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = Exchange("Put", "PUT", path, hdrs, body, content_type);
        if (!res) {
            return Result<HttpResponse, Error>::Err(res.Error());
        }
        LogResponse(res.Value().status, res.Value().headers, res.Value().body);
        CaptureContextId(res.Value().headers);
        CaptureCookies(res.Value().headers);
        RememberSessionFailure("Put", path, res.Value().status, res.Value().body);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res.Value().status, ToHttpHeaders(res.Value().headers), res.Value().body});
    }

    // Execute a DELETE, returning our HttpResponse.
    Result<HttpResponse, Error> DoDelete(std::string_view path,
                                         const HttpHeaders& extra_headers) {
        if (configuration_error_) return Result<HttpResponse, Error>::Err(*configuration_error_);
        if (terminal_error_) return Result<HttpResponse, Error>::Err(*terminal_error_);
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client, accept_language,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "DELETE " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = Exchange("Delete", "DELETE", path, hdrs);
        if (!res) {
            return Result<HttpResponse, Error>::Err(res.Error());
        }
        LogResponse(res.Value().status, res.Value().headers, res.Value().body);
        CaptureContextId(res.Value().headers);
        CaptureCookies(res.Value().headers);
        RememberSessionFailure("Delete", path, res.Value().status, res.Value().body);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res.Value().status, ToHttpHeaders(res.Value().headers), res.Value().body});
    }

    // Fetch a new CSRF token. When request_path targets BW, fetch from
    // /sap/bw/modeling/discovery; otherwise /sap/bc/adt/discovery. SAP
    // scopes CSRF tokens per application path — an ADT token is rejected
    // by the BW endpoint and vice versa.
    Result<std::string, Error> DoFetchCsrfToken(
            std::string_view request_path = "") {
        if (configuration_error_) return Result<std::string, Error>::Err(*configuration_error_);
        if (terminal_error_) return Result<std::string, Error>::Err(*terminal_error_);
        const bool bw = IsBwPath(request_path);
        const auto fetch_from = [&](const std::string& fetch_path) -> Result<std::string, Error> {
            HttpHeaders extra;
            extra["x-csrf-token"] = "fetch";
            auto hdrs = BuildRequestHeaders(extra, sap_client, accept_language, std::nullopt,
                                            cookies_, stateful_);
            InjectStatefulHeaders(hdrs);
            InjectBwHeaders(fetch_path, hdrs);

            LogInfo("http", "GET " + fetch_path + " (CSRF fetch)");
            LogRequestHeaders(hdrs);
            auto res = Exchange("FetchCsrfToken", "GET", fetch_path, hdrs);
            if (!res) {
                return Result<std::string, Error>::Err(res.Error());
            }
            LogResponse(res.Value().status, res.Value().headers, res.Value().body);
            // Even a refused CSRF fetch can establish a security session.
            CaptureCookies(res.Value().headers);
            CaptureContextId(res.Value().headers);
            if (res.Value().status != 200) {
                auto error = MakeSessionHttpError("FetchCsrfToken", fetch_path, res.Value().status, res.Value().body);
                if (error.IsSessionFailure()) terminal_error_ = error;
                return Result<std::string, Error>::Err(error);
            }

            auto token = adt_utils::FindHeaderValueCi(
                ToHttpHeaders(res.Value().headers), "x-csrf-token");
            if (!token.has_value()) {
                return Result<std::string, Error>::Err(
                    MakeSessionError("FetchCsrfToken", fetch_path,
                                     res.Value().status,
                                     "No x-csrf-token header in response",
                                     ErrorCategory::CsrfToken));
            }
            if (bw) {
                bw_csrf_token_ = *token;
            } else {
                csrf_token = *token;
            }
            return Result<std::string, Error>::Ok(*token);
        };

        if (bw) {
            return fetch_from("/sap/bw/modeling/discovery");
        }

        return fetch_from("/sap/bc/adt/discovery");
    }
};

// ---------------------------------------------------------------------------
// AdtSession — constructor / destructor / move
// ---------------------------------------------------------------------------

AdtSession::AdtSession(const std::string& host,
                       uint16_t port,
                       bool use_https,
                       const std::string& user,
                       const std::string& password,
                       const SapClient& sap_client,
                       const AdtSessionOptions& options)
    : impl_(std::make_unique<Impl>(host, port, use_https, user, password,
                                   sap_client.Value(), options)) {}

namespace {

// SAP's logon (security) session cookie: SAP_SESSIONID_<SID>_<CLIENT>.
bool HoldsLogonSession(const std::map<std::string, std::string>& cookies) {
    for (const auto& [name, _] : cookies) {
        if (name.rfind("SAP_SESSIONID_", 0) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

// Ends the logon on the server - and with it any stateful ABAP context -
// through ICF's logoff service, then forgets everything bound to it (logon
// and context cookies, CSRF tokens). The next request logs on afresh.
//
// Measured on A4H (TH_USER_LIST): a stateful session left behind stays a
// user context until rdisp/plugin_auto_logout (30 min); at 400 per user SAP
// refuses new stateful sessions ("400 Service cannot be reached") and then
// every request ("429 Too Many Logons"), for every tool of that user.
// `sap-sessioncmd=close` (parameter or header) and `sap-sessioncmd=logoff`
// on an ADT URL are answered 200 but end nothing; /sap/public/bc/icf/logoff
// does. Best effort: on failure the session times out on the server.
bool AdtSession::EndServerSession() {
    // Logoff needs the security cookie, not a new stateful ABAP context.
    const bool stateful = impl_->stateful_;
    impl_->stateful_ = false;
    auto ended = impl_->DoGet("/sap/public/bc/icf/logoff", {});
    impl_->stateful_ = stateful;
    if (ended.IsErr() || ended.Value().status_code < 200 || ended.Value().status_code >= 300) {
        auto error = ended.IsErr() ? ended.Error() : Error::FromHttpStatus(
            "Logoff", "/sap/public/bc/icf/logoff", ended.Value().status_code, ended.Value().body);
        error.session_failure = true;
        if (!impl_->terminal_error_) impl_->terminal_error_ = error;
        LogWarn("http", "Logging off failed; stopping further work and retaining the session "
                        "for final cleanup: " + error.message);
        return false;
    }
    for (auto it = impl_->cookies_.begin(); it != impl_->cookies_.end();) {
        const auto& name = it->first;
        if (name == "sap-contextid" || name == "MYSAPSSO2" ||
            name.rfind("SAP_SESSIONID_", 0) == 0) {
            it = impl_->cookies_.erase(it);
        } else {
            ++it;
        }
    }
    impl_->csrf_token.reset();
    impl_->bw_csrf_token_.reset();
    impl_->sap_context_id_.clear();
    return true;
}

// A session nobody will continue (no --session-file) logs off when it goes
// away, so a CI job running hundreds of commands as one user leaves nothing
// open on the server.
AdtSession::~AdtSession() {
    if (!impl_ || (impl_->keep_logon_ && !impl_->terminal_error_) ||
        (!HoldsLogonSession(impl_->cookies_) && impl_->cookies_.count("sap-contextid") == 0)) {
        return;
    }
    try {
        EndServerSession();
    } catch (...) {
        // A destructor must not throw; the session times out on the server.
    }
}

// ---------------------------------------------------------------------------
// Get — with 403 CSRF retry
// ---------------------------------------------------------------------------
std::string AdtSession::LogonUserName() const { return impl_->logon_user; }

Result<HttpResponse, Error> AdtSession::Get(std::string_view path,
                                            const HttpHeaders& headers) {
    auto result = impl_->DoGet(path, headers);
    if (result.IsErr()) {
        return result;
    }

    // On 403, try re-fetching CSRF token and retry once — but only if the
    // body doesn't contain a SAP application error (e.g., BW lock conflict).
    if (result.Value().status_code == 403 &&
        !HasSapErrorInBody(result.Value().body)) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
        return impl_->DoGet(path, headers);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Post — with 403 CSRF retry
// ---------------------------------------------------------------------------
Result<HttpResponse, Error> AdtSession::Post(std::string_view path,
                                             std::string_view body,
                                             std::string_view content_type,
                                             const HttpHeaders& headers) {
    // Ensure we have a CSRF token for mutating requests.
    if (!impl_->CsrfTokenFor(path).has_value()) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
    }

    auto result = impl_->DoPost(path, body, content_type, headers);
    if (result.IsErr()) {
        return result;
    }

    // On 403, re-fetch CSRF token and retry once — but only if the
    // body doesn't contain a SAP application error (e.g., BW lock conflict).
    if (result.Value().status_code == 403 &&
        !HasSapErrorInBody(result.Value().body)) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
        return impl_->DoPost(path, body, content_type, headers);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Delete — with 403 CSRF retry
// ---------------------------------------------------------------------------
Result<HttpResponse, Error> AdtSession::Delete(std::string_view path,
                                               const HttpHeaders& headers) {
    // Ensure we have a CSRF token for mutating requests.
    if (!impl_->CsrfTokenFor(path).has_value()) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
    }

    auto result = impl_->DoDelete(path, headers);
    if (result.IsErr()) {
        return result;
    }

    // On 403, re-fetch CSRF token and retry once — but only if the
    // body doesn't contain a SAP application error.
    if (result.Value().status_code == 403 &&
        !HasSapErrorInBody(result.Value().body)) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
        return impl_->DoDelete(path, headers);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Put — with 403 CSRF retry
// ---------------------------------------------------------------------------
Result<HttpResponse, Error> AdtSession::Put(std::string_view path,
                                            std::string_view body,
                                            std::string_view content_type,
                                            const HttpHeaders& headers) {
    // Ensure we have a CSRF token for mutating requests.
    if (!impl_->CsrfTokenFor(path).has_value()) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
    }

    auto result = impl_->DoPut(path, body, content_type, headers);
    if (result.IsErr()) {
        return result;
    }

    // On 403, re-fetch CSRF token and retry once — but only if the
    // body doesn't contain a SAP application error.
    if (result.Value().status_code == 403 &&
        !HasSapErrorInBody(result.Value().body)) {
        auto token_result = impl_->DoFetchCsrfToken(path);
        if (token_result.IsErr()) {
            return Result<HttpResponse, Error>::Err(std::move(token_result).Error());
        }
        return impl_->DoPut(path, body, content_type, headers);
    }

    return result;
}

// ---------------------------------------------------------------------------
// SetStateful / IsStateful
// ---------------------------------------------------------------------------
void AdtSession::SetStateful(bool enabled) {
    // Leaving stateful mode ends the server-side ABAP session as well, like
    // Eclipse does when it drops a session (see EndServerSession for why
    // merely forgetting the context id is not enough).
    if (!enabled && impl_->stateful_ && impl_->cookies_.count("sap-contextid") > 0) {
        EndServerSession();
    }
    impl_->stateful_ = enabled;
    if (!enabled) {
        impl_->sap_context_id_.clear();
    }
}

void AdtSession::RecoverForNextOperation() {
    if (!impl_->terminal_error_) return;
    if ((HoldsLogonSession(impl_->cookies_) || impl_->cookies_.count("sap-contextid") > 0) &&
        !EndServerSession()) return;
    impl_->csrf_token.reset();
    impl_->bw_csrf_token_.reset();
    impl_->sap_context_id_.clear();
    impl_->cookies_.clear();
    impl_->stateful_ = false;
    impl_->terminal_error_.reset();
}

void AdtSession::ResetStatefulSession() {
    if (impl_->terminal_error_) return;
    if (HoldsLogonSession(impl_->cookies_) || impl_->cookies_.count("sap-contextid") > 0) {
        EndServerSession();
    }
    if (!impl_->terminal_error_) {
        impl_->csrf_token.reset();
        impl_->bw_csrf_token_.reset();
        impl_->sap_context_id_.clear();
        impl_->cookies_.clear();
    }
}

bool AdtSession::IsStateful() const {
    return impl_->stateful_;
}

// ---------------------------------------------------------------------------
// FetchCsrfToken
// ---------------------------------------------------------------------------
Result<std::string, Error> AdtSession::FetchCsrfToken() {
    return impl_->DoFetchCsrfToken();
}

// ---------------------------------------------------------------------------
// PollUntilComplete
// ---------------------------------------------------------------------------
Result<PollResult, Error> AdtSession::PollUntilComplete(
    std::string_view location_url,
    std::chrono::seconds timeout) {
    auto start = std::chrono::steady_clock::now();
    const auto deadline = start + timeout;

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        auto result = impl_->DoGet(location_url, {});
        if (result.IsErr()) {
            return Result<PollResult, Error>::Err(std::move(result).Error());
        }

        const auto& resp = result.Value();

        // Completed: 200 OK
        if (resp.status_code == 200) {
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            return Result<PollResult, Error>::Ok(
                PollResult{PollStatus::Completed, resp.body, elapsed});
        }

        // Still running: 202 Accepted
        if (resp.status_code == 202) {
            // Check timeout before sleeping.
            if (std::chrono::steady_clock::now() >= deadline) {
                elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                return Result<PollResult, Error>::Err(Error{
                    "PollUntilComplete",
                    std::string(location_url),
                    resp.status_code,
                    "Timed out waiting for async operation after " +
                        std::to_string(elapsed.count()) + "ms",
                    std::nullopt,
                    ErrorCategory::Timeout});
            }
            std::this_thread::sleep_for(impl_->options.poll_interval);
            continue;
        }

        // Failed: any other status
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return Result<PollResult, Error>::Ok(
            PollResult{PollStatus::Failed, resp.body, elapsed});
    }
}

// ---------------------------------------------------------------------------
// SaveSession / LoadSession
// ---------------------------------------------------------------------------
Result<void, Error> AdtSession::SaveSession(const std::string& path) const {
    if (impl_->configuration_error_) return Result<void, Error>::Err(*impl_->configuration_error_);
    nlohmann::json j;
    j["binding"] = impl_->SessionBinding();
    if (impl_->csrf_token.has_value()) {
        j["csrf_token"] = *impl_->csrf_token;
    }
    if (impl_->bw_csrf_token_.has_value()) {
        j["bw_csrf_token"] = *impl_->bw_csrf_token_;
    }
    j["stateful"] = impl_->stateful_;
    j["context_id"] = impl_->sap_context_id_;
    j["cookies"] = impl_->cookies_;

    std::ofstream ofs(path);
    if (!ofs) {
        return Result<void, Error>::Err(
            MakeSessionError("SaveSession", path, std::nullopt,
                             "Failed to open file for writing"));
    }
    ofs << j.dump(2);
    impl_->keep_logon_ = true;
#ifndef _WIN32
    // Set file permissions to owner read/write only (chmod 600).
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    return Result<void, Error>::Ok();
}

Result<void, Error> AdtSession::LoadSession(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return Result<void, Error>::Err(
            MakeSessionError("LoadSession", path, std::nullopt,
                             "Failed to open session file"));
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error& e) {
        return Result<void, Error>::Err(
            MakeSessionError("LoadSession", path, std::nullopt,
                             "Malformed JSON: " + std::string(e.what())));
    }

    if (impl_->configuration_error_) return Result<void, Error>::Err(*impl_->configuration_error_);
    const bool certificate_auth = impl_->options.client_certificate_path || impl_->options.client_key_path ||
                                  impl_->options.windows_certificate_thumbprint;
    if ((certificate_auth && !j.contains("binding")) ||
        (j.contains("binding") && j["binding"] != impl_->SessionBinding())) {
        return Result<void, Error>::Err(MakeSessionError("LoadSession", path, std::nullopt,
            "Session file does not match the server, SAP client, or authentication identity; start a new session",
            ErrorCategory::Authentication));
    }

    if (j.contains("csrf_token") && j["csrf_token"].is_string()) {
        impl_->csrf_token = j["csrf_token"].get<std::string>();
    }
    if (j.contains("bw_csrf_token") && j["bw_csrf_token"].is_string()) {
        impl_->bw_csrf_token_ = j["bw_csrf_token"].get<std::string>();
    }
    if (j.contains("stateful") && j["stateful"].is_boolean()) {
        impl_->stateful_ = j["stateful"].get<bool>();
    }
    if (j.contains("context_id") && j["context_id"].is_string()) {
        impl_->sap_context_id_ = j["context_id"].get<std::string>();
    }
    if (j.contains("cookies") && j["cookies"].is_object()) {
        impl_->cookies_ =
            j["cookies"].get<std::map<std::string, std::string>>();
    }
    impl_->keep_logon_ = true;

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
