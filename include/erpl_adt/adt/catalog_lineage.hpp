#pragma once

#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/catalog_model.hpp>

#include <set>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ConvertBwLineageGraph — pure conversion of a BW lineage graph (already
// fetched via BwExportInfoarea/BwBuildLineageGraph) into Catalog Feed v1
// edges, generalizing BW's field-scoped node/edge model into entity-level
// `lineage`/`uses` edges with an aggregated field_mapping (FR-5/FR-6).
//
// A referenced object outside `known_entity_ids` gets a stub entity
// (per the catalog data model's dangling-edge resolution — a stub row, not
// a nullable FK) so every edge's from_id/to_id always resolves.
// ---------------------------------------------------------------------------
struct BwLineageConversion {
    std::vector<CatalogEntity> stub_entities;
    std::vector<CatalogEdge> edges;
};

[[nodiscard]] BwLineageConversion ConvertBwLineageGraph(
    const std::string& system_sid,
    const BwLineageGraph& graph,
    const std::set<std::string>& known_entity_ids);

// ---------------------------------------------------------------------------
// CatalogWhereUsed — every edge that points at `target` (catalog-wide
// aggregated where-used, FR-7). Pure lookup over an already-built feed.
// ---------------------------------------------------------------------------
[[nodiscard]] std::vector<CatalogEdge> CatalogWhereUsed(
    const CatalogFeed& feed,
    const EntityId& target);

// ---------------------------------------------------------------------------
// CatalogColumnLineage — end-to-end field lineage (FR-6): walk
// field_mapping edges backward from (start_entity, field_name), depth
// bounded. Returns the hop chain from the target field back to its
// earliest resolvable source, in traversal order (target first).
// ---------------------------------------------------------------------------
struct LineageHop {
    EntityId entity_id;
    std::string field_name;
};

[[nodiscard]] std::vector<LineageHop> CatalogColumnLineage(
    const CatalogFeed& feed,
    const EntityId& start_entity,
    const std::string& field_name,
    int max_depth = 10);

} // namespace erpl_adt
