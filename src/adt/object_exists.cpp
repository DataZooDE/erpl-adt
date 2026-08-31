#include <erpl_adt/adt/object_exists.hpp>

namespace erpl_adt {

Result<void, Error> EnsureObjectExists(IAdtSession& session,
                                       const std::string& uri,
                                       const std::string& operation,
                                       const std::string& what) {
    HttpHeaders headers;
    headers["Accept"] = "application/*";

    auto response = session.Get(uri, headers);
    if (response.IsErr()) {
        // A transport failure is not evidence of absence.
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 200) {
        return Result<void, Error>::Ok();
    }
    if (http.status_code == 404) {
        Error error{operation, uri, 404, what + " does not exist", std::nullopt,
                    ErrorCategory::NotFound};
        error.hint = "Check the name and, for an object URI, that it is the "
                     "one 'search' returned.";
        return Result<void, Error>::Err(std::move(error));
    }

    // Anything else (403 on a protected object, 500, ...) is reported as it
    // came: the caller should see the real reason rather than "missing".
    return Result<void, Error>::Err(
        Error::FromHttpStatus(operation, uri, http.status_code, http.body));
}

}  // namespace erpl_adt
