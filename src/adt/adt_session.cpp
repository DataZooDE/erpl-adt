#include <erpl_adt/adt/adt_session.hpp>
#include "adt_utils.hpp"
#include <erpl_adt/core/log.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace erpl_adt {

namespace {

Error MakeSessionError(const std::string& operation,
                       const std::string& endpoint,
                       std::optional<int> http_status,
                       const std::string& message,
                       ErrorCategory category = ErrorCategory::Connection) {
    return Error{operation, endpoint, http_status, message, std::nullopt, category};
}

// Check if a 403 response body contains a SAP application error (XML).
// BW lock conflicts return 403 with <exc:message> — these should NOT trigger
// CSRF retry because they are real application errors, not token expiry.
// Genuine CSRF expiry returns bare 403 with no XML error detail.
bool HasSapErrorInBody(const std::string& body) {
    if (body.empty()) return false;
    // Match <exc:message>, <message>, or <message lang="..."> (attributes).
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
    const std::optional<std::string>& csrf_token,
    const std::map<std::string, std::string>& cookies = {},
    bool stateful_mode = false) {
    httplib::Headers hdrs;
    hdrs.emplace("sap-client", sap_client);
    hdrs.emplace("Accept-Language", "en");
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
    std::string sap_client;
    std::optional<std::string> csrf_token;       // ADT paths (/sap/bc/adt/)
    std::optional<std::string> bw_csrf_token_;   // BW paths (/sap/bw/modeling/)
    AdtSessionOptions options;
    bool stateful_ = false;
    std::string sap_context_id_;
    std::map<std::string, std::string> cookies_;

    Impl(const std::string& host,
         uint16_t port,
         bool use_https,
         const std::string& user,
         const std::string& password,
         const std::string& sap_client_value,
         const AdtSessionOptions& opts)
        : sap_client(sap_client_value), options(opts) {
        auto base_url = (use_https ? "https://" : "http://") + host + ":" +
                        std::to_string(port);
        client = std::make_unique<httplib::Client>(base_url);

        client->set_basic_auth(user, password);
        client->set_connection_timeout(opts.connect_timeout);
        client->set_read_timeout(opts.read_timeout);

        if (use_https && opts.disable_tls_verify) {
            client->enable_server_certificate_verification(false);
        }
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
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "GET " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = client->Get(std::string(path), hdrs);
        if (!res) {
            const auto http_error = res.error();
            return Result<HttpResponse, Error>::Err(
                MakeSessionError("Get", std::string(path), std::nullopt,
                                 "HTTP request failed: " +
                                     httplib::to_string(http_error),
                                 CategoryFromHttpTransportError(http_error)));
        }
        LogResponse(res->status, res->headers, res->body);
        CaptureContextId(res->headers);
        CaptureCookies(res->headers);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res->status, ToHttpHeaders(res->headers), res->body});
    }

    // Execute a POST, returning our HttpResponse.
    Result<HttpResponse, Error> DoPost(std::string_view path,
                                       std::string_view body,
                                       std::string_view content_type,
                                       const HttpHeaders& extra_headers) {
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "POST " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = client->Post(std::string(path), hdrs,
                                std::string(body), std::string(content_type));
        if (!res) {
            const auto http_error = res.error();
            return Result<HttpResponse, Error>::Err(
                MakeSessionError("Post", std::string(path), std::nullopt,
                                 "HTTP request failed: " +
                                     httplib::to_string(http_error),
                                 CategoryFromHttpTransportError(http_error)));
        }
        LogResponse(res->status, res->headers, res->body);
        CaptureContextId(res->headers);
        CaptureCookies(res->headers);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res->status, ToHttpHeaders(res->headers), res->body});
    }

    // Execute a PUT, returning our HttpResponse.
    Result<HttpResponse, Error> DoPut(std::string_view path,
                                      std::string_view body,
                                      std::string_view content_type,
                                      const HttpHeaders& extra_headers) {
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "PUT " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = client->Put(std::string(path), hdrs,
                               std::string(body), std::string(content_type));
        if (!res) {
            const auto http_error = res.error();
            return Result<HttpResponse, Error>::Err(
                MakeSessionError("Put", std::string(path), std::nullopt,
                                 "HTTP request failed: " +
                                     httplib::to_string(http_error),
                                 CategoryFromHttpTransportError(http_error)));
        }
        LogResponse(res->status, res->headers, res->body);
        CaptureContextId(res->headers);
        CaptureCookies(res->headers);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res->status, ToHttpHeaders(res->headers), res->body});
    }

    // Execute a DELETE, returning our HttpResponse.
    Result<HttpResponse, Error> DoDelete(std::string_view path,
                                         const HttpHeaders& extra_headers) {
        auto hdrs = BuildRequestHeaders(extra_headers, sap_client,
                                        CsrfTokenFor(path), cookies_,
                                        stateful_);
        InjectStatefulHeaders(hdrs);
        InjectBwHeaders(path, hdrs);
        LogInfo("http", "DELETE " + std::string(path));
        LogRequestHeaders(hdrs);
        auto res = client->Delete(std::string(path), hdrs);
        if (!res) {
            const auto http_error = res.error();
            return Result<HttpResponse, Error>::Err(
                MakeSessionError("Delete", std::string(path), std::nullopt,
                                 "HTTP request failed: " +
                                     httplib::to_string(http_error),
                                 CategoryFromHttpTransportError(http_error)));
        }
        LogResponse(res->status, res->headers, res->body);
        CaptureContextId(res->headers);
        CaptureCookies(res->headers);
        return Result<HttpResponse, Error>::Ok(HttpResponse{
            res->status, ToHttpHeaders(res->headers), res->body});
    }

    // Fetch a new CSRF token. When request_path targets BW, fetch from
    // /sap/bw/modeling/discovery; otherwise /sap/bc/adt/discovery. SAP
    // scopes CSRF tokens per application path — an ADT token is rejected
    // by the BW endpoint and vice versa.
    Result<std::string, Error> DoFetchCsrfToken(
            std::string_view request_path = "") {
        const bool bw = IsBwPath(request_path);
        const auto fetch_from = [&](const std::string& fetch_path) -> Result<std::string, Error> {
            HttpHeaders extra;
            extra["x-csrf-token"] = "fetch";
            auto hdrs = BuildRequestHeaders(extra, sap_client, std::nullopt,
                                            cookies_, stateful_);
            InjectStatefulHeaders(hdrs);
            InjectBwHeaders(fetch_path, hdrs);

            LogInfo("http", "GET " + fetch_path + " (CSRF fetch)");
            LogRequestHeaders(hdrs);
            auto res = client->Get(fetch_path, hdrs);
            if (!res) {
                const auto http_error = res.error();
                return Result<std::string, Error>::Err(
                    MakeSessionError("FetchCsrfToken", fetch_path,
                                     std::nullopt,
                                     "HTTP request failed: " +
                                         httplib::to_string(http_error),
                                     CategoryFromHttpTransportError(http_error)));
            }
            LogResponse(res->status, res->headers, res->body);
            if (res->status != 200) {
                return Result<std::string, Error>::Err(
                    Error::FromHttpStatus("FetchCsrfToken", fetch_path, res->status, res->body));
            }

            // Capture session cookies and context ID from the CSRF fetch response.
            CaptureCookies(res->headers);
            CaptureContextId(res->headers);

            auto token = adt_utils::FindHeaderValueCi(
                ToHttpHeaders(res->headers), "x-csrf-token");
            if (!token.has_value()) {
                return Result<std::string, Error>::Err(
                    MakeSessionError("FetchCsrfToken", fetch_path,
                                     res->status,
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

AdtSession::~AdtSession() = default;

// ---------------------------------------------------------------------------
// Get — with 403 CSRF retry
// ---------------------------------------------------------------------------
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
    impl_->stateful_ = enabled;
    if (!enabled) {
        impl_->sap_context_id_.clear();
    }
}

void AdtSession::ResetStatefulSession() {
    impl_->csrf_token.reset();
    impl_->bw_csrf_token_.reset();
    impl_->sap_context_id_.clear();
    impl_->cookies_.clear();
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
    nlohmann::json j;
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

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
