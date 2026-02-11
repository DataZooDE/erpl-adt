#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/types.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// AdtSessionOptions — configuration for the ADT HTTP session.
// ---------------------------------------------------------------------------
struct AdtSessionOptions {
    std::chrono::seconds connect_timeout{30};
    std::chrono::seconds read_timeout{120};
    bool disable_tls_verify = false;
    std::chrono::seconds poll_interval{2};
};

// ---------------------------------------------------------------------------
// AdtSession — concrete IAdtSession implementation using cpp-httplib.
//
// Uses pimpl to avoid leaking httplib into the public header. The Impl
// struct is defined in the .cpp file.
//
// Features:
//   - Basic Auth on every request
//   - CSRF token lifecycle (fetch, cache, retry on 403)
//   - SAP headers: sap-client, Accept-Language
//   - Cookie jar: handled by httplib::Client automatically
//   - Async polling: PollUntilComplete for 202 responses
//   - TLS: optional disable for self-signed certs
// ---------------------------------------------------------------------------
class AdtSession : public IAdtSession {
public:
    AdtSession(const std::string& host,
               uint16_t port,
               bool use_https,
               const std::string& user,
               const std::string& password,
               const SapClient& sap_client,
               const AdtSessionOptions& options = {});

    ~AdtSession() override;

    // Non-copyable, non-movable (polymorphic base deletes move/copy).
    AdtSession(const AdtSession&) = delete;
    AdtSession& operator=(const AdtSession&) = delete;
    AdtSession(AdtSession&&) = delete;
    AdtSession& operator=(AdtSession&&) = delete;

    // -- IAdtSession implementation ------------------------------------------

    [[nodiscard]] Result<HttpResponse, Error> Get(
        std::string_view path,
        const HttpHeaders& headers = {}) override;

    [[nodiscard]] Result<HttpResponse, Error> Post(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers = {}) override;

    [[nodiscard]] Result<HttpResponse, Error> Put(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers = {}) override;

    [[nodiscard]] Result<HttpResponse, Error> Delete(
        std::string_view path,
        const HttpHeaders& headers = {}) override;

    void SetStateful(bool enabled) override;
    [[nodiscard]] bool IsStateful() const override;

    [[nodiscard]] Result<std::string, Error> FetchCsrfToken() override;

    [[nodiscard]] Result<PollResult, Error> PollUntilComplete(
        std::string_view location_url,
        std::chrono::seconds timeout) override;

    // -- Session persistence (concrete class only, not on IAdtSession) --------

    /// Save session state (CSRF token, context ID, cookies) to a JSON file.
    [[nodiscard]] Result<void, Error> SaveSession(const std::string& path) const;

    /// Load session state from a previously saved JSON file.
    [[nodiscard]] Result<void, Error> LoadSession(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace erpl_adt
