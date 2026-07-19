#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CdsField — one exposed element of a CDS view (a real field or an exposed
// association). Associations appear as a field entry with no `as` alias;
// `is_association` distinguishes them.
// ---------------------------------------------------------------------------
struct CdsField {
    std::string name;              // exposed alias, or the association identifier
    std::string source_expression; // the raw expression before `as`, e.g. "devclass"
    bool is_key = false;
    bool is_association = false;
    std::vector<std::string> annotations;  // raw "@Foo.bar: value" lines attached to this field
};

// ---------------------------------------------------------------------------
// CdsAssociation — an `association ... to <Target> as <Alias> on <cond>` or
// `composition ... of <Target> as <Alias>` clause.
// ---------------------------------------------------------------------------
struct CdsAssociation {
    std::string alias;
    std::string target;
    std::string cardinality;   // e.g. "[0..1]", "[1..1]", "[0..*]" — empty if unspecified
    std::string on_condition;  // empty for composition (no `on` clause)
    bool is_composition = false;
    bool to_parent = false;
};

// ---------------------------------------------------------------------------
// CdsViewInfo — structural parse of a CDS view's DDL source.
// ---------------------------------------------------------------------------
struct CdsViewInfo {
    std::string entity_name;   // name after "define view entity"
    std::string source_table;  // table/view after "as select from"
    std::vector<CdsField> fields;
    std::vector<CdsAssociation> associations;
};

// ---------------------------------------------------------------------------
// ParseCdsSource — structural parse of ABAP CDS DDL source text.
//
// Text-based parse (no ABAP CDS compiler available over ADT) — extracts
// entity name, source table, association/composition declarations, and the
// field list (with `key` flag and `@`-annotations), but does not resolve
// field data types (that requires the CDS compiler, which erpl-adt cannot
// invoke over the REST API). Malformed/unsupported source returns a
// best-effort partial CdsViewInfo rather than an error, mirroring
// ParseFieldsFromDdl's tolerant style for TABL/DT sources.
// ---------------------------------------------------------------------------
[[nodiscard]] CdsViewInfo ParseCdsSource(const std::string& ddl_source);

// ---------------------------------------------------------------------------
// GetCdsStructure — fetch and structurally parse a CDS view's DDL source.
//
// Endpoint: GET /sap/bc/adt/ddic/ddl/sources/{cdsName}/source/main
// ---------------------------------------------------------------------------
[[nodiscard]] Result<CdsViewInfo, Error> GetCdsStructure(
    IAdtSession& session,
    const std::string& cds_name);

} // namespace erpl_adt
