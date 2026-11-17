#include <erpl_adt/workflow/abapgit_fugr.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>

namespace erpl_adt {

namespace {

Error FugrError(const std::string& message) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    err.category = ErrorCategory::Internal;
    return err;
}

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string Child(const tinyxml2::XMLElement* el, const char* name) {
    const auto* c = el->FirstChildElement(name);
    return c != nullptr && c->GetText() != nullptr ? c->GetText() : "";
}

std::string Print(const tinyxml2::XMLElement* el) {
    tinyxml2::XMLPrinter printer(nullptr, true);
    el->Accept(&printer);
    return printer.CStr();
}

std::vector<FmParameter> ReadParameters(const tinyxml2::XMLElement* item, const char* table,
                                        const char* line, const char* like_field) {
    std::vector<FmParameter> params;
    const auto* tab = item->FirstChildElement(table);
    if (tab == nullptr) {
        return params;
    }
    for (const auto* p = tab->FirstChildElement(line); p != nullptr;
         p = p->NextSiblingElement(line)) {
        FmParameter param;
        param.name = Child(p, "PARAMETER");
        param.dbfield = Child(p, like_field);
        param.typ = Child(p, "TYP");
        param.types = Child(p, "TYPES");
        param.reference = Child(p, "REFERENCE") == "X";
        param.optional = Child(p, "OPTIONAL") == "X";
        param.ref_class = Child(p, "REF_CLASS") == "X";
        param.default_value = Child(p, "DEFAULT");
        params.push_back(std::move(param));
    }
    return params;
}

// One parameter line as LCL_SIGNATURE_SOURCE_BUILDER->ADD_PARAMETERS renders
// it, after CONVERT_PARAMETER_OLD_TO_FUPA mapped the old table row.
std::string ParameterLine(const FmParameter& p, bool is_tables, bool allow_default) {
    std::string line = "    ";
    const std::string name = Lower(p.name);
    line += (p.reference || is_tables) ? name : "VALUE(" + name + ")";

    // FUPARAREF-STRUCTURE / -TYPE / -REF_CLASS
    std::string structure;
    bool type_flag = false;
    bool ref_class = p.ref_class;
    if (!p.dbfield.empty()) {
        structure = p.dbfield;
    } else if (!p.typ.empty()) {
        structure = p.typ;
        type_flag = p.types != "O";
    }
    // PA_STRIP_FUPARAREF_TYPE: old SE37 kept "REF TO x" in the type field.
    const std::string upper = Upper(structure);
    if (upper.rfind("REF TO ", 0) == 0) {
        structure = structure.substr(7);
        ref_class = true;
    }

    if (!structure.empty()) {
        if (ref_class) {
            line += " TYPE REF TO ";
        } else if (type_flag) {
            line += " TYPE ";
        } else {
            line += " LIKE ";
        }
        line += Lower(structure);
    } else {
        line += is_tables ? " TYPE STANDARD TABLE" : " TYPE ANY";
    }
    if (allow_default && !p.default_value.empty()) {
        line += " DEFAULT " + p.default_value;
    } else if (p.optional) {
        line += " OPTIONAL";
    }
    if (structure.empty()) {
        line += " ##ADT_PARAMETER_UNTYPED";
    }
    return line;
}

}  // namespace

Result<FugrDefinition, Error> ParseFugrValues(std::string_view values_xml) {
    const std::string wrapped = "<V>" + std::string(values_xml) + "</V>";
    tinyxml2::XMLDocument doc;
    if (doc.Parse(wrapped.c_str(), wrapped.size()) != tinyxml2::XML_SUCCESS) {
        return Result<FugrDefinition, Error>::Err(FugrError("Malformed function group XML"));
    }
    const auto* root = doc.RootElement();
    FugrDefinition def;
    def.areat = Child(root, "AREAT");
    if (const auto* incl = root->FirstChildElement("INCLUDES")) {
        for (const auto* i = incl->FirstChildElement(); i != nullptr; i = i->NextSiblingElement()) {
            if (i->GetText() != nullptr) {
                def.includes.push_back(Upper(i->GetText()));
            }
        }
    }
    if (const auto* funcs = root->FirstChildElement("FUNCTIONS")) {
        for (const auto* item = funcs->FirstChildElement("item"); item != nullptr;
             item = item->NextSiblingElement("item")) {
            FmInterface fm;
            fm.name = Upper(Child(item, "FUNCNAME"));
            if (fm.name.empty()) {
                return Result<FugrDefinition, Error>::Err(
                    FugrError("Function group XML: FUNCTIONS item without FUNCNAME"));
            }
            fm.short_text = Child(item, "SHORT_TEXT");
            fm.remote_call = Child(item, "REMOTE_CALL");
            fm.update_task = Child(item, "UPDATE_TASK");
            fm.class_based_exceptions = Child(item, "EXCEPTION_CLASSES") == "X";
            fm.importing = ReadParameters(item, "IMPORT", "RSIMP", "DBFIELD");
            fm.exporting = ReadParameters(item, "EXPORT", "RSEXP", "DBFIELD");
            fm.changing = ReadParameters(item, "CHANGING", "RSCHA", "DBFIELD");
            fm.tables = ReadParameters(item, "TABLES", "RSTBL", "DBSTRUCT");
            if (const auto* exc = item->FirstChildElement("EXCEPTION")) {
                for (const auto* e = exc->FirstChildElement("RSEXC"); e != nullptr;
                     e = e->NextSiblingElement("RSEXC")) {
                    FmException ex;
                    ex.name = Child(e, "EXCEPTION");
                    ex.resumable = Child(e, "IS_RESUMABLE") == "X";
                    fm.exceptions.push_back(std::move(ex));
                }
            }
            fm.item_xml = Print(item);
            def.functions.push_back(std::move(fm));
        }
    }
    return Result<FugrDefinition, Error>::Ok(std::move(def));
}

std::vector<std::string> BuildFmSignatureLines(const FmInterface& fm) {
    std::vector<std::string> lines;
    lines.push_back("FUNCTION " + Lower(fm.name));
    auto group = [&lines](const char* keyword, const std::vector<FmParameter>& params,
                          bool is_tables, bool allow_default) {
        if (params.empty()) {
            return;
        }
        lines.push_back(std::string("  ") + keyword);
        for (const auto& p : params) {
            lines.push_back(ParameterLine(p, is_tables, allow_default));
        }
    };
    // Order of LCL_SIGNATURE_SOURCE_BUILDER->GET_SIGNATURE_SOURCE.
    group("IMPORTING", fm.importing, false, true);
    group("EXPORTING", fm.exporting, false, false);
    group("CHANGING", fm.changing, false, true);
    group("TABLES", fm.tables, true, false);
    if (!fm.exceptions.empty()) {
        lines.push_back(fm.class_based_exceptions ? "  RAISING" : "  EXCEPTIONS");
        for (const auto& e : fm.exceptions) {
            const std::string name = Lower(e.name);
            lines.push_back("    " + (e.resumable ? "RESUMABLE(" + name + ")" : name));
        }
    }
    lines.back() += ".";
    return lines;
}

Result<std::string, Error> BuildFmSignatureSource(const FmInterface& fm,
                                                 std::string_view abapgit_source) {
    // Locate the FUNCTION statement and the generated comment block after it.
    size_t pos = 0;
    bool found = false;
    while (pos < abapgit_source.size()) {
        auto eol = abapgit_source.find('\n', pos);
        const size_t next = eol == std::string_view::npos ? abapgit_source.size() : eol + 1;
        std::string_view line = abapgit_source.substr(pos, next - pos);
        size_t first = line.find_first_not_of(" \t");
        const std::string head =
            first == std::string_view::npos ? "" : Upper(line.substr(first, 9));
        pos = next;
        if (head == "FUNCTION ") {
            found = true;
            break;
        }
    }
    if (!found) {
        return Result<std::string, Error>::Err(
            FugrError("Function module " + fm.name + ": no FUNCTION statement in its source"));
    }
    size_t header_lines = 1;  // the FUNCTION statement
    while (pos < abapgit_source.size() && abapgit_source.compare(pos, 2, "*\"") == 0) {
        auto eol = abapgit_source.find('\n', pos);
        pos = eol == std::string_view::npos ? abapgit_source.size() : eol + 1;
        ++header_lines;
    }

    // Like ADT's own rendering (CL_FB_FUNC_SOURCE_CONVERTER->concatenate_source),
    // the signature is padded with empty lines to the height of the comment
    // block it replaces; on save SAP drops exactly that padding again, so
    // empty lines that belong to the body survive the round trip.
    const auto signature = BuildFmSignatureLines(fm);
    std::string out;
    for (const auto& line : signature) {
        out += line;
        out += '\n';
    }
    for (size_t i = signature.size(); i < header_lines; ++i) {
        out += '\n';
    }
    out += abapgit_source.substr(pos);
    return Result<std::string, Error>::Ok(std::move(out));
}

std::string FugrPartName(std::string_view name) {
    std::string out = Lower(name);
    std::replace(out.begin(), out.end(), '/', '#');
    return out;
}

}  // namespace erpl_adt
