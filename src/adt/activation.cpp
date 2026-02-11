#include <erpl_adt/adt/activation.hpp>

namespace erpl_adt {

namespace {

const char* kInactivePath = "/sap/bc/adt/activation/inactive";
const char* kActivationPath = "/sap/bc/adt/activation";
const char* kActivationContentType = "application/vnd.sap.adt.activation.v1+xml";

} // namespace

Result<std::vector<InactiveObject>, Error> GetInactiveObjects(
    IAdtSession& session,
    const IXmlCodec& codec) {

    auto response = session.Get(kInactivePath);
    if (response.IsErr()) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            Error::FromHttpStatus("GetInactiveObjects", kInactivePath, http.status_code, http.body));
    }

    return codec.ParseInactiveObjectsResponse(http.body);
}

Result<ActivationResult, Error> ActivateAll(
    IAdtSession& session,
    const IXmlCodec& codec,
    const std::vector<InactiveObject>& objects,
    std::chrono::seconds timeout) {

    // Nothing to activate — return a zero-count success.
    if (objects.empty()) {
        return Result<ActivationResult, Error>::Ok(
            ActivationResult{0, 0, 0, {}});
    }

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildActivationXml(objects);
    if (xml.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(xml).Error());
    }

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kActivationPath, xml.Value(), kActivationContentType, headers);
    if (response.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 + Location → poll until complete.
    if (http.status_code == 202) {
        auto it = http.headers.find("Location");
        if (it == http.headers.end()) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateAll", kActivationPath, 202,
                "202 response missing Location header", std::nullopt});
        }

        auto poll = session.PollUntilComplete(it->second, timeout);
        if (poll.IsErr()) {
            return Result<ActivationResult, Error>::Err(std::move(poll).Error());
        }
        if (poll.Value().status == PollStatus::Failed) {
            return Result<ActivationResult, Error>::Err(Error{
                "ActivateAll", kActivationPath, std::nullopt,
                "async activation operation failed", std::nullopt});
        }

        return codec.ParseActivationResponse(poll.Value().body);
    }

    // Synchronous: 200 with activation result.
    if (http.status_code == 200) {
        return codec.ParseActivationResponse(http.body);
    }

    return Result<ActivationResult, Error>::Err(
        Error::FromHttpStatus("ActivateAll", kActivationPath, http.status_code, http.body));
}

} // namespace erpl_adt
