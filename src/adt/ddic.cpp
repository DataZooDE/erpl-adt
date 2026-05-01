#include <erpl_adt/adt/ddic.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <tinyxml2.h>

#include <map>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

std::string GetText(const tinyxml2::XMLElement* el, const char* name) {
    auto* child = el->FirstChildElement(name);
    if (child && child->GetText()) return child->GetText();
    return "";
}

Result<std::vector<PackageEntry>, Error> ParseNodeStructure(
    std::string_view xml) {
    if (xml.empty()) {
        return Result<std::vector<PackageEntry>, Error>::Ok({});
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "ListPackageContents", "",
            "Failed to parse node structure XML")) {
        return Result<std::vector<PackageEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<PackageEntry> entries;

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    }

    // Navigate: asx:abap > asx:values > DATA > TREE_CONTENT > SEU_ADT_REPOSITORY_OBJ_NODE
    auto* values = root->FirstChildElement();
    if (!values) {
        return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    }
    auto* data = values->FirstChildElement("DATA");
    if (!data) {
        return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    }
    auto* tree = data->FirstChildElement("TREE_CONTENT");
    if (!tree) {
        return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
    }

    for (auto* node = tree->FirstChildElement("SEU_ADT_REPOSITORY_OBJ_NODE"); node;
         node = node->NextSiblingElement("SEU_ADT_REPOSITORY_OBJ_NODE")) {
        PackageEntry entry;
        entry.object_type = GetText(node, "OBJECT_TYPE");
        entry.object_name = GetText(node, "OBJECT_NAME");
        entry.object_uri = GetText(node, "OBJECT_URI");
        entry.description = GetText(node, "DESCRIPTION");
        entry.expandable = GetText(node, "EXPANDABLE") == "X";

        if (!entry.object_name.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    return Result<std::vector<PackageEntry>, Error>::Ok(std::move(entries));
}

// Returns true when the ADT response is a blue:blueSource element (TABL/DT DDL form).
// On modern ABAP systems, transparent tables with DDL source return this format instead
// of <tabl:table> with <tabl:field> children.
bool IsBlueSource(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return false;
    }
    const auto* root = doc.RootElement();
    if (!root || !root->Name()) return false;
    return std::string_view(root->Name()).find("blueSource") != std::string_view::npos;
}

// Parse table fields from ABAP SQL DDL source text.
// Handles lines of the form:
//   key fieldname : TypeName [not null] ...
//       fieldname : TypeName [not null] ...
// Skips annotations (@...), define/}, and foreign key continuation lines.
std::vector<TableField> ParseFieldsFromDdl(const std::string& ddl) {
    std::vector<TableField> fields;
    // Matches: optional "key " prefix, field name, colon, type name.
    // Type handles both data elements (s_mandt) and built-in dotted types
    // (abap.sstring(255), abap.curr(15,2), abap.int4).
    static const std::regex kFieldRe(
        R"(^\s*(key\s+)?([a-zA-Z_]\w*)\s*:\s*([a-zA-Z_]\w*(?:\.[a-zA-Z_]\w*)*(?:\([^)]*\))?))",
        std::regex::icase);

    std::istringstream stream(ddl);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip annotation lines and lines that clearly aren't field declarations
        if (line.empty()) continue;
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (line[first] == '@') continue;  // annotation

        std::smatch m;
        if (!std::regex_search(line, m, kFieldRe)) continue;

        const std::string name = m[2].str();
        // Skip DDL keywords that pattern-match but aren't fields
        if (name == "define" || name == "with" || name == "where" ||
            name == "and" || name == "key") {
            continue;
        }

        TableField field;
        field.name = name;
        field.type = m[3].str();
        field.key_field = m[1].matched;  // "key " prefix present
        fields.push_back(std::move(field));
    }
    return fields;
}

Result<TableInfo, Error> ParseTableDefinition(
    std::string_view xml, const std::string& table_name) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "GetTableDefinition", table_name,
            "Failed to parse table XML")) {
        return Result<TableInfo, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<TableInfo, Error>::Err(Error{
            "GetTableDefinition", table_name, std::nullopt,
            "Empty table response", std::nullopt});
    }

    TableInfo info;
    info.name = xml_utils::AttrAny(root, "adtcore:name", "name");
    info.description = xml_utils::AttrAny(root, "adtcore:description", "description");
    info.delivery_class = xml_utils::AttrAny(root, "tabl:deliveryClass", "deliveryClass");

    // Parse fields from child elements.
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        std::string el_name = el->Name() ? el->Name() : "";
        if (el_name.find("field") == std::string::npos &&
            el_name.find("Field") == std::string::npos &&
            el_name.find("column") == std::string::npos) {
            continue;
        }

        TableField field;
        field.name = xml_utils::AttrAny(el, "adtcore:name", "name");
        field.type = xml_utils::AttrAny(el, "tabl:type", "type");
        field.description = xml_utils::AttrAny(el, "adtcore:description", "description");
        field.key_field = (xml_utils::AttrAny(el, "tabl:keyField", "keyField") == "true");

        auto len_str = xml_utils::AttrAny(el, "tabl:length", "length");
        if (!len_str.empty()) {
            try {
                std::size_t pos = 0;
                int value = std::stoi(len_str, &pos);
                if (pos == len_str.size()) {
                    field.length = value;
                }
            } catch (const std::exception&) {}
        }
        auto dec_str = xml_utils::AttrAny(el, "tabl:decimals", "decimals");
        if (!dec_str.empty()) {
            try {
                std::size_t pos = 0;
                int value = std::stoi(dec_str, &pos);
                if (pos == dec_str.size()) {
                    field.decimals = value;
                }
            } catch (const std::exception&) {}
        }

        if (!field.name.empty()) {
            info.fields.push_back(std::move(field));
        }
    }

    return Result<TableInfo, Error>::Ok(std::move(info));
}

// Extract length/decimals from abap.type_name(length,decimals) syntax.
// Returns a pair of optionals; empty if the type is not an abap.* built-in
// with parenthesised parameters.
std::pair<std::optional<int>, std::optional<int>> ParseAbapBuiltinParams(
        const std::string& type) {
    static const std::regex kRe(R"(^abap\.\w+\((\d+)(?:,(\d+))?\)$)",
                                std::regex::icase);
    std::smatch m;
    if (!std::regex_match(type, m, kRe)) {
        return {std::nullopt, std::nullopt};
    }
    std::optional<int> length;
    std::optional<int> decimals;
    try { length = std::stoi(m[1].str()); } catch (...) {}
    if (m[2].matched) {
        try { decimals = std::stoi(m[2].str()); } catch (...) {}
    }
    return {length, decimals};
}

// Parse a data element XML response and return length, decimals, description.
struct DataElementInfo {
    std::optional<int> length;
    std::optional<int> decimals;
    std::string description;
};

DataElementInfo ParseDataElementXml(const std::string& xml) {
    DataElementInfo info;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return info;
    auto* root = doc.RootElement();
    if (!root) return info;

    info.description = xml_utils::AttrAny(root, "adtcore:description", "description");

    auto* dtel = root->FirstChildElement("dtel:dataElement");
    if (!dtel) return info;

    auto len_str = GetText(dtel, "dtel:dataTypeLength");
    if (!len_str.empty()) {
        try {
            int v = std::stoi(len_str);
            if (v > 0) info.length = v;
        } catch (...) {}
    }
    auto dec_str = GetText(dtel, "dtel:dataTypeDecimals");
    if (!dec_str.empty()) {
        try {
            int v = std::stoi(dec_str);
            if (v > 0) info.decimals = v;
        } catch (...) {}
    }
    return info;
}

// Enrich fields parsed from DDL source with length, decimals, and description:
//   - abap.* built-in types: extract params from the type string (no HTTP call)
//   - data element names: fetch /sap/bc/adt/ddic/dataelements/{name}
// Failures on individual element fetches are silently ignored so that a
// missing or unauthorised data element doesn't abort the entire table read.
void EnrichFieldsFromDataElements(IAdtSession& session,
                                  std::vector<TableField>& fields) {
    // Collect unique non-abap type names that need a data element lookup.
    std::set<std::string> to_fetch;
    for (const auto& f : fields) {
        if (f.type.empty()) continue;
        auto [len, dec] = ParseAbapBuiltinParams(f.type);
        if (!len.has_value() && f.type.substr(0, 5) != "abap.") {
            to_fetch.insert(f.type);
        }
    }

    // Fetch each unique data element once and cache the result.
    std::map<std::string, DataElementInfo> cache;
    for (const auto& type_name : to_fetch) {
        auto url = "/sap/bc/adt/ddic/dataelements/" + type_name;
        auto resp = session.Get(url, {});
        if (resp.IsOk() && resp.Value().status_code == 200) {
            cache[type_name] = ParseDataElementXml(resp.Value().body);
        }
    }

    // Enrich each field.
    for (auto& f : fields) {
        if (f.type.empty()) continue;
        auto [len, dec] = ParseAbapBuiltinParams(f.type);
        if (len.has_value()) {
            f.length   = len;
            f.decimals = dec;
        } else {
            auto it = cache.find(f.type);
            if (it != cache.end()) {
                f.length      = it->second.length;
                f.decimals    = it->second.decimals;
                f.description = it->second.description;
            }
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ListPackageContents
// ---------------------------------------------------------------------------
Result<std::vector<PackageEntry>, Error> ListPackageContents(
    IAdtSession& session,
    const std::string& package_name) {
    auto url = "/sap/bc/adt/repository/nodestructure"
               "?parent_type=DEVC/K&parent_name=" + package_name +
               "&withShortDescriptions=true";

    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<std::vector<PackageEntry>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<PackageEntry>, Error>::Err(
            Error::FromHttpStatus("ListPackageContents", url, http.status_code, http.body));
    }

    return ParseNodeStructure(http.body);
}

// ---------------------------------------------------------------------------
// ListPackageTree
// ---------------------------------------------------------------------------
Result<std::vector<PackageEntry>, Error> ListPackageTree(
    IAdtSession& session,
    const PackageTreeOptions& options) {

    std::vector<PackageEntry> results;

    // BFS: queue of (package_name, depth) pairs.
    std::queue<std::pair<std::string, int>> queue;
    std::set<std::string> visited;

    queue.push({options.root_package, 0});
    visited.insert(options.root_package);

    while (!queue.empty()) {
        auto [pkg_name, depth] = queue.front();
        queue.pop();

        auto contents = ListPackageContents(session, pkg_name);
        if (contents.IsErr()) {
            return Result<std::vector<PackageEntry>, Error>::Err(
                std::move(contents).Error());
        }

        for (auto& entry : std::move(contents).Value()) {
            // Sub-package: queue for recursive traversal.
            if (entry.object_type == "DEVC/K") {
                if (depth + 1 < options.max_depth &&
                    visited.find(entry.object_name) == visited.end()) {
                    visited.insert(entry.object_name);
                    queue.push({entry.object_name, depth + 1});
                }
                continue;
            }

            // Apply type filter if set.
            if (options.type_filter.has_value()) {
                // Match if object_type starts with the filter
                // e.g. filter "CLAS" matches "CLAS/OC"
                if (entry.object_type.substr(0, options.type_filter->size()) !=
                    *options.type_filter) {
                    continue;
                }
            }

            entry.package_name = pkg_name;
            results.push_back(std::move(entry));
        }
    }

    return Result<std::vector<PackageEntry>, Error>::Ok(std::move(results));
}

// ---------------------------------------------------------------------------
// GetTableDefinition
// ---------------------------------------------------------------------------
Result<TableInfo, Error> GetTableDefinition(
    IAdtSession& session,
    const std::string& table_name,
    bool resolve_types) {
    auto url = "/sap/bc/adt/ddic/tables/" + table_name;

    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.tables.v2+xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<TableInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<TableInfo, Error>::Err(Error{
            "GetTableDefinition", table_name, 404,
            "Table not found", std::nullopt,
            ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        return Result<TableInfo, Error>::Err(
            Error::FromHttpStatus("GetTableDefinition", table_name, http.status_code, http.body));
    }

    auto result = ParseTableDefinition(http.body, table_name);
    if (result.IsErr()) return result;

    // On modern ABAP systems, tables stored as DDL source (TABL/DT) return a
    // <blue:blueSource> element with no field children. Fall back to fetching
    // the DDL source text and parsing fields from it.
    if (result.Value().fields.empty() && IsBlueSource(http.body)) {
        HttpHeaders src_headers;
        src_headers["Accept"] = "text/plain";
        auto src_response = session.Get(url + "/source/main", src_headers);
        if (src_response.IsOk() && src_response.Value().status_code == 200) {
            auto info = std::move(result).Value();
            info.fields = ParseFieldsFromDdl(src_response.Value().body);
            if (resolve_types) {
                EnrichFieldsFromDataElements(session, info.fields);
            }
            return Result<TableInfo, Error>::Ok(std::move(info));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// GetCdsSource
// ---------------------------------------------------------------------------
Result<std::string, Error> GetCdsSource(
    IAdtSession& session,
    const std::string& cds_name) {
    auto url = "/sap/bc/adt/ddic/ddl/sources/" + cds_name + "/source/main";

    HttpHeaders headers;
    headers["Accept"] = "text/plain";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<std::string, Error>::Err(Error{
            "GetCdsSource", cds_name, 404,
            "CDS view not found", std::nullopt,
            ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        return Result<std::string, Error>::Err(
            Error::FromHttpStatus("GetCdsSource", cds_name, http.status_code, http.body));
    }

    return Result<std::string, Error>::Ok(http.body);
}

} // namespace erpl_adt
