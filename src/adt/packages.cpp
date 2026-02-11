#include <erpl_adt/adt/packages.hpp>

#include <iomanip>
#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

// URL-encode a package name. Slashes and special characters must be
// percent-encoded in the path segment (e.g. /DMO/FLIGHT -> %2FDMO%2FFLIGHT).
std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string PackagePath(const PackageName& name) {
    return "/sap/bc/adt/packages/" + UrlEncode(name.Value());
}

const char* kPackagesPath = "/sap/bc/adt/packages";
const char* kPackageContentType = "application/vnd.sap.adt.packages.v1+xml";

} // namespace

Result<bool, Error> PackageExists(
    IAdtSession& session,
    const IXmlCodec& /*codec*/,
    const PackageName& package_name) {

    auto path = PackagePath(package_name);
    auto response = session.Get(path);
    if (response.IsErr()) {
        return Result<bool, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 200) {
        return Result<bool, Error>::Ok(true);
    }
    if (http.status_code == 404) {
        return Result<bool, Error>::Ok(false);
    }

    return Result<bool, Error>::Err(
        Error::FromHttpStatus("PackageExists", path, http.status_code, http.body));
}

Result<PackageInfo, Error> CreatePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component) {

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<PackageInfo, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildPackageCreateXml(package_name, description, software_component);
    if (xml.IsErr()) {
        return Result<PackageInfo, Error>::Err(std::move(xml).Error());
    }

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kPackagesPath, xml.Value(), kPackageContentType, headers);
    if (response.IsErr()) {
        return Result<PackageInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<PackageInfo, Error>::Err(
            Error::FromHttpStatus("CreatePackage", kPackagesPath, http.status_code, http.body));
    }

    return codec.ParsePackageResponse(http.body);
}

Result<PackageInfo, Error> EnsurePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component) {

    auto exists = PackageExists(session, codec, package_name);
    if (exists.IsErr()) {
        return Result<PackageInfo, Error>::Err(std::move(exists).Error());
    }

    if (exists.Value()) {
        // Package already exists — return its info.
        auto path = PackagePath(package_name);
        auto response = session.Get(path);
        if (response.IsErr()) {
            return Result<PackageInfo, Error>::Err(std::move(response).Error());
        }
        return codec.ParsePackageResponse(response.Value().body);
    }

    return CreatePackage(session, codec, package_name, description, software_component);
}

} // namespace erpl_adt
