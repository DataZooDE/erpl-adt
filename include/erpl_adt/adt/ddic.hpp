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
    std::string abap_type;     // ABAP primitive: CHAR, CLNT, NUMC, DATS, CURR, CUKY, INT4…
    std::string check_table;   // FK target table, e.g. "T000", "SCARR" (empty if none)
    std::string description;
    bool key_field = false;
    std::optional<int> length;    // field length (chars/bytes depending on type)
    std::optional<int> decimals;  // decimal places (for P/F/currency types)
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
//
// When resolve_types is true (default), DDL-format table fields (TABL/DT)
// are enriched with length and description by fetching each referenced data
// element from /sap/bc/adt/ddic/dataelements/{name}. Built-in abap.* types
// (e.g. abap.curr(15,2)) have their length/decimals extracted from the type
// string without any extra request. Pass resolve_types=false to skip this
// and get a fast, offline-parseable result (field names and types only).
// ---------------------------------------------------------------------------
[[nodiscard]] Result<TableInfo, Error> GetTableDefinition(
    IAdtSession& session,
    const std::string& table_name,
    bool resolve_types = true);

// ---------------------------------------------------------------------------
// GetCdsSource — read CDS view source code.
//
// Endpoint: GET /sap/bc/adt/ddic/ddl/sources/{cdsName}/source/main
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> GetCdsSource(
    IAdtSession& session,
    const std::string& cds_name);

} // namespace erpl_adt
