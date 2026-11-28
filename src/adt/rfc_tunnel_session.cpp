#include <erpl_adt/adt/rfc_tunnel_session.hpp>
#include "adt_utils.hpp"
#include <algorithm>
#include <thread>

namespace erpl_adt {
RfcTunnelSession::RfcTunnelSession(std::unique_ptr<IRfcTunnelClient> client,
                                 std::chrono::seconds interval)
    : client_(std::move(client)), authenticated_user_(client_->AuthenticatedUser()),
      poll_interval_(interval) {}
RfcTunnelSession::~RfcTunnelSession() { client_->Close(); }
std::string RfcTunnelSession::LogonUserName() const { return authenticated_user_; }

Result<HttpResponse, Error> RfcTunnelSession::Request(std::string_view method,
        std::string_view path, std::string_view body, std::string_view content_type,
        const HttpHeaders& headers) {
    if (terminal_error_) return Result<HttpResponse, Error>::Err(*terminal_error_);
    // RFC dispatch accepts application-relative URIs, never a different host.
    if (path.empty() || path.front() != '/' || path.substr(0, 2) == "//" ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
        Error error;
        error.operation = "RfcTunnel";
        error.endpoint = std::string(path);
        error.message = "RFC tunnel requires an absolute SAP resource path";
        return Result<HttpResponse, Error>::Err(std::move(error));
    }
    // Closing an ABAP context reconnects on the next operation. Certificate
    // mappings may have changed in the meantime; authenticate before dispatch
    // so a mutation cannot run as a different user from the session's owner.
    auto connected = client_->EnsureConnected();
    if (connected.IsErr()) {
        terminal_error_ = connected.Error();
        return Result<HttpResponse, Error>::Err(*terminal_error_);
    }
    if (authenticated_user_.empty() || client_->AuthenticatedUser() != authenticated_user_) {
        Error error;
        error.operation = "RfcTunnel";
        error.category = ErrorCategory::Authentication;
        error.message = "RFC authenticated identity changed; create a new session";
        terminal_error_ = std::move(error);
        client_->Close();
        return Result<HttpResponse, Error>::Err(*terminal_error_);
    }
    HttpHeaders request_headers = headers;
    if (!adt_utils::FindHeaderValueCi(request_headers, "Accept")) {
        request_headers["Accept"] = "*/*";
    }
    if (!content_type.empty() && !adt_utils::FindHeaderValueCi(request_headers, "Content-Type")) {
        request_headers["Content-Type"] = std::string(content_type);
    }
    if (path.substr(0, 8) == "/sap/bw/" &&
        !adt_utils::FindHeaderValueCi(request_headers, "bwmt-level")) {
        request_headers["bwmt-level"] = "50";
    }
    auto result = client_->Execute(method, path, body, request_headers);
    if (result.IsErr()) {
        // The transport cannot know whether SAP already applied a mutation.
        // Stop this operation rather than reconnecting or replaying it.
        terminal_error_ = result.Error();
        return result;
    }
    const auto& response = result.Value();
    if (response.status_code == 400 || response.status_code == 429) {
        auto error = Error::FromHttpStatus("RfcTunnel", std::string(path),
                                         response.status_code, response.body);
        if (error.IsSessionFailure()) terminal_error_ = std::move(error);
    }
    return result;
}
Result<HttpResponse, Error> RfcTunnelSession::Get(std::string_view path, const HttpHeaders& headers) {
    return Request("GET", path, "", "", headers);
}
Result<HttpResponse, Error> RfcTunnelSession::Post(std::string_view path, std::string_view body,
        std::string_view content_type, const HttpHeaders& headers) {
    return Request("POST", path, body, content_type, headers);
}
Result<HttpResponse, Error> RfcTunnelSession::Put(std::string_view path, std::string_view body,
        std::string_view content_type, const HttpHeaders& headers) {
    return Request("PUT", path, body, content_type, headers);
}
Result<HttpResponse, Error> RfcTunnelSession::Delete(std::string_view path, const HttpHeaders& headers) {
    return Request("DELETE", path, "", "", headers);
}
void RfcTunnelSession::SetStateful(bool enabled) {
    if (stateful_ && !enabled) client_->Close();
    stateful_ = enabled;
}
bool RfcTunnelSession::IsStateful() const { return stateful_; }
void RfcTunnelSession::RecoverForNextOperation() {
    if (!terminal_error_) return;
    client_->Close();
    stateful_ = false;
    terminal_error_.reset();
}
Result<std::string, Error> RfcTunnelSession::FetchCsrfToken() {
    if (terminal_error_) return Result<std::string, Error>::Err(*terminal_error_);
    // SADT_REST_RFC_ENDPOINT calls the REST handler directly under the RFC
    // logon. ICF CSRF/cookie negotiation does not run on this transport.
    return Result<std::string, Error>::Ok(std::string{});
}
Result<PollResult, Error> RfcTunnelSession::PollUntilComplete(std::string_view location,
                                                           std::chrono::seconds timeout) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + timeout;
    for (;;) {
        auto response = Get(location);
        if (response.IsErr()) return Result<PollResult, Error>::Err(std::move(response).Error());
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        if (response.Value().status_code == 200) {
            return Result<PollResult, Error>::Ok(PollResult{PollStatus::Completed,
                                                          response.Value().body, elapsed});
        }
        if (response.Value().status_code != 202) {
            return Result<PollResult, Error>::Ok(PollResult{PollStatus::Failed,
                                                          response.Value().body, elapsed});
        }
        if (now >= deadline) {
            Error error;
            error.operation = "PollUntilComplete";
            error.endpoint = std::string(location);
            error.category = ErrorCategory::Timeout;
            error.message = "Timed out waiting for RFC tunnel async operation";
            return Result<PollResult, Error>::Err(std::move(error));
        }
        std::this_thread::sleep_for(std::min(std::chrono::steady_clock::duration(poll_interval_),
                                           deadline - now));
    }
}
} // namespace erpl_adt
