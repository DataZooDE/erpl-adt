#include <erpl_adt/adt/catalog_overlay.hpp>

#include <yaml-cpp/yaml.h>

#include <set>

namespace erpl_adt {

namespace {

bool IsValidConfidentiality(const std::string& value) {
    static const std::set<std::string> kAllowed = {"Public", "Internal", "Confidential"};
    return kAllowed.count(value) > 0;
}

std::optional<std::string> OptScalar(const YAML::Node& node, const char* key) {
    if (!node[key] || !node[key].IsScalar()) return std::nullopt;
    return node[key].as<std::string>();
}

} // anonymous namespace

Result<std::vector<OverlayEntry>, std::string> ParseOverlayYaml(const std::string& yaml_text) {
    std::vector<OverlayEntry> entries;
    if (yaml_text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return Result<std::vector<OverlayEntry>, std::string>::Ok(std::move(entries));
    }

    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& ex) {
        return Result<std::vector<OverlayEntry>, std::string>::Err(
            std::string("Invalid overlay YAML: ") + ex.what());
    }

    if (!root.IsDefined() || root.IsNull()) {
        return Result<std::vector<OverlayEntry>, std::string>::Ok(std::move(entries));
    }
    if (!root.IsMap()) {
        return Result<std::vector<OverlayEntry>, std::string>::Err(
            "Overlay YAML must be a map keyed by entity_id");
    }

    for (const auto& kv : root) {
        OverlayEntry entry;
        entry.entity_id = kv.first.as<std::string>();
        const auto& node = kv.second;
        if (!node.IsMap()) {
            return Result<std::vector<OverlayEntry>, std::string>::Err(
                "Overlay entry '" + entry.entity_id + "' must be a map of business fields");
        }

        entry.definition = OptScalar(node, "definition");
        entry.owner = OptScalar(node, "owner");
        entry.lob = OptScalar(node, "lob");
        entry.confidentiality = OptScalar(node, "confidentiality");
        if (entry.confidentiality.has_value() && !IsValidConfidentiality(*entry.confidentiality)) {
            return Result<std::vector<OverlayEntry>, std::string>::Err(
                "Overlay entry '" + entry.entity_id + "': invalid confidentiality '" +
                *entry.confidentiality + "' (expected Public|Internal|Confidential)");
        }

        entries.push_back(std::move(entry));
    }

    return Result<std::vector<OverlayEntry>, std::string>::Ok(std::move(entries));
}

ApplyOverlayResult ApplyOverlay(ICatalogStore& store, const std::vector<OverlayEntry>& entries,
                                 const std::string& curated_by) {
    ApplyOverlayResult result;

    for (const auto& entry : entries) {
        auto id = EntityId::Create(entry.entity_id);
        if (id.IsErr()) {
            result.orphan_ids.push_back(entry.entity_id);
            continue;
        }

        auto existing = store.GetEntity(id.Value());
        if (existing.IsErr()) {
            result.write_errors.push_back(entry.entity_id + ": " + existing.Error().ToString());
            continue;
        }
        if (!existing.Value().has_value()) {
            result.orphan_ids.push_back(entry.entity_id);
            continue;
        }

        ICatalogStore::OverlayFields fields;
        fields.definition = entry.definition;
        fields.owner = entry.owner;
        fields.lob = entry.lob;
        fields.confidentiality = entry.confidentiality;

        auto apply_result = store.ApplyOverlay(id.Value(), fields, curated_by);
        if (apply_result.IsErr()) {
            result.write_errors.push_back(entry.entity_id + ": " + apply_result.Error().ToString());
            continue;
        }
        ++result.applied_count;
    }

    return result;
}

} // namespace erpl_adt
