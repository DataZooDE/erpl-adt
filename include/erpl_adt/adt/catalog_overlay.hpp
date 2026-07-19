#pragma once

#include <erpl_adt/core/catalog_types.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/storage/i_catalog_store.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// OverlayEntry — one YAML entry: an entity_id and the business fields to
// curate onto it.
// ---------------------------------------------------------------------------
struct OverlayEntry {
    std::string entity_id;
    std::optional<std::string> definition;
    std::optional<std::string> owner;
    std::optional<std::string> lob;
    std::optional<std::string> confidentiality;  // Public | Internal | Confidential
};

// ---------------------------------------------------------------------------
// ParseOverlayYaml — parses a business-overlay YAML file, keyed by entity_id:
//
//   <entity_id>:
//     definition: "..."
//     owner: "..."
//     lob: "..."
//     confidentiality: Public|Internal|Confidential
//
// Pure/I-O-free — no store or SAP access. `confidentiality`, if present,
// must be one of the three allowed values; an invalid value is a parse
// error (fail fast on curation input, not silently accepted).
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<OverlayEntry>, std::string> ParseOverlayYaml(
    const std::string& yaml_text);

// ---------------------------------------------------------------------------
// ApplyOverlayResult — outcome of applying a parsed overlay to a store.
// ---------------------------------------------------------------------------
struct ApplyOverlayResult {
    int applied_count = 0;
    std::vector<std::string> orphan_ids;   // entries whose entity_id doesn't exist (FR-11)
    std::vector<std::string> write_errors; // store-level failures, keyed by entity_id in the message
};

// ---------------------------------------------------------------------------
// ApplyOverlay — writes each entry's business fields into `store`,
// collecting orphans (unknown entity_id) rather than failing the whole
// batch on one bad entry.
// ---------------------------------------------------------------------------
[[nodiscard]] ApplyOverlayResult ApplyOverlay(ICatalogStore& store,
                                               const std::vector<OverlayEntry>& entries,
                                               const std::string& curated_by);

} // namespace erpl_adt
