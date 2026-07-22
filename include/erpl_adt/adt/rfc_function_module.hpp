#pragma once

#include <erpl_adt/adt/ddic.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// RfcParameter — one IMPORTING/EXPORTING/TABLES/CHANGING parameter of a
// function module's interface.
//
// `type_name` is the DDIC structure/data-element/table-type the parameter
// is declared against (via TYPE or LIKE), uppercased, with any trailing
// `-FIELD` component of a `LIKE struct-field` reference stripped — that's
// the structure being *used*, which is what a catalog edge should point at,
// not the individual field. std::nullopt when the parameter's declaration
// couldn't be parsed (e.g. an inline anonymous TYPE, or a raising/
// exceptions line, which this parser deliberately doesn't attempt to
// resolve to a DDIC object).
// ---------------------------------------------------------------------------
struct RfcParameter {
    std::string name;
    std::string kind;  // "importing" | "exporting" | "tables" | "changing"
    std::optional<std::string> type_name;
    bool is_optional = false;
    std::optional<std::string> default_value;  // from a `default '...'` clause
};

struct RfcSignature {
    std::vector<RfcParameter> parameters;
};

// ---------------------------------------------------------------------------
// ParseRfcSignature — pure parsing, no I/O. Extracts the IMPORTING/
// EXPORTING/TABLES/CHANGING parameter declarations from a function module's
// ABAP source text (the `FUNCTION ... .` interface header, before the
// executable statement block begins).
//
// Deliberately does not attempt to resolve RAISING/EXCEPTIONS entries (class
// exceptions and classic exceptions aren't DDIC structure references, so
// they're out of scope for the "what DDIC objects does this FM use" edge
// this parser feeds into) — encountering either section clears the current
// parameter kind (so any trailing content isn't mis-attributed) without
// erroring.
// ---------------------------------------------------------------------------
[[nodiscard]] RfcSignature ParseRfcSignature(std::string_view source);

// ---------------------------------------------------------------------------
// ListFunctionModules — the function modules that belong to one function
// group (children of a FUGR/F object, e.g. "SU_USER").
//
// Endpoint: POST /sap/bc/adt/repository/nodestructure
//           ?parent_type=FUGR/F&parent_name={group}&withShortDescriptions=true
// Reuses ParseNodeStructure via the same PackageEntry shape ListPackageTree
// uses — a node-structure listing isn't DEVC-specific, it's generic over
// `parent_type`.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<PackageEntry>, Error> ListFunctionModules(
    IAdtSession& session, const std::string& function_group);

// ---------------------------------------------------------------------------
// GetFunctionModuleSignature — reads a function module's source
// (`{object_uri}/source/main`) and parses its interface.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<RfcSignature, Error> GetFunctionModuleSignature(
    IAdtSession& session, const std::string& function_module_object_uri);

} // namespace erpl_adt
