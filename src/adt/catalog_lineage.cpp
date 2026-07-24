#include <erpl_adt/adt/catalog_lineage.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>

#include <ctime>
#include <map>
#include <optional>
#include <sstream>

namespace erpl_adt {

namespace {

std::string UtcTimestampNow() {
    std::time_t now = std::time(nullptr);
    struct tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

// BwLineageGraph field-scoped node ids look like "field:TYPE:NAME:FIELD"
// (see bw_lineage_graph.cpp's FieldNodeId) — the owning object is TYPE/NAME.
struct FieldNodeRef {
    std::string object_type;
    std::string object_name;
    std::string field_name;
};

std::optional<FieldNodeRef> ParseFieldNodeId(const std::string& id) {
    static constexpr std::string_view kPrefix = "field:";
    if (id.rfind(kPrefix, 0) != 0) return std::nullopt;
    auto rest = id.substr(kPrefix.size());
    auto p1 = rest.find(':');
    if (p1 == std::string::npos) return std::nullopt;
    auto object_type = rest.substr(0, p1);
    auto rest2 = rest.substr(p1 + 1);
    auto p2 = rest2.find(':');
    if (p2 == std::string::npos) return std::nullopt;
    return FieldNodeRef{object_type, rest2.substr(0, p2), rest2.substr(p2 + 1)};
}

// "obj:TYPE:NAME" — see bw_lineage_graph.cpp's ObjectNodeId.
std::optional<std::pair<std::string, std::string>> ParseObjectNodeId(const std::string& id) {
    static constexpr std::string_view kPrefix = "obj:";
    if (id.rfind(kPrefix, 0) != 0) return std::nullopt;
    auto rest = id.substr(kPrefix.size());
    auto p1 = rest.find(':');
    if (p1 == std::string::npos) return std::nullopt;
    return std::make_pair(rest.substr(0, p1), rest.substr(p1 + 1));
}

std::string EscapeJsonString(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// One BW TRFN rule's field mapping, widened beyond the bare from/to pair —
// rule_type ("StepDirect"/"StepFormula"/"StepConstant"/...), formula, and
// constant come straight from BwTransformationRule (already fully parsed by
// ParseTrfnRule, bw_lineage.cpp) but were previously dropped at this exact
// conversion step. Empty strings mean "not applicable to this rule type",
// same convention BwTransformationRule itself already uses.
struct FieldMappingEntry {
    std::string from_field;
    std::string to_field;
    std::string rule_type;
    std::string formula;
    std::string constant;
};

std::string RenderFieldMappingJson(const std::vector<FieldMappingEntry>& mappings) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < mappings.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& m = mappings[i];
        oss << R"({"from_field":")" << EscapeJsonString(m.from_field)
            << R"(","to_field":")" << EscapeJsonString(m.to_field) << R"(")";
        if (!m.rule_type.empty()) {
            oss << R"(,"rule_type":")" << EscapeJsonString(m.rule_type) << R"(")";
        }
        if (!m.formula.empty()) {
            oss << R"(,"formula":")" << EscapeJsonString(m.formula) << R"(")";
        }
        if (!m.constant.empty()) {
            oss << R"(,"constant":")" << EscapeJsonString(m.constant) << R"(")";
        }
        oss << "}";
    }
    oss << "]";
    return oss.str();
}

// Very small JSON-array reader for the [{"from_field":x,"to_field":y}, ...]
// shape this module itself writes — not a general JSON parser.
std::vector<std::pair<std::string, std::string>> ParseFieldMappingJson(
    const std::string& json) {
    std::vector<std::pair<std::string, std::string>> pairs;
    auto extract = [&](size_t obj_start, size_t obj_end, const std::string& key) -> std::string {
        auto key_pos = json.find("\"" + key + "\"", obj_start);
        if (key_pos == std::string::npos || key_pos >= obj_end) return "";
        auto colon = json.find(':', key_pos);
        auto quote1 = json.find('"', colon);
        auto quote2 = json.find('"', quote1 + 1);
        if (quote1 == std::string::npos || quote2 == std::string::npos) return "";
        return json.substr(quote1 + 1, quote2 - quote1 - 1);
    };

    size_t pos = 0;
    while (true) {
        auto obj_start = json.find('{', pos);
        if (obj_start == std::string::npos) break;
        auto obj_end = json.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        auto from_field = extract(obj_start, obj_end, "from_field");
        auto to_field = extract(obj_start, obj_end, "to_field");
        if (!from_field.empty() || !to_field.empty()) {
            pairs.emplace_back(from_field, to_field);
        }
        pos = obj_end + 1;
    }
    return pairs;
}

} // anonymous namespace

BwLineageConversion ConvertBwLineageGraph(const std::string& system_sid,
                                           const BwLineageGraph& graph,
                                           const std::set<std::string>& known_entity_ids) {
    BwLineageConversion result;
    std::set<std::string> all_known = known_entity_ids;

    // Resolve every node to its owning entity ID, stubbing in entities for
    // anything not already part of the current build.
    std::map<std::string, EntityId> node_owner;  // graph node id -> owning EntityId
    std::map<std::string, std::string> node_owner_field;  // field nodes only: field name

    for (const auto& node : graph.nodes) {
        std::string object_type;
        std::string object_name;
        std::string field_name;

        if (auto field_ref = ParseFieldNodeId(node.id)) {
            object_type = field_ref->object_type;
            object_name = field_ref->object_name;
            field_name = field_ref->field_name;
        } else if (auto obj_ref = ParseObjectNodeId(node.id)) {
            object_type = obj_ref->first;
            object_name = obj_ref->second;
        } else {
            continue;  // unrecognized node id shape — nothing to anchor an entity to
        }

        auto entity_id = DeriveEntityId(system_sid, CatalogDomain::Bw, object_type, object_name);
        node_owner.insert_or_assign(node.id, entity_id);
        if (!field_name.empty()) node_owner_field[node.id] = field_name;

        if (all_known.insert(entity_id.Value()).second) {
            CatalogEntity stub(entity_id);
            stub.system_sid = system_sid;
            stub.domain = CatalogDomain::Bw;
            stub.object_type = object_type;
            stub.technical_name = object_name;
            stub.display_name = object_name;
            stub.extracted_at = UtcTimestampNow();
            result.stub_entities.push_back(std::move(stub));
        }
    }

    // Direct object-to-object structural edges.
    static const std::set<std::string> kObjectEdgeTypes = {
        "dtp_source", "dtp_target", "trfn_source", "trfn_target"};
    static const std::set<std::string> kUsesEdgeTypes = {"xref"};
    static const std::set<std::string> kFieldEdgeTypes = {"field_mapping", "field_derivation"};

    std::map<std::pair<std::string, std::string>, std::string> direct_edge_kind;
    std::map<std::pair<std::string, std::string>, std::vector<FieldMappingEntry>>
        field_mappings;

    for (const auto& edge : graph.edges) {
        auto from_it = node_owner.find(edge.from);
        auto to_it = node_owner.find(edge.to);
        if (from_it == node_owner.end() || to_it == node_owner.end()) continue;

        auto key = std::make_pair(from_it->second.Value(), to_it->second.Value());

        if (kObjectEdgeTypes.count(edge.type) || kUsesEdgeTypes.count(edge.type)) {
            direct_edge_kind[key] = kUsesEdgeTypes.count(edge.type) ? "uses" : "lineage";
        } else if (kFieldEdgeTypes.count(edge.type)) {
            auto from_field_it = node_owner_field.find(edge.from);
            auto to_field_it = node_owner_field.find(edge.to);
            if (from_field_it != node_owner_field.end() && to_field_it != node_owner_field.end()) {
                FieldMappingEntry entry;
                entry.from_field = from_field_it->second;
                entry.to_field = to_field_it->second;
                auto rule_type_it = edge.attributes.find("rule_type");
                if (rule_type_it != edge.attributes.end()) entry.rule_type = rule_type_it->second;
                auto formula_it = edge.attributes.find("formula");
                if (formula_it != edge.attributes.end()) entry.formula = formula_it->second;
                auto constant_it = edge.attributes.find("constant");
                if (constant_it != edge.attributes.end()) entry.constant = constant_it->second;
                field_mappings[key].push_back(std::move(entry));
            }
        }
        // "contains_field", "field_origin", and anything else are structural
        // noise for the entity-level graph — intentionally not emitted.
    }

    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& [key, _] : direct_edge_kind) pairs.insert(key);
    for (const auto& [key, _] : field_mappings) pairs.insert(key);

    for (const auto& key : pairs) {
        CatalogEdge cedge(EntityId::Create(key.first).Value(), EntityId::Create(key.second).Value());
        cedge.id = key.first + "->" + key.second;
        auto kind_it = direct_edge_kind.find(key);
        cedge.kind = (kind_it != direct_edge_kind.end()) ? kind_it->second : "lineage";
        auto fm_it = field_mappings.find(key);
        if (fm_it != field_mappings.end()) {
            cedge.field_mapping_json = RenderFieldMappingJson(fm_it->second);
        }
        cedge.extracted_at = UtcTimestampNow();
        result.edges.push_back(std::move(cedge));
    }

    return result;
}

std::vector<CatalogEdge> CatalogWhereUsed(const CatalogFeed& feed, const EntityId& target) {
    std::vector<CatalogEdge> result;
    for (const auto& edge : feed.edges) {
        if (edge.to_id.Value() == target.Value()) {
            result.push_back(edge);
        }
    }
    return result;
}

std::vector<LineageHop> CatalogColumnLineage(const CatalogFeed& feed,
                                              const EntityId& start_entity,
                                              const std::string& field_name,
                                              int max_depth) {
    std::vector<LineageHop> chain;
    chain.push_back({start_entity, field_name});

    EntityId current_entity = start_entity;
    std::string current_field = field_name;
    std::set<std::string> visited = {start_entity.Value() + "#" + field_name};

    for (int depth = 0; depth < max_depth; ++depth) {
        bool advanced = false;
        for (const auto& edge : feed.edges) {
            if (edge.kind != "lineage") continue;
            if (edge.to_id.Value() != current_entity.Value()) continue;
            if (edge.field_mapping_json.empty()) continue;

            for (const auto& [from_field, to_field] : ParseFieldMappingJson(edge.field_mapping_json)) {
                if (to_field != current_field) continue;
                auto visit_key = edge.from_id.Value() + "#" + from_field;
                if (!visited.insert(visit_key).second) continue;  // avoid cycles

                chain.push_back({edge.from_id, from_field});
                current_entity = edge.from_id;
                current_field = from_field;
                advanced = true;
                break;
            }
            if (advanced) break;
        }
        if (!advanced) break;
    }

    return chain;
}

} // namespace erpl_adt
