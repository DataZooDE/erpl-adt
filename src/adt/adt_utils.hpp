#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace erpl_adt::adt_utils {

inline bool IEquals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        const auto lc = static_cast<unsigned char>(lhs[i]);
        const auto rc = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(lc) != std::tolower(rc)) {
            return false;
        }
    }
    return true;
}

inline std::optional<std::string> FindHeaderValueCi(const HttpHeaders& headers,
                                                    std::string_view key) {
    for (const auto& [k, v] : headers) {
        if (IEquals(k, key)) {
            return v;
        }
    }
    return std::nullopt;
}

inline Result<std::string, Error> RequireHeaderCi(const HttpHeaders& headers,
                                                  std::string_view key,
                                                  std::string_view operation,
                                                  std::string_view endpoint,
                                                  int status_code) {
    auto value = FindHeaderValueCi(headers, key);
    if (!value.has_value()) {
        return Result<std::string, Error>::Err(Error{
            std::string(operation),
            std::string(endpoint),
            status_code,
            "HTTP " + std::to_string(status_code) + " response missing required '" +
                std::string(key) + "' header",
            std::nullopt,
            ErrorCategory::Internal});
    }
    return Result<std::string, Error>::Ok(std::move(*value));
}

inline bool HasStatus(int status_code, std::initializer_list<int> expected) {
    return std::find(expected.begin(), expected.end(), status_code) !=
           expected.end();
}

inline std::optional<Error> ParseXmlOrError(tinyxml2::XMLDocument& doc,
                                            std::string_view xml,
                                            std::string_view operation,
                                            std::string_view endpoint,
                                            std::string_view context,
                                            ErrorCategory category =
                                                ErrorCategory::Internal) {
    if (doc.Parse(xml.data(), xml.size()) == tinyxml2::XML_SUCCESS) {
        return std::nullopt;
    }

    std::string message(context);
    if (const char* err = doc.ErrorStr(); err != nullptr && *err != '\0') {
        message += ": ";
        message += err;
    }
    const int line = doc.ErrorLineNum();
    if (line > 0) {
        message += " (line ";
        message += std::to_string(line);
        message += ")";
    }

    return Error{
        std::string(operation),
        std::string(endpoint),
        std::nullopt,
        std::move(message),
        std::nullopt,
        category};
}

inline std::string XmlEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// ParseXmlWithRoot<T>
//
// Encapsulates the repeated parse → root-check → fn(root) pattern that
// appears in every ADT module. Eliminates the tinyxml2 boilerplate and
// ensures consistent error messages for parse and empty-response failures.
//
// Usage:
//   return adt_utils::ParseXmlWithRoot<MyResult>(
//       http.body, "MyOperation", kMyPath, "context msg",
//       [](const tinyxml2::XMLElement* root) {
//           ...parse root...
//           return Result<MyResult, Error>::Ok(...);
//       });
// ---------------------------------------------------------------------------
template <typename T, typename Fn>
Result<T, Error> ParseXmlWithRoot(std::string_view xml,
                                  std::string_view operation,
                                  std::string_view endpoint,
                                  std::string_view parse_error_context,
                                  Fn&& fn) {
    tinyxml2::XMLDocument doc;
    if (auto err = ParseXmlOrError(doc, xml, operation, endpoint,
                                   parse_error_context)) {
        return Result<T, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<T, Error>::Err(Error{
            std::string(operation), std::string(endpoint), std::nullopt,
            "Empty response", std::nullopt});
    }

    return std::forward<Fn>(fn)(root);
}

} // namespace erpl_adt::adt_utils
