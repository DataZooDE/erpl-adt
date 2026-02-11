#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// PackageEntry — a single item inside a package (from node structure).
// ---------------------------------------------------------------------------
struct PackageEntry {
    std::string object_type;    // e.g. "CLAS/OC", "PROG/P"
    std::string object_name;
    std::string object_uri;
    std::string description;
    bool expandable = false;
    std::string package_name;   // set by ListPackageTree to track provenance
};

// ---------------------------------------------------------------------------
// ListPackageContents — list objects inside a package.
//
// Endpoint: POST /sap/bc/adt/repository/nodestructure
//           ?parent_type=DEVC/K&parent_name={pkg}&withShortDescriptions=true
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<PackageEntry>, Error> ListPackageContents(
    IAdtSession& session,
    const std::string& package_name);

// ---------------------------------------------------------------------------
// ListPackageTree — recursively list all objects in a package hierarchy.
//
// BFS traversal: lists root package contents, then sub-packages (DEVC/K),
// collecting all non-package entries. Optional type filter.
// ---------------------------------------------------------------------------
struct PackageTreeOptions {
    std::string root_package;
    std::optional<std::string> type_filter;  // e.g. "TABL", "CLAS"
    int max_depth = 50;                       // safety limit
};

[[nodiscard]] Result<std::vector<PackageEntry>, Error> ListPackageTree(
    IAdtSession& session,
    const PackageTreeOptions& options);

// ---------------------------------------------------------------------------
// TableField — a field in a database table.
// ---------------------------------------------------------------------------
struct TableField {
    std::string name;
    std::string type;          // data element or built-in type
    std::string description;
    bool key_field = false;
};

// ---------------------------------------------------------------------------
// TableInfo — metadata about a database table.
// ---------------------------------------------------------------------------
struct TableInfo {
    std::string name;
    std::string description;
    std::string delivery_class;
    std::vector<TableField> fields;
};

// ---------------------------------------------------------------------------
// GetTableDefinition — fetch table definition metadata.
//
// Endpoint: GET /sap/bc/adt/ddic/tables/{tableName}
// ---------------------------------------------------------------------------
[[nodiscard]] Result<TableInfo, Error> GetTableDefinition(
    IAdtSession& session,
    const std::string& table_name);

// ---------------------------------------------------------------------------
// GetCdsSource — read CDS view source code.
//
// Endpoint: GET /sap/bc/adt/ddic/ddl/sources/{cdsName}/source/main
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> GetCdsSource(
    IAdtSession& session,
    const std::string& cds_name);

} // namespace erpl_adt
