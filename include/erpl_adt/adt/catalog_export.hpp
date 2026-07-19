#pragma once

#include <erpl_adt/adt/catalog_model.hpp>

#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// RenderCatalogFeedJson — Catalog Feed v1 JSON (schema_version/contract +
// entities[]/fields[]/edges[]/warnings[]). Deterministic for a given feed
// (NFR-2): callers should ensure `feed`'s entities/fields/edges are already
// in a stable sort order (CatalogBuild does this).
// ---------------------------------------------------------------------------
[[nodiscard]] std::string RenderCatalogFeedJson(const CatalogFeed& feed);

// ---------------------------------------------------------------------------
// RenderCatalogFeedOpenMetadataJson — OpenMetadata "table" profile.
// DDIC/CDS-domain entities become tables with columns; other domains are
// included as bare entities without columns (OpenMetadata has no ABAP-class
// equivalent, so this profile only claims field-level fidelity for
// DDIC/CDS). Generalizes BwRenderExportOpenMetadataJson to any domain.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string RenderCatalogFeedOpenMetadataJson(
    const CatalogFeed& feed,
    const std::string& service_name = "erpl_adt",
    const std::string& system_id = "");

// ---------------------------------------------------------------------------
// RenderCatalogFeedMermaid — a flowchart of entities connected by edges.
// Simpler than BW's lane-based dataflow diagram (bw export-area --mermaid);
// this is a domain-agnostic overview, not a replacement for it.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string RenderCatalogFeedMermaid(const CatalogFeed& feed);

} // namespace erpl_adt
