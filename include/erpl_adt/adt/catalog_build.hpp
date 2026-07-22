#pragma once

#include <erpl_adt/adt/catalog_model.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CatalogBuildOptions — scope for a catalog build.
//
// `packages` walks ABAP/DDIC/CDS objects via ListPackageTree (recursive);
// `infoareas` walks BW objects via BwExportInfoarea. Either may be empty.
// ---------------------------------------------------------------------------
struct CatalogBuildOptions {
    std::string system_sid;
    std::vector<std::string> packages;
    std::vector<std::string> infoareas;
    std::optional<std::string> type_filter;  // e.g. "TABL", "CLAS" — ABAP/DDIC scope only
    int max_depth = 50;
    bool resolve_ddic_types = false;  // true = enrich TABL fields via data-element lookups (slower)
    // true = also stash each object's full raw source/XML into
    // CatalogEntity.raw_json — a fallback for whatever isn't structurally
    // modeled yet. Off by default: a full-system crawl already touches
    // thousands of objects, and most of them are never opened again, so
    // carrying full source for all of them by default would bloat the
    // DB for little benefit.
    bool include_raw_source = false;
};

// ---------------------------------------------------------------------------
// CatalogBuild — walk a scope and normalize into a Catalog Feed v1.
//
// Reuses existing readers (ListPackageTree, GetTableDefinition,
// GetCdsStructure, BwExportInfoarea) — this is a mapping/aggregation layer,
// not a new extraction protocol. Partial-failure tolerant: an unresolved
// object is recorded in feed.warnings, not a hard error — one bad object
// must not fail the whole build.
//
// Never hard-fails except for i/o errors unrelated to any single object
// (e.g. session/auth failure surfaces immediately since nothing downstream
// can succeed either).
// ---------------------------------------------------------------------------
[[nodiscard]] Result<CatalogFeed, Error> CatalogBuild(
    IAdtSession& session,
    const CatalogBuildOptions& options);

// ---------------------------------------------------------------------------
// Per-item building blocks, exposed so CatalogSync's resumable loop
// (adt/catalog_sync.hpp) can process one package/infoarea at a time with a
// checkpoint written between items, instead of only via CatalogBuild's
// single all-or-nothing walk of the whole scope. `seen_ids` must be the same
// set threaded across every call in a run (cross-item/cross-invocation
// dedup); on a resumed run, seed it from the store's existing entity IDs.
// ---------------------------------------------------------------------------
[[nodiscard]] bool IsFatalForWholeBuild(const Error& error);

[[nodiscard]] Result<void, Error> BuildAbapDdicCdsScope(
    IAdtSession& session, const std::string& package, const CatalogBuildOptions& options,
    CatalogFeed& feed, std::set<std::string>& seen_ids);

[[nodiscard]] Result<void, Error> BuildBwScope(
    IAdtSession& session, const std::string& infoarea, const CatalogBuildOptions& options,
    CatalogFeed& feed, std::set<std::string>& seen_ids);

} // namespace erpl_adt
