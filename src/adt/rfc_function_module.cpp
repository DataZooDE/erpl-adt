#include <erpl_adt/adt/rfc_function_module.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/source.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace erpl_adt {

namespace {

std::string Trim(std::string_view s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string ToUpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string GetText(const tinyxml2::XMLElement* el, const char* name) {
    auto* child = el->FirstChildElement(name);
    if (child && child->GetText()) return child->GetText();
    return "";
}

// Matches: [VALUE(]NAME[)] (LIKE|TYPE) TYPE_REF — the ABAP parameter
// declaration shape ADT's function-module source reader returns (pass-by-
// value params wrap the name in VALUE(...); pass-by-reference params don't).
const std::regex& ParamRegex() {
    static const std::regex re(
        R"(^(?:VALUE\()?([A-Za-z_][A-Za-z0-9_]*)\)?\s+(?:LIKE|TYPE)\s+([A-Za-z_][A-Za-z0-9_\-]*))",
        std::regex::icase);
    return re;
}

// Matches a `default '...'` or `default value` clause anywhere in the
// (already trimmed, terminator-stripped) declaration line — e.g.
// `value(cache_results) type flag_x default 'X'`
// (test/testdata/abap/rfc_bapi_user_get_detail_source.abap:4). Only the
// quoted-literal form is captured; `default space`/other keyword defaults
// are left unset rather than guessed at.
const std::regex& DefaultValueRegex() {
    static const std::regex re(R"(\bdefault\s+'([^']*)')", std::regex::icase);
    return re;
}

} // namespace

RfcSignature ParseRfcSignature(std::string_view source) {
    RfcSignature sig;
    std::istringstream stream{std::string(source)};
    std::string line;
    bool in_function = false;
    std::string current_kind;

    while (std::getline(stream, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty()) continue;
        std::string lower = ToLowerCopy(trimmed);

        if (!in_function) {
            if (lower.rfind("function ", 0) == 0 || lower == "function") {
                in_function = true;
            }
            continue;
        }

        if (lower == "importing" || lower == "exporting" || lower == "tables" ||
            lower == "changing") {
            current_kind = lower;
            continue;
        }
        // RAISING/EXCEPTIONS aren't DDIC references — stop attributing
        // declarations to a parameter kind, but keep scanning for the
        // terminating "." (which may be on one of these lines).
        if (lower.rfind("raising", 0) == 0 || lower.rfind("exceptions", 0) == 0) {
            current_kind.clear();
        }

        bool terminates = trimmed.back() == '.';
        std::string decl = terminates ? trimmed.substr(0, trimmed.size() - 1) : trimmed;

        if (!current_kind.empty() && !decl.empty()) {
            std::smatch m;
            if (std::regex_search(decl, m, ParamRegex())) {
                RfcParameter p;
                p.name = ToUpperCopy(m[1].str());
                p.kind = current_kind;
                std::string type_ref = m[2].str();
                auto dash = type_ref.find('-');
                std::string type_name = dash == std::string::npos ? type_ref : type_ref.substr(0, dash);
                p.type_name = ToUpperCopy(type_name);
                p.is_optional = ToLowerCopy(decl).find("optional") != std::string::npos;
                std::smatch dm;
                if (std::regex_search(decl, dm, DefaultValueRegex())) {
                    p.default_value = dm[1].str();
                }
                sig.parameters.push_back(std::move(p));
            }
        }

        if (terminates) break;
    }

    return sig;
}

Result<std::vector<PackageEntry>, Error> ListFunctionModules(
    IAdtSession& session, const std::string& function_group) {
    auto url = "/sap/bc/adt/repository/nodestructure"
               "?parent_type=FUGR%2FF&parent_name=" + function_group +
               "&withShortDescriptions=true";

    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<std::vector<PackageEntry>, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<PackageEntry>, Error>::Err(
            Error::FromHttpStatus("ListFunctionModules", url, http.status_code, http.body));
    }
    if (http.body.empty()) {
        return Result<std::vector<PackageEntry>, Error>::Ok({});
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "ListFunctionModules", "", "Failed to parse node structure XML")) {
        return Result<std::vector<PackageEntry>, Error>::Err(std::move(*parse_error));
    }

    std::vector<PackageEntry> entries;
    auto* root = doc.RootElement();
    if (!root) return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    auto* values = root->FirstChildElement();
    if (!values) return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    auto* data = values->FirstChildElement("DATA");
    if (!data) return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    auto* tree = data->FirstChildElement("TREE_CONTENT");
    if (!tree) return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));

    for (auto* node = tree->FirstChildElement("SEU_ADT_REPOSITORY_OBJ_NODE"); node;
         node = node->NextSiblingElement("SEU_ADT_REPOSITORY_OBJ_NODE")) {
        PackageEntry entry;
        entry.object_type = GetText(node, "OBJECT_TYPE");
        entry.object_name = GetText(node, "OBJECT_NAME");
        entry.object_uri = GetText(node, "OBJECT_URI");
        entry.description = GetText(node, "DESCRIPTION");
        entry.expandable = GetText(node, "EXPANDABLE") == "X";
        if (!entry.object_name.empty()) entries.push_back(std::move(entry));
    }

    return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
}

Result<RfcSignature, Error> GetFunctionModuleSignature(
    IAdtSession& session, const std::string& function_module_object_uri) {
    auto source = ReadSource(session, function_module_object_uri + "/source/main");
    if (source.IsErr()) {
        return Result<RfcSignature, Error>::Err(std::move(source).Error());
    }
    return Result<RfcSignature, Error>::Ok(ParseRfcSignature(source.Value()));
}

} // namespace erpl_adt
