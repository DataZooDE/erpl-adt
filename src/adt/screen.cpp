#include <erpl_adt/adt/screen.hpp>

#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

// ABAP string literals escape an embedded quote by doubling it.
std::string AbapEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\'') {
            out += "''";
        } else {
            out += c;
        }
    }
    return out;
}

std::string ToLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Split a '|'-delimited line (already stripped of its "KEY=" prefix) into
// fields, preserving empty trailing fields (std::getline drops them).
std::vector<std::string> SplitPipe(const std::string& line) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : line) {
        if (c == '|') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

std::optional<int> ParseIntField(const std::string& s) {
    if (s.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// Parses the "RC=<n>" / "MSG=<msgclass>/<msgtype>/<msgno>:<v1>|<v2>|<v3>|<v4>"
// lines shared by all three generated helper classes.
struct ParsedOutput {
    std::optional<int> rc;
    std::string msg_raw;      // full text after "MSG="
    std::vector<std::string> other_lines;
};

ParsedOutput ParseCommon(const std::string& output) {
    ParsedOutput parsed;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("RC=", 0) == 0) {
            parsed.rc = ParseIntField(line.substr(3));
        } else if (line.rfind("MSG=", 0) == 0) {
            parsed.msg_raw = line.substr(4);
        } else {
            parsed.other_lines.push_back(line);
        }
    }
    return parsed;
}

// MSG format: "<class>/<type>/<number>:<v1>|<v2>|<v3>|<v4>"
std::string HumanizeMessage(const std::string& msg_raw) {
    if (msg_raw.empty()) {
        return "";
    }
    auto colon = msg_raw.find(':');
    std::string values = colon == std::string::npos ? "" : msg_raw.substr(colon + 1);
    auto parts = SplitPipe(values);
    std::string joined;
    for (const auto& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += " ";
        }
        joined += part;
    }
    if (joined.empty()) {
        return msg_raw;
    }
    return joined;
}

// This system's ABAP release rejects the documented `||` escape for a
// literal pipe inside a string template ("Unmasked symbol '|' in string
// template", confirmed live), so every line that needs a literal '|'
// separator is built by concatenating short templates (each ending right
// before the separator) with a quoted '|' literal via `&&`, never by
// embedding an unescaped pipe inside one template.
//
// sy-subrc/sy-msg* are snapshotted into local variables (rc_/msg_*)
// immediately after the CALL FUNCTION that sets them — confirmed live that
// evaluating a later string template can otherwise leave sy-subrc read back
// as 0 by the time of the diagnostic print, silently swallowing the error.
const char* const kSnapshotStatement =
    "    DATA(rc_) = sy-subrc.\n"
    "    DATA(msg_id) = sy-msgid.\n"
    "    DATA(msg_ty) = sy-msgty.\n"
    "    DATA(msg_no) = sy-msgno.\n"
    "    DATA(msg_v1) = sy-msgv1.\n"
    "    DATA(msg_v2) = sy-msgv2.\n"
    "    DATA(msg_v3) = sy-msgv3.\n"
    "    DATA(msg_v4) = sy-msgv4.\n";

const char* const kRcWriteStatement = "    out->write( |RC={ rc_ }| ).\n";

const char* const kMsgWriteStatement =
    "    IF rc_ <> 0.\n"
    "      out->write( |MSG={ msg_id }/{ msg_ty }/{ msg_no }:"
    "{ msg_v1 }| && '|' && |{ msg_v2 }| && '|' && |{ msg_v3 }| && "
    "'|' && |{ msg_v4 }| ).\n"
    "    ENDIF.\n";

Error MakeActivationError(const std::string& operation, const std::string& output,
                          const std::string& detail) {
    Error err;
    err.operation = operation;
    err.endpoint = "classrun";
    err.message = detail.empty() ? ("Screen operation failed: " + output) : detail;
    err.sap_error = output;
    err.category = ErrorCategory::ActivationError;
    return err;
}

}  // namespace

// ---------------------------------------------------------------------------
// Source generation
// ---------------------------------------------------------------------------

std::string GenerateScreenCreateSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen,
    const std::string& description,
    const std::string& next_screen,
    int lines,
    int columns,
    const std::vector<ScreenFieldSpec>& fields) {
    const std::string lower_class = ToLower(class_name);

    std::ostringstream src;
    src << "CLASS " << lower_class << " DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        << "  PUBLIC SECTION.\n"
        << "    INTERFACES IF_OO_ADT_CLASSRUN.\n"
        << "ENDCLASS.\n\n"
        << "CLASS " << lower_class << " IMPLEMENTATION.\n"
        << "  METHOD if_oo_adt_classrun~main.\n"
        << "    TRY.\n"
        << "    DATA header      TYPE rpy_dyhead.\n"
        << "    DATA containers  TYPE dycatt_tab.\n"
        << "    DATA wa_cont     LIKE LINE OF containers.\n"
        << "    DATA fields      TYPE dyfatc_tab.\n"
        << "    DATA wa_field    LIKE LINE OF fields.\n"
        << "    DATA flow_logic  TYPE STANDARD TABLE OF rpy_dyflow.\n"
        << "    DATA wa_flow     LIKE LINE OF flow_logic.\n\n"
        << "    header-program    = '" << AbapEscape(program) << "'.\n"
        << "    header-screen     = '" << AbapEscape(screen) << "'.\n"
        << "    header-descript   = '" << AbapEscape(description) << "'.\n"
        << "    header-nextscreen = '" << AbapEscape(next_screen) << "'.\n"
        << "    header-lines      = " << lines << ".\n"
        << "    header-columns    = " << columns << ".\n\n"
        << "    \" RPY_DYNPRO_INSERT only visits FIELDS_TO_CONTAINERS for a\n"
        << "    \" container that also appears in CONTAINERS -- the root\n"
        << "    \" container (blank type/name) is mandatory or every field is\n"
        << "    \" silently dropped.\n"
        << "    CLEAR wa_cont.\n"
        << "    APPEND wa_cont TO containers.\n\n"
        << "    \" A Dynpro is invalid without minimal flow logic -- RPY_DYNPRO_INSERT\n"
        << "    \" fails generation (NOT_GENERATED) with 'A PROCESS statement is\n"
        << "    \" missing' when FLOW_LOGIC is empty (confirmed live).\n"
        << "    wa_flow-line = 'PROCESS BEFORE OUTPUT.'.\n"
        << "    APPEND wa_flow TO flow_logic.\n"
        << "    wa_flow-line = 'PROCESS AFTER INPUT.'.\n"
        << "    APPEND wa_flow TO flow_logic.\n\n";

    for (const auto& field : fields) {
        src << "    CLEAR wa_field.\n"
            << "    wa_field-name = '" << AbapEscape(field.name) << "'.\n"
            << "    wa_field-type = 'TEXT'.\n"
            << "    wa_field-text = '" << AbapEscape(field.text) << "'.\n"
            << "    wa_field-line = " << field.line << ".\n"
            << "    wa_field-column = " << field.column << ".\n"
            << "    wa_field-length = " << field.length << ".\n"
            << "    wa_field-input_fld = 'X'.\n"
            << "    wa_field-output_fld = 'X'.\n"
            << "    APPEND wa_field TO fields.\n\n";
    }

    src << "    CALL FUNCTION 'RPY_DYNPRO_INSERT'\n"
        << "      EXPORTING\n"
        << "        header              = header\n"
        << "        suppress_exist_checks = 'X'\n"
        << "      TABLES\n"
        << "        containers            = containers\n"
        << "        fields_to_containers  = fields\n"
        << "        flow_logic            = flow_logic\n"
        << "      EXCEPTIONS\n"
        << "        cancelled               = 1\n"
        << "        already_exists          = 2\n"
        << "        program_not_exists      = 3\n"
        << "        not_executed            = 4\n"
        << "        missing_required_field  = 5\n"
        << "        illegal_field_value     = 6\n"
        << "        field_not_allowed       = 7\n"
        << "        not_generated           = 8\n"
        << "        illegal_field_position  = 9\n"
        << "        OTHERS                  = 10.\n\n"
        << kSnapshotStatement
        << kRcWriteStatement
        << kMsgWriteStatement
        << "    CATCH cx_root INTO DATA(lx_root).\n"
        << "      out->write( |EXCEPTION={ lx_root->get_text( ) }| ).\n"
        << "    ENDTRY.\n"
        << "  ENDMETHOD.\n"
        << "ENDCLASS.\n";

    return src.str();
}

std::string GenerateScreenReadSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen) {
    const std::string lower_class = ToLower(class_name);

    std::ostringstream src;
    src << "CLASS " << lower_class << " DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        << "  PUBLIC SECTION.\n"
        << "    INTERFACES IF_OO_ADT_CLASSRUN.\n"
        << "ENDCLASS.\n\n"
        << "CLASS " << lower_class << " IMPLEMENTATION.\n"
        << "  METHOD if_oo_adt_classrun~main.\n"
        << "    TRY.\n"
        << "    DATA header      TYPE rpy_dyhead.\n"
        << "    DATA containers  TYPE dycatt_tab.\n"
        << "    DATA fields      TYPE dyfatc_tab.\n"
        << "    DATA flow_logic  TYPE STANDARD TABLE OF rpy_dyflow.\n"
        << "    DATA wa_field    LIKE LINE OF fields.\n\n"
        << "    CALL FUNCTION 'RPY_DYNPRO_READ'\n"
        << "      EXPORTING\n"
        << "        progname           = '" << AbapEscape(program) << "'\n"
        << "        dynnr              = '" << AbapEscape(screen) << "'\n"
        << "      IMPORTING\n"
        << "        header             = header\n"
        << "      TABLES\n"
        << "        containers            = containers\n"
        << "        fields_to_containers  = fields\n"
        << "        flow_logic            = flow_logic\n"
        << "      EXCEPTIONS\n"
        << "        cancelled          = 1\n"
        << "        not_found          = 2\n"
        << "        permission_error   = 3\n"
        << "        OTHERS             = 4.\n\n"
        << kSnapshotStatement
        << kRcWriteStatement
        << "    IF rc_ <> 0.\n"
        << "      out->write( |MSG={ msg_id }/{ msg_ty }/{ msg_no }:"
        << "{ msg_v1 }| && '|' && |{ msg_v2 }| && '|' && |{ msg_v3 }| && "
        << "'|' && |{ msg_v4 }| ).\n"
        << "      RETURN.\n"
        << "    ENDIF.\n\n"
        << "    DATA(hdr) = |HEADER={ header-program }| && '|'\n"
        << "      && |{ header-screen }| && '|'\n"
        << "      && |{ header-lines WIDTH = 3 PAD = '0' ALIGN = RIGHT }| && '|'\n"
        << "      && |{ header-columns WIDTH = 3 PAD = '0' ALIGN = RIGHT }|.\n"
        << "    out->write( hdr ).\n\n"
        << "    LOOP AT fields INTO wa_field.\n"
        << "      DATA(fld) = |FIELD={ wa_field-name }| && '|'\n"
        << "        && |{ wa_field-line WIDTH = 3 PAD = '0' ALIGN = RIGHT }| && '|'\n"
        << "        && |{ wa_field-column WIDTH = 3 PAD = '0' ALIGN = RIGHT }| && '|'\n"
        << "        && |{ wa_field-length WIDTH = 3 PAD = '0' ALIGN = RIGHT }| && '|'\n"
        << "        && |{ wa_field-input_fld }| && '|' && |{ wa_field-output_fld }|.\n"
        << "      out->write( fld ).\n"
        << "    ENDLOOP.\n"
        << "    CATCH cx_root INTO DATA(lx_root).\n"
        << "      out->write( |EXCEPTION={ lx_root->get_text( ) }| ).\n"
        << "    ENDTRY.\n"
        << "  ENDMETHOD.\n"
        << "ENDCLASS.\n";

    return src.str();
}

std::string GenerateScreenDeleteSource(
    const std::string& class_name,
    const std::string& program,
    const std::string& screen) {
    const std::string lower_class = ToLower(class_name);

    std::ostringstream src;
    src << "CLASS " << lower_class << " DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        << "  PUBLIC SECTION.\n"
        << "    INTERFACES IF_OO_ADT_CLASSRUN.\n"
        << "ENDCLASS.\n\n"
        << "CLASS " << lower_class << " IMPLEMENTATION.\n"
        << "  METHOD if_oo_adt_classrun~main.\n"
        << "    TRY.\n"
        << "    DATA corrnum TYPE trkorr.\n\n"
        << "    CALL FUNCTION 'RS_SCRP_DELETE'\n"
        << "      EXPORTING\n"
        << "        dynnr                  = '" << AbapEscape(screen) << "'\n"
        << "        progname               = '" << AbapEscape(program) << "'\n"
        << "        with_popup             = space\n"
        << "      CHANGING\n"
        << "        corrnum                = corrnum\n"
        << "      EXCEPTIONS\n"
        << "        enqueued_by_user       = 1\n"
        << "        enqueue_system_failure = 2\n"
        << "        not_executed           = 3\n"
        << "        not_exists             = 4\n"
        << "        no_modify_permission   = 5\n"
        << "        popup_canceled         = 6\n"
        << "        OTHERS                 = 7.\n\n"
        << kSnapshotStatement
        << kRcWriteStatement
        << kMsgWriteStatement
        << "    CATCH cx_root INTO DATA(lx_root).\n"
        << "      out->write( |EXCEPTION={ lx_root->get_text( ) }| ).\n"
        << "    ENDTRY.\n"
        << "  ENDMETHOD.\n"
        << "ENDCLASS.\n";

    return src.str();
}

// ---------------------------------------------------------------------------
// Output parsing
// ---------------------------------------------------------------------------

Result<void, Error> ParseScreenCreateOutput(const std::string& output) {
    auto parsed = ParseCommon(output);
    if (!parsed.rc.has_value()) {
        return Result<void, Error>::Err(MakeActivationError(
            "ScreenCreate", output,
            "Screen creation produced no recognizable result from the backend helper"));
    }
    if (*parsed.rc != 0) {
        return Result<void, Error>::Err(
            MakeActivationError("ScreenCreate", output, HumanizeMessage(parsed.msg_raw)));
    }
    return Result<void, Error>::Ok();
}

Result<ScreenReadResult, Error> ParseScreenReadOutput(const std::string& output) {
    auto parsed = ParseCommon(output);
    if (!parsed.rc.has_value()) {
        return Result<ScreenReadResult, Error>::Err(MakeActivationError(
            "ScreenRead", output,
            "Screen read produced no recognizable result from the backend helper"));
    }
    if (*parsed.rc == 2) {
        Error err;
        err.operation = "ScreenRead";
        err.endpoint = "classrun";
        err.message = "Screen does not exist";
        err.sap_error = output;
        err.category = ErrorCategory::NotFound;
        return Result<ScreenReadResult, Error>::Err(err);
    }
    if (*parsed.rc != 0) {
        return Result<ScreenReadResult, Error>::Err(
            MakeActivationError("ScreenRead", output, HumanizeMessage(parsed.msg_raw)));
    }

    ScreenReadResult result;
    for (const auto& line : parsed.other_lines) {
        if (line.rfind("HEADER=", 0) == 0) {
            auto parts = SplitPipe(line.substr(7));
            if (parts.size() >= 4) {
                result.program = parts[0];
                result.screen = parts[1];
                result.lines = ParseIntField(parts[2]).value_or(0);
                result.columns = ParseIntField(parts[3]).value_or(0);
            }
        } else if (line.rfind("FIELD=", 0) == 0) {
            auto parts = SplitPipe(line.substr(6));
            if (parts.empty() || parts[0].empty()) {
                continue;  // synthetic/blank field name (e.g. OK-code slot)
            }
            ScreenFieldInfo field;
            field.name = parts[0];
            field.line = parts.size() > 1 ? ParseIntField(parts[1]).value_or(0) : 0;
            field.column = parts.size() > 2 ? ParseIntField(parts[2]).value_or(0) : 0;
            field.length = parts.size() > 3 ? ParseIntField(parts[3]).value_or(0) : 0;
            field.input = parts.size() > 4 && parts[4] == "X";
            field.output = parts.size() > 5 && parts[5] == "X";
            result.fields.push_back(std::move(field));
        }
    }

    return Result<ScreenReadResult, Error>::Ok(std::move(result));
}

Result<void, Error> ParseScreenDeleteOutput(const std::string& output) {
    auto parsed = ParseCommon(output);
    if (!parsed.rc.has_value()) {
        return Result<void, Error>::Err(MakeActivationError(
            "ScreenDelete", output,
            "Screen deletion produced no recognizable result from the backend helper"));
    }
    if (*parsed.rc != 0) {
        return Result<void, Error>::Err(
            MakeActivationError("ScreenDelete", output, HumanizeMessage(parsed.msg_raw)));
    }
    return Result<void, Error>::Ok();
}

}  // namespace erpl_adt
