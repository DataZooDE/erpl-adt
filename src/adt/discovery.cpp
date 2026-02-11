#include <erpl_adt/adt/discovery.hpp>

namespace erpl_adt {

namespace {

const char* kDiscoveryPath = "/sap/bc/adt/discovery";

} // namespace

Result<DiscoveryResult, Error> Discover(
    IAdtSession& session,
    const IXmlCodec& codec) {

    auto response = session.Get(kDiscoveryPath);
    if (response.IsErr()) {
        return Result<DiscoveryResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<DiscoveryResult, Error>::Err(
            Error::FromHttpStatus("Discover", kDiscoveryPath, http.status_code, http.body));
    }

    return codec.ParseDiscoveryResponse(http.body);
}

bool HasAbapGitSupport(const DiscoveryResult& discovery) {
    return discovery.has_abapgit_support;
}

} // namespace erpl_adt
