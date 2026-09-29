#pragma once

#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Classic Dynpro (screen painter) support.
//
// ADT has no REST collection for classic screens — confirmed against a live
// system's discovery document and a 404 on
// GET /sap/bc/adt/programs/programs/{prog}/screens. The only genuine path is
// SAP's own repository function modules (RPY_DYNPRO_INSERT, RPY_DYNPRO_READ,
// RS_SCRP_DELETE), executed through the existing `object run`
// (IF_OO_ADT_CLASSRUN) machinery: erpl-adt writes a disposable helper class
// whose source is generated here, activates it, and runs it, then parses the
// plain-text console output this module also generates the ABAP for.
//
// Field shape validated by iterating against a live SAP system
// (see PR description / CHANGELOG for the exact round trip):
//   - RPY_DYNPRO_INSERT's FIELDS_TO_CONTAINERS is only visited for containers
//     that also appear in CONTAINERS — an empty CONTAINERS table silently
//     drops every field. One root container (type/name both blank) is always
//     emitted.
//   - RPY_DYFATT-TYPE is mandatory; 'TEXT' is the plain input/output field
//     kind (as opposed to TEMPLATE/RADIO/CHECK/PUSH/OKCODE/...).
//   - HEADER-NEXTSCREEN may not be '0' (or blank) on this system's release —
//     it must resolve to an existing screen; erpl-adt defaults it to the
//     screen being created (a self-loop), overridable by the caller.
// ---------------------------------------------------------------------------

struct ScreenFieldSpec {
    std::string name;     // ABAP-Cloud-safe screen field name, e.g. "HEADERNAME"
    std::string text;     // field label
    int line = 1;
    int column = 1;
    int length = 20;
};

struct ScreenFieldInfo {
    std::string name;
    int line = 0;
    int column = 0;
    int length = 0;
    bool input = false;
    bool output = false;
};

struct ScreenReadResult {
    std::string program;
    std::string screen;
    int lines = 0;
    int columns = 0;
    std::vector<ScreenFieldInfo> fields;
};

// ---------------------------------------------------------------------------
// GenerateScreenCreateSource / GenerateScreenReadSource /
// GenerateScreenDeleteSource — build the ABAP source of a disposable
// IF_OO_ADT_CLASSRUN class that performs one screen operation and prints a
// machine-parsable result. `class_name` is the bare ABAP class name (used
// only for the CLASS DEFINITION/IMPLEMENTATION statements).
// ---------------------------------------------------------------------------
[[nodiscard]] std::string GenerateScreenCreateSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen,
    const std::string& description,
    const std::string& next_screen,
    int lines,
    int columns,
    const std::vector<ScreenFieldSpec>& fields);

[[nodiscard]] std::string GenerateScreenReadSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen);

[[nodiscard]] std::string GenerateScreenDeleteSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen);

// ---------------------------------------------------------------------------
// ParseScreen{Create,Read,Delete}Output — parse the console output produced
// by the classes generated above into a Result. A non-zero ABAP return code
// (or a message the generated code could not classify) becomes an
// ErrorCategory::ActivationError so screen failures never read as success.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> ParseScreenCreateOutput(
    const std::string& output);

[[nodiscard]] Result<ScreenReadResult, Error> ParseScreenReadOutput(
    const std::string& output);

[[nodiscard]] Result<void, Error> ParseScreenDeleteOutput(
    const std::string& output);

}  // namespace erpl_adt
