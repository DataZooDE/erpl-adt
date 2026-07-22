#pragma once

#include <erpl_adt/adt/catalog_model.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CatalogSearchHit — one ranked result from ICatalogStore::Search.
// ---------------------------------------------------------------------------
struct CatalogSearchHit {
    CatalogEntity entity;
    double score = 0.0;
};

// ---------------------------------------------------------------------------
// SyncCheckpointState — resumable progress for CatalogSync's per-item loop,
// persisted inside the same DuckDB file as the catalog data (not a separate
// sidecar file) so there's exactly one artifact to move/copy/back up. At
// most one checkpoint exists at a time — starting a fresh (non-resumed)
// sync replaces it. `exists = false` means nothing to resume (a normal,
// non-error state, e.g. before the first-ever sync of a fresh file).
// ---------------------------------------------------------------------------
struct SyncCheckpointState {
    bool exists = false;
    bool interrupted = false;  // true if the last recorded status was "interrupted"
    std::string sid;
    std::vector<std::string> requested_packages;
    std::vector<std::string> requested_infoareas;
    std::set<std::string> completed_packages;
    std::set<std::string> completed_infoareas;
};

// ---------------------------------------------------------------------------
// ICatalogStore — abstract read/write interface over the persisted catalog
// (DuckDB-backed today; abstracted so callers — CLI, MCP tools, tests — never
// depend on DuckDB directly). Mirrors the IAdtSession/IXmlCodec abstraction
// discipline: constructor-injectable, hand-mockable, no hidden globals.
// ---------------------------------------------------------------------------
class ICatalogStore {
public:
    virtual ~ICatalogStore() = default;

    // Writes every entity/field/edge in `feed` (upsert by primary key) and
    // rebuilds the derived search text/FTS index. Does not touch sync_runs.
    [[nodiscard]] virtual Result<void, Error> WriteFeed(const CatalogFeed& feed) = 0;

    [[nodiscard]] virtual Result<std::optional<CatalogEntity>, Error> GetEntity(
        const EntityId& id) = 0;

    [[nodiscard]] virtual Result<std::vector<CatalogField>, Error> GetFields(
        const EntityId& entity_id) = 0;

    // Edges pointing AT `id` (catalog-wide where-used) and edges pointing
    // FROM `id` (outgoing lineage/uses), each capped at `max_results`.
    [[nodiscard]] virtual Result<std::vector<CatalogEdge>, Error> GetEdgesTo(
        const EntityId& id, int max_results) = 0;
    [[nodiscard]] virtual Result<std::vector<CatalogEdge>, Error> GetEdgesFrom(
        const EntityId& id, int max_results) = 0;

    [[nodiscard]] virtual Result<std::vector<CatalogSyncRunSummary>, Error> RecentSyncRuns(
        int max_results) = 0;

    struct CatalogStats {
        int64_t entity_count = 0;
        int64_t field_count = 0;
        int64_t edge_count = 0;
        int64_t unresolved_edge_count = 0;
        int64_t curated_entity_count = 0;
    };
    [[nodiscard]] virtual Result<CatalogStats, Error> Stats() = 0;

    // One row per distinct (domain, object_type) actually present in the
    // catalog, with a count — lets a caller (the Discover UI's object-type
    // filter) offer exactly what's really there instead of a hardcoded
    // guess at every domain's possible object types (which vary a lot,
    // especially BW: IOBJ/ADSO/CUBE/ELEM/... vs ABAP's TABL/CLAS/FUGR/...).
    struct ObjectTypeCount {
        std::string domain;
        std::string object_type;
        int64_t count = 0;
    };
    // `query` narrows counts to the same match a SearchFtsPage call with
    // this query text would return — empty (or "*") means "browse all",
    // matching SearchFtsPage's own convention, so the Discover UI's chip
    // counts stay in sync with whatever the user has actually typed
    // instead of always reflecting the whole catalog.
    [[nodiscard]] virtual Result<std::vector<ObjectTypeCount>, Error> ListObjectTypeCounts(
        const std::string& query = "") = 0;

    // One row per distinct (domain, object_type, object_subtype) actually
    // present — a level deeper than ListObjectTypeCounts, only meaningful
    // where object_subtype is set (BW ELEM today: REP/VAR/CKF/RKF/FILT/STR).
    // Kept as its own method rather than widening ListObjectTypeCounts so
    // the existing (domain, object_type) grain — and the object-type filter
    // counts that already depend on it — don't change.
    struct ObjectSubtypeCount {
        std::string domain;
        std::string object_type;
        std::string object_subtype;
        int64_t count = 0;
    };
    [[nodiscard]] virtual Result<std::vector<ObjectSubtypeCount>, Error>
        ListObjectSubtypeCounts(const std::string& query = "") = 0;

    // Business overlay fields for one entity (`catalog annotate`, P3).
    // std::nullopt fields are left unchanged; empty string clears a field.
    // Fails (rather than silently creating a stub) if `id` isn't an
    // existing entity — an overlay entry with no matching entity is an
    // orphan, reported by the caller, not written.
    struct OverlayFields {
        std::optional<std::string> definition;
        std::optional<std::string> owner;
        std::optional<std::string> lob;
        std::optional<std::string> confidentiality;
    };
    [[nodiscard]] virtual Result<void, Error> ApplyOverlay(
        const EntityId& id, const OverlayFields& fields, const std::string& curated_by) = 0;

    // --- Incremental sync (P4) -----------------------------------------
    // Currently-stored entity IDs, for diffing against a freshly built
    // feed (added = in feed, not here; removed = here, not in feed).
    // `package_or_infoarea_filter` restricts to entities whose
    // package_or_infoarea is in the list — a `catalog sync --package A`
    // must only ever consider A's own entities as candidates for removal,
    // never delete entities belonging to some other previously-sunk
    // package/infoarea that simply wasn't part of this sync's scope. An
    // empty filter means "all entities" (used by full-feed comparisons).
    [[nodiscard]] virtual Result<std::vector<std::string>, Error> ListEntityIds(
        const std::vector<std::string>& package_or_infoarea_filter = {}) = 0;

    // Upserts exactly these entities (replacing any existing row with the
    // same id) and replaces their fields wholesale — for the "added" and
    // "changed" set of an incremental sync, not a full-feed rebuild.
    [[nodiscard]] virtual Result<void, Error> UpsertEntitiesAndFields(
        const std::vector<CatalogEntity>& entities, const std::vector<CatalogField>& fields) = 0;

    // Deletes these entities and their fields — the "removed" set of an
    // incremental sync.
    [[nodiscard]] virtual Result<void, Error> DeleteEntities(
        const std::vector<EntityId>& ids) = 0;

    // Upserts exactly these edges (replacing any existing row with the same
    // id) — the incremental-write counterpart to UpsertEntitiesAndFields for
    // edges, used by CatalogSync's per-item resumable loop so BW lineage
    // edges aren't lost when a sync writes one package/infoarea at a time
    // instead of one full-feed WriteFeed call.
    [[nodiscard]] virtual Result<void, Error> UpsertEdges(
        const std::vector<CatalogEdge>& edges) = 0;

    [[nodiscard]] virtual Result<void, Error> RecordSyncRun(
        const CatalogSyncRunSummary& run) = 0;

    // --- Sync checkpoint/resume (stored alongside the catalog data) ----
    // Starts a fresh checkpoint, replacing any prior one (whether it
    // finished, was interrupted, or belonged to a different scope
    // entirely) — the caller is responsible for deciding whether to call
    // this or resume from LoadSyncCheckpoint first.
    [[nodiscard]] virtual Result<void, Error> ResetSyncCheckpoint(
        const std::string& sid, const std::vector<std::string>& packages,
        const std::vector<std::string>& infoareas) = 0;

    // Marks one package/infoarea done — upsert by (kind, name), so calling
    // this twice for the same item (e.g. a retried write) doesn't duplicate.
    [[nodiscard]] virtual Result<void, Error> RecordSyncCheckpointItem(
        const std::string& kind, const std::string& name, int entities, int fields) = 0;

    [[nodiscard]] virtual Result<void, Error> MarkSyncCheckpointInterrupted(
        const std::string& reason) = 0;
    [[nodiscard]] virtual Result<void, Error> MarkSyncCheckpointCompleted() = 0;

    [[nodiscard]] virtual Result<SyncCheckpointState, Error> LoadSyncCheckpoint() = 0;

    // Full-text search over technical_name/display_name/object_type
    // (BM25-ranked), highest score first. An empty (or "*") query browses
    // all entities in technical-name order instead of ranking a match.
    [[nodiscard]] virtual Result<std::vector<CatalogSearchHit>, Error> SearchFts(
        const std::string& query, int max_results) = 0;

    // Cursor-paginated, filterable form of SearchFts — the `catalog_search`
    // MCP tool's backing call. `offset` is an opaque-to-the-client integer
    // cursor (the previous page's offset + hits.size()); `domain`/
    // `curated_only` narrow the same query rather than filtering
    // client-side after the fact. Plain SearchFts(query, max_results)
    // remains the unfiltered/unpaginated convenience form used by the CLI
    // and by SearchHybrid's fusion.
    struct SearchOptions {
        int max_results = 20;
        int offset = 0;
        std::optional<std::string> domain;        // "ABAP" | "DDIC" | "CDS" | "BW"
        std::optional<std::string> object_type;   // exact match, e.g. "TABL/DT", "IOBJ"
        std::optional<std::string> object_subtype;  // exact match, e.g. "REP" (BW query)
        bool curated_only = false;
    };
    struct SearchPage {
        std::vector<CatalogSearchHit> hits;
        bool has_more = false;
    };
    [[nodiscard]] virtual Result<SearchPage, Error> SearchFtsPage(
        const std::string& query, const SearchOptions& options) = 0;

    // Replaces the embedding vector for one entity (upsert). `model` is
    // stored alongside so a future re-embed with a different model is
    // detectable rather than silently mixed with stale vectors.
    [[nodiscard]] virtual Result<void, Error> WriteEmbedding(
        const EntityId& entity_id, const std::vector<float>& embedding,
        const std::string& model) = 0;

    // Nearest-neighbor search over entity_embeddings (cosine distance via
    // the HNSW index), highest similarity first.
    [[nodiscard]] virtual Result<std::vector<CatalogSearchHit>, Error> SearchVss(
        const std::vector<float>& query_embedding, int max_results) = 0;

    // Reciprocal-rank fusion of SearchFts(query_text) and
    // SearchVss(query_embedding) — same tie-breaking rule every call
    // (NFR-4: deterministic ranking for a fixed cache state and query).
    [[nodiscard]] virtual Result<std::vector<CatalogSearchHit>, Error> SearchHybrid(
        const std::string& query_text, const std::vector<float>& query_embedding,
        int max_results) = 0;

    [[nodiscard]] virtual Result<int, Error> SchemaVersion() = 0;
    [[nodiscard]] virtual Result<int64_t, Error> EntityCount() = 0;
};

} // namespace erpl_adt
