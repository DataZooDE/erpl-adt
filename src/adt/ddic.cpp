#include <erpl_adt/adt/ddic.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <tinyxml2.h>

#include <queue>
#include <set>
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

        if (!field.name.empty()) {
            info.fields.push_back(std::move(field));
        }
    }

    return Result<TableInfo, Error>::Ok(std::move(info));
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
    const std::string& table_name) {
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

    return ParseTableDefinition(http.body, table_name);
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
