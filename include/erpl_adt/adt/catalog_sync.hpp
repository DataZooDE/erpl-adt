#pragma once

#include <erpl_adt/adt/catalog_build.hpp>
#include <erpl_adt/adt/catalog_model.hpp>
#include <erpl_adt/storage/i_catalog_store.hpp>

#include <functional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// DiffFeedAgainstStore — pure classification of a freshly built feed against
// the set of entity IDs already in the store:
//   - added:   in the feed, id not in `existing_entity_ids`
//   - changed: in the feed, id already in `existing_entity_ids` — re-upserted
//     every sync (entity IDs are content-derived, but re-deriving from raw
//     SAP metadata every time is cheap; skipping unchanged rows would need
//     a field-by-field diff against the stored row, deferred as a future
//     optimization)
//   - removed: in `existing_entity_ids`, not in the feed
// Pure — no store access, so testable without a database.
// ---------------------------------------------------------------------------
struct CatalogDiff {
    std::vector<CatalogEntity> added;
    std::vector<CatalogEntity> changed;
    std::vector<EntityId> removed;
};

[[nodiscard]] CatalogDiff DiffFeedAgainstStore(
    const CatalogFeed& new_feed, const std::vector<std::string>& existing_entity_ids);

// ---------------------------------------------------------------------------
// Progress + checkpoint/resume support for CatalogSync. Reported once per
// package/infoarea as CatalogSync processes its scope one item at a time —
// checkpoint state is stored in the same DuckDB file via
// ICatalogStore's sync-checkpoint methods (see i_catalog_store.hpp).
// ---------------------------------------------------------------------------
struct CatalogSyncProgress {
    std::string item_kind;  // "package" | "infoarea"
    std::string item_name;
    int index = 0;  // 1-based
    int total = 0;
};
using CatalogSyncProgressCallback = std::function<void(const CatalogSyncProgress&)>;

struct CatalogSyncPipelineOptions {
    // If true, resume from the checkpoint already stored in `store` (see
    // ICatalogStore::LoadSyncCheckpoint) — skips packages/infoareas it
    // already marks done, continuing where an interrupted run left off. The
    // checkpoint's recorded scope must match `options` exactly (sid +
    // packages + infoareas, order-insensitive) — a mismatch is a user error
    // (wrong --db, wrong scope) and fails fast rather than silently
    // resuming the wrong thing. If false, always starts fresh, discarding
    // any prior checkpoint (whether it finished, was interrupted, or
    // belonged to a different scope entirely).
    bool resume = false;

    // Invoked after each package/infoarea finishes (whether freshly
    // processed or skipped because a resumed run already completed it —
    // skipped items are NOT reported, only fresh work is).
    CatalogSyncProgressCallback on_progress;
};

// ---------------------------------------------------------------------------
// CatalogSync — syncs `options`' scope into `store`, upserting only the
// delta (added + changed) and deleting anything that's disappeared from the
// scope, recording a sync_runs row. Unlike `catalog build --db` (full
// wipe-and-replace via WriteFeed), this minimizes write churn — though the
// SAP-side read is still a full walk of the scope (ADT has no bulk "changed
// since" query), so this optimizes store writes, not the SAP round-trip.
//
// Always processes one package/infoarea at a time (instead of building the
// whole scope in memory before writing anything), upserting each item's
// slice to the store and recording checkpoint progress (in the same
// DuckDB file, via ICatalogStore's sync-checkpoint methods — no separate
// sidecar file) as it goes. A fatal error (connection/auth/CSRF) after item
// N leaves items 1..N durably committed and resumable via
// `pipeline_opts.resume = true` on a later call against the same store,
// instead of discarding all N items' work.
//
// Removal detection (the "removed" count/deletes) only runs when this
// invocation is NOT a resume — a resumed run only has the items it
// personally processed in memory, not the full scope's picture, so
// removal would incorrectly flag every skipped-but-still-valid item as
// gone. Run a plain (non-resumed) sync afterwards if removal detection
// matters for a scope that was built up across multiple resumed calls.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<CatalogSyncRunSummary, Error> CatalogSync(
    IAdtSession& session, ICatalogStore& store, const CatalogBuildOptions& options,
    const std::string& scope_label,
    const CatalogSyncPipelineOptions& pipeline_opts = {});

} // namespace erpl_adt
