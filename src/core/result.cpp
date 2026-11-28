#include <erpl_adt/core/result.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace erpl_adt {

namespace {

std::string Lowercase(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(lower),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

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

// ICF failures can be HTML rather than an ADT XML error.
std::optional<std::string> ExtractHtmlTitle(const std::string& body) {
    const auto lower = Lowercase(body);
    auto title_pos = lower.find("<title");
    if (title_pos == std::string::npos) return std::nullopt;
    const auto after_name = title_pos + 6;
    if (after_name >= lower.size()) return std::nullopt;
    const char next = lower[after_name];
    if (next != '>' && next != ' ' && next != '\t' &&
        next != '\n' && next != '\r') return std::nullopt;

    auto content_start = lower.find('>', after_name);
    if (content_start == std::string::npos) return std::nullopt;
    ++content_start;
    auto content_end = lower.find("</title>", content_start);
    if (content_end == std::string::npos) return std::nullopt;

    auto first = body.find_first_not_of(" \t\r\n", content_start);
    if (first == std::string::npos || first >= content_end) return std::nullopt;
    auto last = body.find_last_not_of(" \t\r\n", content_end - 1);
    if (last == std::string::npos || last < first) return std::nullopt;
    return body.substr(first, std::min<size_t>(last - first + 1, 512));
}

// Keep visible diagnostic text, never CSS, scripts or embedded images. SAP's
// title is often generic; the status/reason lives in the body (issues #102/103).
std::optional<std::string> ExtractHtmlDiagnostic(const std::string& body) {
    const auto title = ExtractHtmlTitle(body);
    const auto lower = Lowercase(body);
    auto start = lower.find("<body");
    if (start == std::string::npos) return title;
    start = lower.find('>', start);
    if (start == std::string::npos) return title;
    std::string text;
    const auto end = std::min(body.size(), start + 16384);
    for (size_t i = start + 1; i < end && text.size() < 512;) {
        if (body[i] == '<') {
            if (lower.compare(i, 7, "</body>") == 0) break;
            if (lower.compare(i, 7, "<script") == 0 || lower.compare(i, 6, "<style") == 0) {
                const auto close = lower.find(lower.compare(i, 7, "<script") == 0
                                                  ? "</script>" : "</style>", i);
                if (close == std::string::npos) break;
                i = close;
            }
            const auto close = body.find('>', i);
            if (close == std::string::npos) break;
            i = close + 1;
            if (!text.empty() && text.back() != ' ') text += ' ';
        } else if (std::isspace(static_cast<unsigned char>(body[i]))) {
            if (!text.empty() && text.back() != ' ') text += ' ';
            ++i;
        } else {
            text += body[i++];
        }
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (text.empty()) return title;
    return title && text != *title ? *title + ": " + text : text;
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

    return ExtractHtmlDiagnostic(body);
}

// The T100 message class of an ADT exception (<entry key="T100KEY-ID">),
// if the body names one. Language-independent, unlike the message text.
std::optional<std::string> ExtractT100Id(const std::string& body) {
    static const std::string kKey = "key=\"T100KEY-ID\">";
    const auto pos = body.find(kKey);
    if (pos == std::string::npos) return std::nullopt;
    const auto start = pos + kKey.size();
    const auto end = body.find('<', start);
    if (end == std::string::npos || end == start) return std::nullopt;
    return body.substr(start, end - start);
}

// Message classes of the Change and Transport System. An ADT write with
// ?corrNr= that the Transport Organizer refuses ("Task/request X does not
// exist" TR 807, "Object ... is already locked in request X" CTS_WBO_API 020)
// answers HTTP 500/400; it is a transport problem (exit 9), as the engine's
// TK/TR/TO messages are.
bool IsCtsMessageClass(const std::string& id) {
    return id == "TR" || id == "TK" || id == "TO" || id == "CTS_WBO_API";
}

// SAP reports an enqueue conflict as a 403 with an application error rather
// than a 409/423, e.g. "User DEVELOPER is currently editing ZCL_FOO".
bool IsLockConflictText(const std::string& text) {
    const auto lower = Lowercase(text);
    static const char* kPhrases[] = {
        "currently editing",
        "locked by",
        "is being edited",
        "currently being edited",
    };
    for (const char* phrase : kPhrases) {
        if (lower.find(phrase) != std::string::npos) return true;
    }
    return false;
}

} // anonymous namespace

Error Error::FromHttpStatus(const std::string& operation,
                            const std::string& endpoint,
                            int status_code,
                            const std::string& response_body) {
    auto sap_error = ExtractSapError(response_body);
    const bool session_missing = status_code == 400 &&
        ((sap_error && Lowercase(*sap_error).find("session not found") != std::string::npos) ||
         (response_body.find('<') == std::string::npos &&
          Lowercase(response_body).find("session not found") != std::string::npos));
    if (session_missing) {
        sap_error = sap_error.value_or("SAP session refused");
        if (Lowercase(*sap_error).find("session not found") == std::string::npos) {
            *sap_error += ": 400 Session not found";
        }
    }

    ErrorCategory category;
    std::string message;

    switch (status_code) {
        case 400:
            if (session_missing || (sap_error.has_value() &&
                Lowercase(*sap_error).find("service cannot be reached") !=
                    std::string::npos)) {
                category = ErrorCategory::Connection;
                message = (session_missing ? "SAP refused the stateful session: "
                                           : "SAP service unavailable: ") + *sap_error;
            } else {
                category = ErrorCategory::Internal;
                message = sap_error.has_value()
                    ? "Bad request: " + *sap_error
                    : "Bad request";
            }
            break;
        case 401:
            category = ErrorCategory::Authentication;
            message = "Authentication failed — check credentials or run 'erpl-adt login'";
            break;
        case 403:
            // A 403 is only a CSRF problem when there is nothing else to
            // explain it. SAP returns 403 for lock conflicts and for services
            // that are not activated on this release; blaming those on a stale
            // token sends callers after the wrong cause.
            if (sap_error.has_value()) {
                category = IsLockConflictText(*sap_error)
                    ? ErrorCategory::LockConflict
                    : ErrorCategory::Authorization;
                message = "Forbidden: " + *sap_error;
            } else if (operation == "FetchCsrfToken") {
                // We were fetching the token, so there was no token to be
                // invalid. This is the shape an unactivated ICF node returns.
                category = ErrorCategory::Authorization;
                message = "Forbidden: CSRF token fetch rejected — the service "
                          "may not be activated, or the user may lack "
                          "authorization for it";
            } else {
                category = ErrorCategory::CsrfToken;
                message = "Forbidden — CSRF token may be invalid";
            }
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

    if ((status_code == 400 || status_code == 500) && category == ErrorCategory::Internal) {
        if (const auto t100 = ExtractT100Id(response_body);
            t100.has_value() && IsCtsMessageClass(*t100)) {
            category = ErrorCategory::TransportError;
        }
    }

    Error error{operation, endpoint, status_code, message, sap_error, category};
    if (status_code == 400 && category == ErrorCategory::Connection) {
        error.hint = "The SAP service may still be starting. Wait a few minutes "
                     "and retry; if it persists, check service availability.";
    }
    error.session_failure = session_missing;
    if (session_missing) {
        error.hint = "SAP refused the stateful session. The user may have too many open "
                     "security sessions, or the context has expired. Check SM05 / SM04; "
                     "end unused sessions or wait for http/security_session_timeout "
                     "(often 30 minutes). Older clients may not log off. This run has stopped.";
    }
    if (status_code == 429) {
        if (sap_error) error.message += ": " + *sap_error;
        error.hint = "SAP refused further requests/logons. This run has stopped; check "
                     "SM05 / SM04 for open sessions and wait before starting a new run.";
    }
    if (category == ErrorCategory::Authentication) {
        // The most common cause is a credential source the caller did not
        // expect to be in play — a stale .adt.creds outliving a system
        // restart, or an environment variable that is not one of the names
        // actually read.
        error.hint =
            "Credentials resolve as: flags > --password-env > environment "
            "(ERPL_ADT_USER/SAP_USER, ERPL_ADT_PASSWORD/SAP_PASSWORD, ...) > "
            "the .adt.creds file in the current directory. Run 'erpl-adt "
            "login' to refresh a stale .adt.creds, or 'erpl-adt logout' to "
            "drop it.";
    }
    return error;
}

} // namespace erpl_adt
