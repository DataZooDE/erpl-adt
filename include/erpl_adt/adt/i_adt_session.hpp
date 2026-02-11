#pragma once

#include <erpl_adt/core/result.hpp>

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// HttpHeaders — ordered key-value pairs for HTTP headers.
// Using a map so lookups are straightforward. Header names are case-sensitive
// in this representation; callers normalise as needed.
// ---------------------------------------------------------------------------
using HttpHeaders = std::map<std::string, std::string>;

// ---------------------------------------------------------------------------
// HttpResponse — the result of an HTTP request.
// ---------------------------------------------------------------------------
struct HttpResponse {
    int status_code = 0;
    HttpHeaders headers;
    std::string body;
};

// ---------------------------------------------------------------------------
// PollStatus — the state of an async ADT operation.
// ---------------------------------------------------------------------------
enum class PollStatus {
    Running,
    Completed,
    Failed,
};

// ---------------------------------------------------------------------------
// PollResult — returned by PollUntilComplete.
// ---------------------------------------------------------------------------
struct PollResult {
    PollStatus status = PollStatus::Running;
    std::string body;
    std::chrono::milliseconds elapsed{0};
};

// ---------------------------------------------------------------------------
// IAdtSession — abstract HTTP session interface for ADT operations.
//
// All ADT operation modules (packages, abapgit, activation) depend on this
// interface rather than a concrete HTTP client. This enables offline testing
// via MockAdtSession.
//
// Methods return Result<T, Error> — never throw on expected failures.
// ---------------------------------------------------------------------------
class IAdtSession {
public:
    virtual ~IAdtSession() = default;

    // Non-copyable, non-movable (polymorphic base).
    IAdtSession(const IAdtSession&) = delete;
    IAdtSession& operator=(const IAdtSession&) = delete;
    IAdtSession(IAdtSession&&) = delete;
    IAdtSession& operator=(IAdtSession&&) = delete;

    // -- HTTP verbs ----------------------------------------------------------

    [[nodiscard]] virtual Result<HttpResponse, Error> Get(
        std::string_view path,
        const HttpHeaders& headers = {}) = 0;

    [[nodiscard]] virtual Result<HttpResponse, Error> Post(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers = {}) = 0;

    [[nodiscard]] virtual Result<HttpResponse, Error> Put(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers = {}) = 0;

    [[nodiscard]] virtual Result<HttpResponse, Error> Delete(
        std::string_view path,
        const HttpHeaders& headers = {}) = 0;

    // -- Stateful session ----------------------------------------------------
    // Stateful sessions maintain a SAP context ID across requests, required
    // for operations like object locking.

    virtual void SetStateful(bool enabled) = 0;
    [[nodiscard]] virtual bool IsStateful() const = 0;

    // -- CSRF ----------------------------------------------------------------

    [[nodiscard]] virtual Result<std::string, Error> FetchCsrfToken() = 0;

    // -- Async polling -------------------------------------------------------

    [[nodiscard]] virtual Result<PollResult, Error> PollUntilComplete(
        std::string_view location_url,
        std::chrono::seconds timeout) = 0;

protected:
    IAdtSession() = default;
};

} // namespace erpl_adt
