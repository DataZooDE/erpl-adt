#include <erpl_adt/adt/packages.hpp>

#include <erpl_adt/adt/search.hpp>
#include <erpl_adt/core/url.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace erpl_adt {

namespace {

std::string PackagePath(const PackageName& name) {
    return "/sap/bc/adt/packages/" + UrlEncode(name.Value());
}

const char* kPackagesPath = "/sap/bc/adt/packages";
const char* kPackageContentType = "application/vnd.sap.adt.packages.v1+xml";
const char* kPackageObjectType = "DEVC/K";

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Resolve existence through the repository information system. Available on
// every SAP_BASIS release, including those without a package object resource.
Result<bool, Error> ExistsViaSearch(
    IAdtSession& session,
    const PackageName& package_name) {

    SearchOptions options;
    options.query = package_name.Value();
    options.max_results = 50;
    options.object_type = kPackageObjectType;

    auto results = SearchObjects(session, options);
    if (results.IsErr()) {
        // Could not determine — surface the failure rather than claiming the
        // package is absent.
        return Result<bool, Error>::Err(std::move(results).Error());
    }

    // quickSearch matches by prefix, so require an exact name hit: a query for
    // "SMOI" must not be satisfied by "SMOI_EN".
    const auto wanted = ToUpper(package_name.Value());
    for (const auto& hit : results.Value()) {
        if (ToUpper(hit.name) == wanted &&
            hit.type.rfind("DEVC", 0) == 0) {
            return Result<bool, Error>::Ok(true);
        }
    }
    return Result<bool, Error>::Ok(false);
}

} // namespace

const char* ToString(PackageResolution resolution) {
    switch (resolution) {
        case PackageResolution::PackageResource: return "package_resource";
        case PackageResolution::Search:          return "search";
    }
    return "package_resource";
}

Result<PackageExistence, Error> ResolvePackageExistence(
    IAdtSession& session,
    const PackageName& package_name) {

    auto path = PackagePath(package_name);
    auto response = session.Get(path);
    if (response.IsErr()) {
        return Result<PackageExistence, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 200) {
        return Result<PackageExistence, Error>::Ok(
            {true, PackageResolution::PackageResource});
    }

    // 404 does not prove absence: SAP_BASIS 7.40 has no per-package object
    // resource and answers 404 for every package. Ask the information system.
    if (http.status_code == 404) {
        auto found = ExistsViaSearch(session, package_name);
        if (found.IsErr()) {
            return Result<PackageExistence, Error>::Err(std::move(found).Error());
        }
        return Result<PackageExistence, Error>::Ok(
            {found.Value(), PackageResolution::Search});
    }

    return Result<PackageExistence, Error>::Err(
        Error::FromHttpStatus("PackageExists", path, http.status_code, http.body));
}

Result<bool, Error> PackageExists(
    IAdtSession& session,
    const IXmlCodec& /*codec*/,
    const PackageName& package_name) {

    auto result = ResolvePackageExistence(session, package_name);
    if (result.IsErr()) {
        return Result<bool, Error>::Err(std::move(result).Error());
    }
    return Result<bool, Error>::Ok(result.Value().exists);
}

Result<PackageInfo, Error> CreatePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component,
    std::string_view responsible) {

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<PackageInfo, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildPackageCreateXml(package_name, description,
                                            software_component, responsible);
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
    std::string_view software_component,
    std::string_view responsible) {

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
        const auto& http = response.Value();
        if (http.status_code != 200) {
            // The package exists (established above) but this release does not
            // serve a package object resource, so its attributes are not
            // readable here.
            return Result<PackageInfo, Error>::Err(
                Error::FromHttpStatus("EnsurePackage", path,
                                       http.status_code, http.body));
        }
        return codec.ParsePackageResponse(http.body);
    }

    return CreatePackage(session, codec, package_name, description,
                          software_component, responsible);
}

} // namespace erpl_adt
