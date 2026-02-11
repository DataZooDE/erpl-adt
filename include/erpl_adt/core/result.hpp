#pragma once

#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Result<T, E> — a discriminated union that holds either a value or an error.
// ---------------------------------------------------------------------------
template <typename T, typename E>
class Result {
public:
    // -- Factories ----------------------------------------------------------

    static Result Ok(const T& value) { return Result(OkTag{}, value); }
    static Result Ok(T&& value) { return Result(OkTag{}, std::move(value)); }

    static Result Err(const E& error) { return Result(ErrTag{}, error); }
    static Result Err(E&& error) { return Result(ErrTag{}, std::move(error)); }

    // -- Query --------------------------------------------------------------

    [[nodiscard]] bool IsOk() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] bool IsErr() const noexcept { return storage_.index() == 1; }
    explicit operator bool() const noexcept { return IsOk(); }

    // -- Access (const&) ----------------------------------------------------

    [[nodiscard]] const T& Value() const& {
        assert(IsOk() && "Value() called on an Err Result");
        return std::get<0>(storage_);
    }

    [[nodiscard]] const E& Error() const& {
        assert(IsErr() && "Error() called on an Ok Result");
        return std::get<1>(storage_);
    }

    // -- Access (&&) --------------------------------------------------------

    [[nodiscard]] T Value() && {
        assert(IsOk() && "Value() called on an Err Result");
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] E Error() && {
        assert(IsErr() && "Error() called on an Ok Result");
        return std::get<1>(std::move(storage_));
    }

    // -- ValueOr ------------------------------------------------------------

    [[nodiscard]] T ValueOr(T default_value) const& {
        if (IsOk()) {
            return std::get<0>(storage_);
        }
        return default_value;
    }

    [[nodiscard]] T ValueOr(T default_value) && {
        if (IsOk()) {
            return std::get<0>(std::move(storage_));
        }
        return default_value;
    }

    // -- Monadic: AndThen ---------------------------------------------------
    // fn: T -> Result<U, E>

    template <typename Fn>
    auto AndThen(Fn&& fn) const& -> std::invoke_result_t<Fn, const T&> {
        using ReturnType = std::invoke_result_t<Fn, const T&>;
        if (IsOk()) {
            return std::forward<Fn>(fn)(std::get<0>(storage_));
        }
        return ReturnType::Err(std::get<1>(storage_));
    }

    template <typename Fn>
    auto AndThen(Fn&& fn) && -> std::invoke_result_t<Fn, T&&> {
        using ReturnType = std::invoke_result_t<Fn, T&&>;
        if (IsOk()) {
            return std::forward<Fn>(fn)(std::get<0>(std::move(storage_)));
        }
        return ReturnType::Err(std::get<1>(std::move(storage_)));
    }

    // -- Monadic: Map -------------------------------------------------------
    // fn: T -> U

    template <typename Fn>
    auto Map(Fn&& fn) const& -> Result<std::invoke_result_t<Fn, const T&>, E> {
        using U = std::invoke_result_t<Fn, const T&>;
        if (IsOk()) {
            return Result<U, E>::Ok(std::forward<Fn>(fn)(std::get<0>(storage_)));
        }
        return Result<U, E>::Err(std::get<1>(storage_));
    }

    template <typename Fn>
    auto Map(Fn&& fn) && -> Result<std::invoke_result_t<Fn, T&&>, E> {
        using U = std::invoke_result_t<Fn, T&&>;
        if (IsOk()) {
            return Result<U, E>::Ok(std::forward<Fn>(fn)(std::get<0>(std::move(storage_))));
        }
        return Result<U, E>::Err(std::get<1>(std::move(storage_)));
    }

private:
    struct OkTag {};
    struct ErrTag {};

    Result(OkTag, const T& value) : storage_(std::in_place_index<0>, value) {}
    Result(OkTag, T&& value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(ErrTag, const E& error) : storage_(std::in_place_index<1>, error) {}
    Result(ErrTag, E&& error) : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<T, E> storage_;
};

// ---------------------------------------------------------------------------
// Result<void, E> — specialization for operations that succeed with no value.
// ---------------------------------------------------------------------------
template <typename E>
class Result<void, E> {
public:
    static Result Ok() { return Result(OkTag{}); }

    static Result Err(const E& error) { return Result(ErrTag{}, error); }
    static Result Err(E&& error) { return Result(ErrTag{}, std::move(error)); }

    [[nodiscard]] bool IsOk() const noexcept { return !error_.has_value(); }
    [[nodiscard]] bool IsErr() const noexcept { return error_.has_value(); }
    explicit operator bool() const noexcept { return IsOk(); }

    [[nodiscard]] const E& Error() const& {
        assert(IsErr() && "Error() called on an Ok Result");
        return *error_;
    }

    [[nodiscard]] E Error() && {
        assert(IsErr() && "Error() called on an Ok Result");
        return std::move(*error_);
    }

private:
    struct OkTag {};
    struct ErrTag {};

    explicit Result(OkTag) : error_(std::nullopt) {}
    Result(ErrTag, const E& error) : error_(error) {}
    Result(ErrTag, E&& error) : error_(std::move(error)) {}

    std::optional<E> error_;
};

// ---------------------------------------------------------------------------
// ErrorCategory — classifies errors for exit codes and structured output.
// ---------------------------------------------------------------------------
enum class ErrorCategory {
    Connection,
    Authentication,
    CsrfToken,
    NotFound,
    PackageError,
    CloneError,
    PullError,
    ActivationError,
    LockConflict,
    TestFailure,
    CheckError,
    TransportError,
    Timeout,
    Internal,
};

// ---------------------------------------------------------------------------
// Error — structured error type for ADT operations.
// ---------------------------------------------------------------------------
struct Error {
    std::string operation;
    std::string endpoint;
    std::optional<int> http_status;
    std::string message;
    std::optional<std::string> sap_error;
    ErrorCategory category = ErrorCategory::Internal;

    /// Create an Error from an HTTP status code with human-readable messages.
    /// Extracts SAP error messages from XML response bodies.
    static Error FromHttpStatus(const std::string& operation,
                                const std::string& endpoint,
                                int status_code,
                                const std::string& response_body = "");

    [[nodiscard]] int ExitCode() const {
        switch (category) {
            case ErrorCategory::Connection:      return 1;
            case ErrorCategory::Authentication:  return 1;
            case ErrorCategory::CsrfToken:       return 1;
            case ErrorCategory::NotFound:        return 2;
            case ErrorCategory::PackageError:    return 2;
            case ErrorCategory::CloneError:      return 3;
            case ErrorCategory::PullError:       return 4;
            case ErrorCategory::ActivationError: return 5;
            case ErrorCategory::LockConflict:    return 6;
            case ErrorCategory::TestFailure:     return 7;
            case ErrorCategory::CheckError:      return 8;
            case ErrorCategory::TransportError:  return 9;
            case ErrorCategory::Timeout:         return 10;
            case ErrorCategory::Internal:        return 99;
        }
        return 99;
    }

    [[nodiscard]] std::string CategoryName() const {
        switch (category) {
            case ErrorCategory::Connection:      return "connection";
            case ErrorCategory::Authentication:  return "authentication";
            case ErrorCategory::CsrfToken:       return "csrf_token";
            case ErrorCategory::NotFound:        return "not_found";
            case ErrorCategory::PackageError:    return "package";
            case ErrorCategory::CloneError:      return "clone";
            case ErrorCategory::PullError:       return "pull";
            case ErrorCategory::ActivationError: return "activation";
            case ErrorCategory::LockConflict:    return "lock_conflict";
            case ErrorCategory::TestFailure:     return "test_failure";
            case ErrorCategory::CheckError:      return "check";
            case ErrorCategory::TransportError:  return "transport";
            case ErrorCategory::Timeout:         return "timeout";
            case ErrorCategory::Internal:        return "internal";
        }
        return "internal";
    }

    [[nodiscard]] std::string ToString() const {
        std::ostringstream oss;
        oss << operation;
        if (!endpoint.empty()) {
            oss << " [" << endpoint << "]";
        }
        if (http_status.has_value()) {
            oss << " (HTTP " << *http_status << ")";
        }
        oss << ": " << message;
        if (sap_error.has_value() && !sap_error->empty()) {
            oss << " — SAP: " << *sap_error;
        }
        return oss.str();
    }

    [[nodiscard]] std::string ToJson() const {
        std::ostringstream oss;
        oss << R"({"error":{)";
        oss << R"("category":")" << CategoryName() << R"(",)";
        oss << R"("operation":")" << operation << R"(",)";
        if (!endpoint.empty()) {
            oss << R"("endpoint":")" << endpoint << R"(",)";
        }
        if (http_status.has_value()) {
            oss << R"("http_status":)" << *http_status << R"(,)";
        }
        oss << R"("message":")" << message << R"(",)";
        if (sap_error.has_value() && !sap_error->empty()) {
            oss << R"("sap_error":")" << *sap_error << R"(",)";
        }
        oss << R"("exit_code":)" << ExitCode();
        oss << R"(}})";
        return oss.str();
    }

    friend std::ostream& operator<<(std::ostream& os, const Error& e) {
        return os << e.ToString();
    }

    bool operator==(const Error& other) const {
        return operation == other.operation &&
               endpoint == other.endpoint &&
               http_status == other.http_status &&
               message == other.message &&
               sap_error == other.sap_error &&
               category == other.category;
    }

    bool operator!=(const Error& other) const {
        return !(*this == other);
    }
};

} // namespace erpl_adt
