#pragma once

#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace erpl_adt::detail {
// Internal exchange boundary. A vector preserves repeated Set-Cookie headers.
using TransportHeaders = std::vector<std::pair<std::string, std::string>>;
struct TransportRequest {
    std::string method;
    std::string path;
    TransportHeaders headers;
    std::string body;
};
struct TransportResponse {
    int status = 0;
    TransportHeaders headers;
    std::string body;
};
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    [[nodiscard]] virtual Result<TransportResponse, Error> Send(const TransportRequest& request) = 0;
};

[[nodiscard]] Result<std::unique_ptr<IHttpTransport>, Error> CreateWindowsCertificateTransport(
    const std::string& host, uint16_t port, const AdtSessionOptions& options);
} // namespace erpl_adt::detail
