#pragma once

#include <erpl_adt/core/catalog_types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CatalogEntity — one row of the unified cross-domain catalog (Catalog Feed
// v1 §4.1). `id` is derived via DeriveEntityId (adt/catalog_ids.hpp) and is
// stable across builds for an unchanged object.
// ---------------------------------------------------------------------------
struct CatalogEntity {
    explicit CatalogEntity(EntityId entity_id) : id(std::move(entity_id)) {}

    EntityId id;
    std::string system_sid;
    CatalogDomain domain = CatalogDomain::Abap;
    std::string object_type;       // e.g. "TABL", "DDLS", "CLAS", "ADSO", "Query"
    std::string technical_name;
    std::string display_name;      // curated label if present, else technical_name
    std::optional<std::string> package_or_infoarea;
    std::optional<std::string> created_by;
    std::optional<std::string> changed_by;
    std::optional<std::string> changed_at;

    // Business overlay (nullable = uncurated) — populated by catalog_overlay.
    std::optional<std::string> biz_definition;
    std::optional<std::string> biz_owner;
    std::optional<std::string> biz_lob;
    std::optional<std::string> biz_confidentiality;
    std::optional<std::string> biz_curated_by;
    std::optional<std::string> biz_curated_at;

    std::string extracted_at;      // provenance
    std::string raw_json;          // full source payload, for drill-down (may be empty)
};

// ---------------------------------------------------------------------------
// CatalogField — a component of an entity (DDIC field, key figure,
// characteristic, CDS exposed field/association).
// ---------------------------------------------------------------------------
struct CatalogField {
    explicit CatalogField(EntityId owning_entity_id) : entity_id(std::move(owning_entity_id)) {}

    std::string id;
    EntityId entity_id;
    std::string name;
    std::optional<std::string> role;         // key_figure | characteristic | field
    std::optional<std::string> data_type;
    std::optional<int> length;
    std::optional<int> decimals;
    std::optional<std::string> aggregation;  // key-figure attributes
    std::optional<std::string> unit;
    std::optional<std::string> formula;      // calculated key figure expression
};

// ---------------------------------------------------------------------------
// CatalogEdge — a relationship between two entities.
// ---------------------------------------------------------------------------
struct CatalogEdge {
    CatalogEdge(EntityId from, EntityId to) : from_id(std::move(from)), to_id(std::move(to)) {}

    std::string id;
    EntityId from_id;
    EntityId to_id;
    std::string kind;                 // lineage | where_used | contains | uses
    std::string field_mapping_json;   // JSON array text: [{from_field,to_field}], empty if n/a
    std::string resolution = "resolved";  // resolved | ambiguous | unresolved
    std::string extracted_at;
};

// ---------------------------------------------------------------------------
// CatalogFeed — Catalog Feed v1: the full build output.
// ---------------------------------------------------------------------------
struct CatalogFeed {
    std::string schema_version = "1.0";
    std::string contract = "catalog.feed.v1";
    std::string system_sid;
    std::string built_at;
    std::vector<CatalogEntity> entities;
    std::vector<CatalogField> fields;
    std::vector<CatalogEdge> edges;
    std::vector<std::string> warnings;
};

// ---------------------------------------------------------------------------
// CatalogSyncRunSummary — one row of operational sync history (sync_runs
// table), surfaced via catalog_sync_status. Populated by `catalog sync`
// (incremental sync) — a full `catalog build --db` does not record a run here.
// ---------------------------------------------------------------------------
struct CatalogSyncRunSummary {
    std::string id;
    std::string started_at;
    std::string finished_at;
    std::string mode;   // full | incremental
    std::string scope;
    int added = 0;
    int changed = 0;
    int removed = 0;
    std::string status;
};

} // namespace erpl_adt
