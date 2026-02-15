#include <erpl_adt/core/result.hpp>

namespace erpl_adt {

namespace {

// Extract text between XML tags using simple string search.
// No tinyxml2 dependency — core/ must not depend on adt/ libraries.
std::optional<std::string> ExtractXmlMessage(const std::string& body,
                                              const std::string& open_tag,
                                              const std::string& close_tag) {
    auto start = body.find(open_tag);
    if (start == std::string::npos) return std::nullopt;
    start += open_tag.size();
    auto end = body.find(close_tag, start);
    if (end == std::string::npos) return std::nullopt;
    auto msg = body.substr(start, end - start);
    if (msg.empty()) return std::nullopt;
    return msg;
}

// Try to extract a human-readable SAP error message from an XML response body.
// SAP ADT uses two common patterns:
//   <message>...</message>  (communicationframework namespace)
//   <exc:message>...</exc:message>  (ADT exceptions namespace)
std::optional<std::string> ExtractSapError(const std::string& body) {
    if (body.empty()) return std::nullopt;

    // Try ADT exception message first (more specific).
    auto msg = ExtractXmlMessage(body, "<exc:message>", "</exc:message>");
    if (msg.has_value()) return msg;

    // Try communicationframework message.
    msg = ExtractXmlMessage(body, "<message>", "</message>");
    if (msg.has_value()) return msg;

    return std::nullopt;
}

} // anonymous namespace

Error Error::FromHttpStatus(const std::string& operation,
                            const std::string& endpoint,
                            int status_code,
                            const std::string& response_body) {
    auto sap_error = ExtractSapError(response_body);

    ErrorCategory category;
    std::string message;

    switch (status_code) {
        case 400:
            category = ErrorCategory::Internal;
            message = sap_error.has_value()
                ? "Bad request: " + *sap_error
                : "Bad request";
            break;
        case 401:
            category = ErrorCategory::Authentication;
            message = "Authentication failed — check credentials or run 'erpl-adt login'";
            break;
        case 403:
            category = ErrorCategory::CsrfToken;
            message = "Forbidden — CSRF token may be invalid";
            break;
        case 404:
            category = ErrorCategory::NotFound;
            message = "Not found";
            break;
        case 409:
            category = ErrorCategory::LockConflict;
            message = "Conflict — resource may be locked by another user";
            break;
        case 423:
            category = ErrorCategory::LockConflict;
            message = "Resource is locked";
            break;
        case 500:
            category = ErrorCategory::Internal;
            message = sap_error.has_value()
                ? "SAP server error: " + *sap_error
                : "SAP server internal error";
            break;
        case 408:
            category = ErrorCategory::Timeout;
            message = "Request timed out";
            break;
        case 429:
            category = ErrorCategory::Timeout;
            message = "Too many requests — retry later";
            break;
        case 502:
        case 503:
        case 504:
            category = ErrorCategory::Connection;
            message = "SAP server unavailable";
            break;
        default:
            category = ErrorCategory::Internal;
            message = "Unexpected HTTP " + std::to_string(status_code);
            break;
    }

    return Error{operation, endpoint, status_code, message, sap_error, category};
}

} // namespace erpl_adt
