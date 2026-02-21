#include <erpl_adt/adt/classrun.hpp>

#include <string>

namespace erpl_adt {

namespace {

// Percent-encode '/' → '%2F' so that namespaced names like /DMO/CL_FOO
// are transmitted as %2FDMO%2FCL_FOO in the URL path.
std::string UrlEncodeName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '/') {
            out += "%2F";
        } else {
            out += c;
        }
    }
    return out;
}

// If class_name is a full ADT URI (starts with /sap/bc/adt/), extract the
// last path segment. Otherwise use it as-is.
std::string ExtractClassName(std::string_view class_name) {
    constexpr std::string_view kAdtPrefix = "/sap/bc/adt/";
    if (class_name.substr(0, kAdtPrefix.size()) == kAdtPrefix) {
        auto last_slash = class_name.rfind('/');
        if (last_slash != std::string_view::npos) {
            return std::string(class_name.substr(last_slash + 1));
        }
    }
    return std::string(class_name);
}

} // anonymous namespace

Result<ClassRunResult, Error> RunClass(
    IAdtSession& session,
    std::string_view class_name) {
    const std::string name    = ExtractClassName(class_name);
    const std::string encoded = UrlEncodeName(name);
    const std::string path    = "/sap/bc/adt/oo/classrun/" + encoded;

    HttpHeaders headers;
    headers["Accept"] = "text/plain";

    auto response = session.Post(path, "", "text/plain", headers);
    if (response.IsErr()) {
        return Result<ClassRunResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<ClassRunResult, Error>::Err(
            Error::FromHttpStatus("RunClass", path, http.status_code, http.body));
    }

    return Result<ClassRunResult, Error>::Ok(
        ClassRunResult{std::string(class_name), http.body});
}

} // namespace erpl_adt
