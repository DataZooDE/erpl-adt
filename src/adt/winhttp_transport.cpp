#include "http_transport.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

namespace erpl_adt::detail {
namespace {
Error TransportError(const std::string& message, ErrorCategory category = ErrorCategory::Authentication) {
    return Error{"Connect", "", std::nullopt, message, std::nullopt, category};
}

#ifdef _WIN32
// Windows handles are opaque API types; RAII keeps every early failure safe.
class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET Get() const { return handle_; }
private:
    HINTERNET handle_;
};
struct StoreCloser { void operator()(const CERT_CONTEXT* context) const { CertFreeCertificateContext(context); } };
using Certificate = std::unique_ptr<const CERT_CONTEXT, StoreCloser>;

std::wstring Wide(const std::string& text) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), output.data(), count) != count) return {};
    return output;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), output.data(), count, nullptr, nullptr) != count) return {};
    return output;
}
Error LastError(const char* operation) {
    const auto code = GetLastError();
    auto error = TransportError(std::string(operation) + " failed (Windows error " +
                                std::to_string(code) + ")",
                                code == ERROR_WINHTTP_TIMEOUT ? ErrorCategory::Timeout : ErrorCategory::Connection);
    error.operation = operation;
    if (code == ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED || code == ERROR_WINHTTP_SECURE_FAILURE) {
        error.category = ErrorCategory::Authentication;
        error.hint = "Check the client certificate's validity and private-key access, and the server certificate trust.";
    }
    return error;
}
int Milliseconds(std::chrono::seconds duration) {
    const auto seconds = duration.count();
    if (seconds <= 0) return 1;
    if (seconds > INT_MAX / 1000) return INT_MAX;
    return static_cast<int>(seconds * 1000);
}

class WindowsCertificateTransport final : public IHttpTransport {
public:
    WindowsCertificateTransport(const std::wstring& host, uint16_t port,
                                Certificate certificate, const AdtSessionOptions& options)
        : session_(WinHttpOpen(L"erpl-adt", WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)),
          connection_(session_.Get() ? WinHttpConnect(session_.Get(), host.c_str(), port, 0) : nullptr),
          certificate_(std::move(certificate)), options_(options) {}

    bool IsValid() const { return session_.Get() && connection_.Get(); }

    Result<TransportResponse, Error> Send(const TransportRequest& request) override {
        using Outcome = Result<TransportResponse, Error>;
        if (request.path.empty() || request.path.front() != '/' || request.path.rfind("//", 0) == 0 ||
            request.path.find_first_of("\r\n") != std::string::npos) {
            return Outcome::Err(TransportError("Windows HTTPS transport requires an origin-relative request path"));
        }
        const auto method = Wide(request.method);
        const auto path = Wide(request.path);
        if (method.empty() || path.empty()) return Outcome::Err(TransportError("Invalid UTF-8 method or request path"));
        const InternetHandle handle(WinHttpOpenRequest(connection_.Get(), method.c_str(), path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!handle.Get()) return Outcome::Err(LastError("WinHttpOpenRequest"));
        DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
        if (!WinHttpSetOption(handle.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSetTimeouts(handle.Get(), Milliseconds(options_.connect_timeout),
                Milliseconds(options_.connect_timeout), Milliseconds(options_.read_timeout),
                Milliseconds(options_.read_timeout)) ||
            !WinHttpSetOption(handle.Get(), WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                const_cast<CERT_CONTEXT*>(certificate_.get()), sizeof(CERT_CONTEXT))) {
            return Outcome::Err(LastError("WinHttpSetOption"));
        }
        if (options_.disable_tls_verify) {
            DWORD insecure = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            if (!WinHttpSetOption(handle.Get(), WINHTTP_OPTION_SECURITY_FLAGS, &insecure, sizeof(insecure))) {
                return Outcome::Err(LastError("WinHttpSetOption"));
            }
        }
        std::string headers;
        for (const auto& [name, value] : request.headers) {
            if (name.find_first_of("\r\n:") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) {
                return Outcome::Err(TransportError("Invalid HTTP header"));
            }
            headers += name + ": " + value + "\r\n";
        }
        const auto wide_headers = Wide(headers);
        if ((!headers.empty() && wide_headers.empty()) ||
            wide_headers.size() > std::numeric_limits<DWORD>::max() ||
            request.body.size() > std::numeric_limits<DWORD>::max()) {
            return Outcome::Err(TransportError("HTTP request is too large or contains invalid UTF-8 headers"));
        }
        // A writable copy satisfies WinHTTP's legacy input signature without exposing key material.
        auto body = request.body;
        const auto body_size = static_cast<DWORD>(body.size());
        if (!WinHttpSendRequest(handle.Get(), wide_headers.c_str(), static_cast<DWORD>(wide_headers.size()),
                                body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data(), body_size, body_size, 0) ||
            !WinHttpReceiveResponse(handle.Get(), nullptr)) return Outcome::Err(LastError("WinHttpSendRequest"));
        TransportResponse response;
        DWORD status = 0;
        DWORD length = sizeof(status);
        if (!WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) {
            return Outcome::Err(LastError("WinHttpQueryHeaders"));
        }
        response.status = static_cast<int>(status);
        length = 0;
        if (!WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                 nullptr, &length, WINHTTP_NO_HEADER_INDEX) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return Outcome::Err(LastError("WinHttpQueryHeaders"));
        }
        std::wstring raw_headers(length / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                raw_headers.data(), &length, WINHTTP_NO_HEADER_INDEX)) {
            return Outcome::Err(LastError("WinHttpQueryHeaders"));
        }
        raw_headers.resize(length / sizeof(wchar_t));
        const auto decoded = Utf8(raw_headers);
        auto offset = decoded.find("\r\n"); // Skip the status line.
        while (offset != std::string::npos) {
            offset += 2;
            const auto end = decoded.find("\r\n", offset);
            if (end == std::string::npos || end == offset) break;
            const auto colon = decoded.find(':', offset);
            if (colon != std::string::npos && colon < end) {
                auto value_start = colon + 1;
                while (value_start < end && (decoded[value_start] == ' ' || decoded[value_start] == '\t')) ++value_start;
                response.headers.emplace_back(decoded.substr(offset, colon - offset), decoded.substr(value_start, end - value_start));
            }
            offset = end;
        }
        std::array<char, 16384> buffer{};
        for (;;) {
            DWORD count = 0;
            if (!WinHttpReadData(handle.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count)) {
                return Outcome::Err(LastError("WinHttpReadData"));
            }
            if (count == 0) break;
            response.body.append(buffer.data(), count);
        }
        return Outcome::Ok(std::move(response));
    }
private:
    InternetHandle session_;
    InternetHandle connection_;
    Certificate certificate_;
    AdtSessionOptions options_;
};
#endif
} // namespace

Result<std::unique_ptr<IHttpTransport>, Error> CreateWindowsCertificateTransport(
    const std::string& host, uint16_t port, const AdtSessionOptions& options) {
    using Outcome = Result<std::unique_ptr<IHttpTransport>, Error>;
#ifdef _WIN32
    std::array<unsigned char, 20> hash{};
    const auto& thumbprint = options.windows_certificate_thumbprint;
    if (!thumbprint || thumbprint->size() != hash.size() * 2) {
        return Outcome::Err(TransportError("Windows certificate thumbprint must contain exactly 40 hexadecimal characters"));
    }
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (size_t index = 0; index < hash.size(); ++index) {
        const auto upper = nibble((*thumbprint)[index * 2]);
        const auto lower = nibble((*thumbprint)[index * 2 + 1]);
        if (upper < 0 || lower < 0) return Outcome::Err(TransportError("Windows certificate thumbprint must be hexadecimal"));
        hash[index] = static_cast<unsigned char>((upper << 4) | lower);
    }
    const auto store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store) return Outcome::Err(LastError("CertOpenStore"));
    CRYPT_HASH_BLOB hash_blob{};
    hash_blob.cbData = static_cast<DWORD>(hash.size());
    hash_blob.pbData = hash.data();
    Certificate certificate(CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0, CERT_FIND_HASH, &hash_blob, nullptr));
    CertCloseStore(store, 0);
    if (!certificate) return Outcome::Err(TransportError("Client certificate was not found in Windows CurrentUser\\MY"));
    if (CertVerifyTimeValidity(nullptr, certificate->pCertInfo) != 0) {
        return Outcome::Err(TransportError("Windows client certificate is expired or not yet valid"));
    }
    if (options.ca_certificate_path) {
        return Outcome::Err(TransportError("Windows certificate authentication uses Windows trust stores; PEM CA overrides are not supported"));
    }
    const auto wide_host = Wide(host);
    if (wide_host.empty()) return Outcome::Err(TransportError("Invalid UTF-8 hostname"));
    auto transport = std::make_unique<WindowsCertificateTransport>(wide_host, port, std::move(certificate), options);
    if (!transport->IsValid()) return Outcome::Err(LastError("WinHttpConnect"));
    return Outcome::Ok(std::move(transport));
#else
    (void)host;
    (void)port;
    (void)options;
    return Outcome::Err(TransportError("Windows certificate-store authentication is only available on Windows"));
#endif
}
} // namespace erpl_adt::detail
