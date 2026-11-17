#pragma once

#include <erpl_adt/core/result.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Function groups in abapGit format (serializer LCL_OBJECT_FUGR) — the pure
// part: reading <fg>.fugr.xml and turning an abapGit function module into the
// source form ADT writes.
//
// abapGit stores a function module the way SE37 keeps it in its include: the
// FUNCTION statement without parameters, followed by a generated `*"` comment
// block describing the interface, and the interface itself as tables
// (RSIMP/RSEXP/RSCHA/RSTBL/RSEXC) in the FUNCTIONS section of the XML.
// ADT instead reads and writes the "signature form": the interface is part
// of the FUNCTION statement (`FUNCTION z_x IMPORTING VALUE(iv) TYPE t ...`).
// On save SAP derives the interface from that statement and regenerates the
// comment block, so writing the signature form through ADT reproduces
// exactly the include abapGit serialized.
//
// The rendering mirrors SAP's own builder (CL_FB_DOLLAR_SOURCE_CONVERTER,
// local class LCL_SIGNATURE_SOURCE_BUILDER) applied to the parameter tables
// converted as in CL_FB_PARAMETER_CONVERSION=>CONVERT_PARAMETER_OLD_TO_FUPA.
// ---------------------------------------------------------------------------

struct FmParameter {
    std::string name;
    std::string dbfield;        // RSIMP/RSEXP/RSCHA-DBFIELD, RSTBL-DBSTRUCT (LIKE / STRUCTURE)
    std::string typ;            // TYP (TYPE, or LIKE when types == "O")
    std::string types;          // TYPES ("O": long LIKE reference kept in TYP)
    bool reference = false;     // REFERENCE = X: pass by reference
    bool optional = false;
    bool ref_class = false;     // REF_CLASS = X: TYPE REF TO
    std::string default_value;  // DEFAULT
};

struct FmException {
    std::string name;
    bool resumable = false;
};

struct FmInterface {
    std::string name;  // FUNCNAME
    std::string short_text;
    std::vector<FmParameter> importing;
    std::vector<FmParameter> exporting;
    std::vector<FmParameter> changing;
    std::vector<FmParameter> tables;
    std::vector<FmException> exceptions;
    bool class_based_exceptions = false;  // EXCEPTION_CLASSES = X: RAISING instead of EXCEPTIONS
    std::string remote_call;              // REMOTE_CALL (R = RFC)
    std::string update_task;              // UPDATE_TASK
    std::string item_xml;                 // the compact <item> element, for change detection
};

struct FugrDefinition {
    std::string areat;                  // function group short text
    std::vector<std::string> includes;  // INCLUDES (incl. SAPL<fg>), upper case
    std::vector<FmInterface> functions;
};

// Parses the compact <asx:values> children of <fg>.fugr.xml.
[[nodiscard]] Result<FugrDefinition, Error> ParseFugrValues(std::string_view values_xml);

// The ADT signature form of `fm`: interface rendered from the parameter
// tables, followed by the body of `abapgit_source` (everything after the
// `*"` comment block). Line ends are LF.
[[nodiscard]] Result<std::string, Error> BuildFmSignatureSource(const FmInterface& fm,
                                                               std::string_view abapgit_source);

// Just the signature statement (FUNCTION ... .), one line per element.
[[nodiscard]] std::vector<std::string> BuildFmSignatureLines(const FmInterface& fm);

// abapGit's file-name part for a sub-object (include or function module):
// lower case, '/' -> '#'. "LZFGTOP" -> "lzfgtop", "/NS/FM" -> "#ns#fm".
[[nodiscard]] std::string FugrPartName(std::string_view name);

}  // namespace erpl_adt
