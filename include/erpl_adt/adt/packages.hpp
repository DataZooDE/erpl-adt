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
// GET  /sap/bc/adt/packages/{name}  — check existence (200=yes, 404=no)
// POST /sap/bc/adt/packages         — create a new package
// ---------------------------------------------------------------------------

[[nodiscard]] Result<bool, Error> PackageExists(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name);

[[nodiscard]] Result<PackageInfo, Error> CreatePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component);

[[nodiscard]] Result<PackageInfo, Error> EnsurePackage(
    IAdtSession& session,
    const IXmlCodec& codec,
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component);

} // namespace erpl_adt
