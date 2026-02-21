#include <erpl_adt/core/result.hpp>

namespace erpl_adt {

namespace {

// Extract text content of the first occurrence of an XML element by tag name.
// Handles both plain tags (<message>) and tags with attributes (<message lang="EN">).
// No tinyxml2 dependency — core/ must not depend on adt/ libraries.
std::optional<std::string> ExtractXmlMessage(const std::string& body,
                                              const std::string& tag_name) {
    const std::string open_prefix = "<" + tag_name;
    auto tag_pos = body.find(open_prefix);
    if (tag_pos == std::string::npos) return std::nullopt;

    // Verify the character after the tag name is '>', whitespace, or '/'.
    // This prevents <messages> from matching when we search for <message.
    const size_t after_prefix = tag_pos + open_prefix.size();
    if (after_prefix >= body.size()) return std::nullopt;
    const char next = body[after_prefix];
    if (next != '>' && next != ' ' && next != '\t' && next != '\n' &&
        next != '\r' && next != '/') {
        return std::nullopt;
    }

    // Skip past the closing '>' of the opening tag (which may have attributes).
    auto content_start = body.find('>', after_prefix);
    if (content_start == std::string::npos) return std::nullopt;
    ++content_start;

    const std::string close_tag = "</" + tag_name + ">";
    auto content_end = body.find(close_tag, content_start);
    if (content_end == std::string::npos) return std::nullopt;

    auto msg = body.substr(content_start, content_end - content_start);
    if (msg.empty()) return std::nullopt;
    return msg;
}

// Try to extract a human-readable SAP error message from an XML response body.
// SAP ADT uses several patterns depending on the error path:
//   <exc:message>       — ADT exceptions namespace (most specific)
//   <message>           — communicationframework (may have lang attribute)
//   <localizedMessage>  — communicationframework localised variant
std::optional<std::string> ExtractSapError(const std::string& body) {
    if (body.empty()) return std::nullopt;

    // Try ADT exception message first (most specific).
    auto msg = ExtractXmlMessage(body, "exc:message");
    if (msg.has_value()) return msg;

    // Try communicationframework message (handles <message lang="EN">).
    msg = ExtractXmlMessage(body, "message");
    if (msg.has_value()) return msg;

    // Fallback: localizedMessage element.
    msg = ExtractXmlMessage(body, "localizedMessage");
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
            message = sap_error.has_value()
                ? "Forbidden: " + *sap_error
                : "Forbidden — CSRF token may be invalid";
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
