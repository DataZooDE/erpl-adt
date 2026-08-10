#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <string_view>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Packages — free functions for ADT package operations.
//
// GET  /sap/bc/adt/packages/{name}  — read a package (200=exists)
// POST /sap/bc/adt/packages         — create a new package
// ---------------------------------------------------------------------------

// How a package's existence was established. SAP_BASIS 7.40 has no
// per-package object resource, so a 404 from it proves nothing and the
// repository information system is consulted instead.
enum class PackageResolution {
    PackageResource,  // GET /sap/bc/adt/packages/{name} answered 200
    Search,           // resolved through the repository information system
};

struct PackageExistence {
    bool exists = false;
    PackageResolution resolved_via = PackageResolution::PackageResource;
};

[[nodiscard]] const char* ToString(PackageResolution resolution);

// Determine whether a package exists.
//
// A 404 from the package resource is NOT treated as proof of absence: on
// SAP_BASIS 7.40 that resource does not exist at all and answers 404 for every
// package, existing or not (GitHub issue #35). In that case existence is
// resolved through the repository information system search, which is
// available on every release. A search that itself fails yields an error —
// never a silent "does not exist".
[[nodiscard]] Result<PackageExistence, Error> ResolvePackageExistence(
    IAdtSession& session,
    const PackageName& package_name);

[[nodiscard]] Result<bool, Error> PackageExists(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name);

[[nodiscard]] Result<PackageInfo, Error> CreatePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component,
    std::string_view responsible);

[[nodiscard]] Result<PackageInfo, Error> EnsurePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component,
    std::string_view responsible);

} // namespace erpl_adt
