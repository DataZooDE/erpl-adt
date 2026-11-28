#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <memory>
#include <optional>

namespace erpl_adt {

// The RFC connection owns the ABAP context. Implementations must never replay
// a failed invocation: a mutation may already have executed in SAP.
class IRfcTunnelClient {
public:
    virtual ~IRfcTunnelClient() = default;
    // Establish authentication before a caller decides whether an operation is safe.
    [[nodiscard]] virtual Result<void, Error> EnsureConnected() = 0;
    [[nodiscard]] virtual Result<HttpResponse, Error> Execute(
        std::string_view method, std::string_view path, std::string_view body,
        const HttpHeaders& headers) = 0;
    [[nodiscard]] virtual std::string AuthenticatedUser() const = 0;
    virtual void Close() = 0;
};

// Parameter names use the NW RFC ABI spelling. Connection validation and
// authentication policy belong to the application session factory.
[[nodiscard]] Result<std::unique_ptr<IRfcTunnelClient>, Error> CreateRfcTunnelClient(
    const std::map<std::string, std::string>& parameters);

class RfcTunnelSession final : public IAdtSession {
public:
    explicit RfcTunnelSession(std::unique_ptr<IRfcTunnelClient> client,
                             std::chrono::seconds poll_interval = std::chrono::seconds{2});
    ~RfcTunnelSession() override;
    [[nodiscard]] std::string LogonUserName() const override;
    [[nodiscard]] Result<HttpResponse, Error> Get(std::string_view path,
        const HttpHeaders& headers = {}) override;
    [[nodiscard]] Result<HttpResponse, Error> Post(std::string_view path,
        std::string_view body, std::string_view content_type,
        const HttpHeaders& headers = {}) override;
    [[nodiscard]] Result<HttpResponse, Error> Put(std::string_view path,
        std::string_view body, std::string_view content_type,
        const HttpHeaders& headers = {}) override;
    [[nodiscard]] Result<HttpResponse, Error> Delete(std::string_view path,
        const HttpHeaders& headers = {}) override;
    void SetStateful(bool enabled) override;
    [[nodiscard]] bool IsStateful() const override;
    void RecoverForNextOperation() override;
    [[nodiscard]] Result<std::string, Error> FetchCsrfToken() override;
    [[nodiscard]] Result<PollResult, Error> PollUntilComplete(std::string_view location_url,
        std::chrono::seconds timeout) override;
private:
    [[nodiscard]] Result<HttpResponse, Error> Request(std::string_view method,
        std::string_view path, std::string_view body, std::string_view content_type,
        const HttpHeaders& headers);
    std::unique_ptr<IRfcTunnelClient> client_;
    const std::string authenticated_user_;
    std::chrono::seconds poll_interval_;
    bool stateful_ = false;
    std::optional<Error> terminal_error_;
};
} // namespace erpl_adt
