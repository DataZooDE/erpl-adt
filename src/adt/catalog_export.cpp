#include <erpl_adt/adt/catalog_export.hpp>

#include <nlohmann/json.hpp>

#include <map>
#include <sstream>

namespace erpl_adt {

namespace {

nlohmann::json EntityToJson(const CatalogEntity& e) {
    nlohmann::json ej;
    ej["id"] = e.id.Value();
    ej["system_sid"] = e.system_sid;
    ej["domain"] = ToString(e.domain);
    ej["object_type"] = e.object_type;
    ej["technical_name"] = e.technical_name;
    ej["display_name"] = e.display_name;
    if (e.package_or_infoarea.has_value()) ej["package_or_infoarea"] = *e.package_or_infoarea;
    if (e.created_by.has_value()) ej["created_by"] = *e.created_by;
    if (e.changed_by.has_value()) ej["changed_by"] = *e.changed_by;
    if (e.changed_at.has_value()) ej["changed_at"] = *e.changed_at;
    if (e.biz_definition.has_value()) ej["biz_definition"] = *e.biz_definition;
    if (e.biz_owner.has_value()) ej["biz_owner"] = *e.biz_owner;
    if (e.biz_lob.has_value()) ej["biz_lob"] = *e.biz_lob;
    if (e.biz_confidentiality.has_value()) ej["biz_confidentiality"] = *e.biz_confidentiality;
    ej["extracted_at"] = e.extracted_at;
    return ej;
}

nlohmann::json FieldToJson(const CatalogField& f) {
    nlohmann::json fj;
    fj["id"] = f.id;
    fj["entity_id"] = f.entity_id.Value();
    fj["name"] = f.name;
    if (f.role.has_value()) fj["role"] = *f.role;
    if (f.data_type.has_value()) fj["data_type"] = *f.data_type;
    if (f.length.has_value()) fj["length"] = *f.length;
    if (f.decimals.has_value()) fj["decimals"] = *f.decimals;
    if (f.aggregation.has_value()) fj["aggregation"] = *f.aggregation;
    if (f.unit.has_value()) fj["unit"] = *f.unit;
    if (f.formula.has_value()) fj["formula"] = *f.formula;
    return fj;
}

nlohmann::json EdgeToJson(const CatalogEdge& e) {
    nlohmann::json edj;
    edj["id"] = e.id;
    edj["from_id"] = e.from_id.Value();
    edj["to_id"] = e.to_id.Value();
    edj["kind"] = e.kind;
    edj["resolution"] = e.resolution;
    if (!e.field_mapping_json.empty()) {
        edj["field_mapping"] = nlohmann::json::parse(e.field_mapping_json, nullptr, false);
    }
    edj["extracted_at"] = e.extracted_at;
    return edj;
}

} // anonymous namespace

std::string RenderCatalogFeedJson(const CatalogFeed& feed) {
    nlohmann::json root;
    root["schema_version"] = feed.schema_version;
    root["contract"] = feed.contract;
    root["system_sid"] = feed.system_sid;
    root["built_at"] = feed.built_at;

    root["entities"] = nlohmann::json::array();
    for (const auto& e : feed.entities) root["entities"].push_back(EntityToJson(e));

    root["fields"] = nlohmann::json::array();
    for (const auto& f : feed.fields) root["fields"].push_back(FieldToJson(f));

    root["edges"] = nlohmann::json::array();
    for (const auto& e : feed.edges) root["edges"].push_back(EdgeToJson(e));

    root["warnings"] = feed.warnings;

    return root.dump();
}

std::string RenderCatalogFeedOpenMetadataJson(const CatalogFeed& feed,
                                               const std::string& service_name,
                                               const std::string& system_id) {
    std::map<std::string, std::vector<const CatalogField*>> fields_by_entity;
    for (const auto& f : feed.fields) {
        fields_by_entity[f.entity_id.Value()].push_back(&f);
    }

    nlohmann::json root;
    root["serviceType"] = "ErplAdtCatalog";
    root["serviceName"] = service_name;
    root["systemId"] = system_id;
    root["builtAt"] = feed.built_at;

    nlohmann::json tables = nlohmann::json::array();
    for (const auto& e : feed.entities) {
        if (e.domain != CatalogDomain::Ddic && e.domain != CatalogDomain::Cds) continue;

        auto fqn = [&](const std::string& name) {
            std::string s = service_name;
            if (!system_id.empty()) s += "." + system_id;
            if (e.package_or_infoarea.has_value() && !e.package_or_infoarea->empty()) {
                s += "." + *e.package_or_infoarea;
            }
            s += "." + name;
            return s;
        };

        nlohmann::json t;
        t["name"] = e.technical_name;
        t["fullyQualifiedName"] = fqn(e.technical_name);
        t["tableType"] = e.domain == CatalogDomain::Cds ? "View" : "Regular";
        if (!e.display_name.empty()) t["description"] = e.display_name;

        nlohmann::json cols = nlohmann::json::array();
        auto it = fields_by_entity.find(e.id.Value());
        if (it != fields_by_entity.end()) {
            for (const auto* f : it->second) {
                nlohmann::json c;
                c["name"] = f->name;
                c["dataType"] = f->data_type.value_or("VARCHAR");
                cols.push_back(std::move(c));
            }
        }
        t["columns"] = std::move(cols);
        tables.push_back(std::move(t));
    }
    root["tables"] = std::move(tables);

    return root.dump();
}

std::string RenderCatalogFeedMermaid(const CatalogFeed& feed) {
    std::ostringstream out;
    out << "graph LR\n";

    std::map<std::string, std::string> node_alias;
    int counter = 0;
    auto alias_for = [&](const CatalogEntity& e) -> std::string {
        auto it = node_alias.find(e.id.Value());
        if (it != node_alias.end()) return it->second;
        std::string alias = "n" + std::to_string(counter++);
        node_alias[e.id.Value()] = alias;
        out << "  " << alias << "[\"" << e.technical_name << "\"]\n";
        return alias;
    };

    for (const auto& e : feed.entities) alias_for(e);

    std::map<std::string, const CatalogEntity*> by_id;
    for (const auto& e : feed.entities) by_id[e.id.Value()] = &e;

    for (const auto& edge : feed.edges) {
        auto from_it = by_id.find(edge.from_id.Value());
        auto to_it = by_id.find(edge.to_id.Value());
        if (from_it == by_id.end() || to_it == by_id.end()) continue;
        out << "  " << node_alias[edge.from_id.Value()] << " -->|" << edge.kind << "| "
            << node_alias[edge.to_id.Value()] << "\n";
    }

    return out.str();
}

} // namespace erpl_adt
