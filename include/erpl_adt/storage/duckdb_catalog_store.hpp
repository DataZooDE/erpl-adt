#pragma once

#include <erpl_adt/storage/i_catalog_store.hpp>

#include <memory>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// DuckDbCatalogStore — real DuckDB-backed ICatalogStore.
//
// Opens the database once (RAII) and reuses the connection for every call
// (NFR-1 — no per-call ATTACH/DETACH). DuckDB is an implementation detail:
// this header never exposes duckdb.hpp to callers (pimpl).
// ---------------------------------------------------------------------------
class DuckDbCatalogStore : public ICatalogStore {
public:
    // `path` == ":memory:" (or empty) opens an ephemeral in-memory database;
    // any other value opens/creates a DuckDB file at that path.
    [[nodiscard]] static Result<std::unique_ptr<DuckDbCatalogStore>, Error> Open(
        const std::string& path, bool read_only = false);

    ~DuckDbCatalogStore() override;

    [[nodiscard]] Result<void, Error> WriteFeed(const CatalogFeed& feed) override;
    [[nodiscard]] Result<std::optional<CatalogEntity>, Error> GetEntity(
        const EntityId& id) override;
    [[nodiscard]] Result<std::vector<CatalogField>, Error> GetFields(
        const EntityId& entity_id) override;
    [[nodiscard]] Result<std::vector<CatalogEdge>, Error> GetEdgesTo(
        const EntityId& id, int max_results) override;
    [[nodiscard]] Result<std::vector<CatalogEdge>, Error> GetEdgesFrom(
        const EntityId& id, int max_results) override;
    [[nodiscard]] Result<std::vector<CatalogSyncRunSummary>, Error> RecentSyncRuns(
        int max_results) override;
    [[nodiscard]] Result<CatalogStats, Error> Stats() override;
    [[nodiscard]] Result<std::vector<ObjectTypeCount>, Error> ListObjectTypeCounts(
        const std::string& query = "") override;
    [[nodiscard]] Result<std::vector<ObjectSubtypeCount>, Error> ListObjectSubtypeCounts(
        const std::string& query = "") override;
    [[nodiscard]] Result<void, Error> ApplyOverlay(
        const EntityId& id, const OverlayFields& fields, const std::string& curated_by) override;
    [[nodiscard]] Result<std::vector<std::string>, Error> ListEntityIds(
        const std::vector<std::string>& package_or_infoarea_filter = {}) override;
    [[nodiscard]] Result<void, Error> UpsertEntitiesAndFields(
        const std::vector<CatalogEntity>& entities, const std::vector<CatalogField>& fields) override;
    [[nodiscard]] Result<void, Error> DeleteEntities(const std::vector<EntityId>& ids) override;
    [[nodiscard]] Result<void, Error> UpsertEdges(const std::vector<CatalogEdge>& edges) override;
    [[nodiscard]] Result<void, Error> RecordSyncRun(const CatalogSyncRunSummary& run) override;
    [[nodiscard]] Result<void, Error> ResetSyncCheckpoint(
        const std::string& sid, const std::vector<std::string>& packages,
        const std::vector<std::string>& infoareas) override;
    [[nodiscard]] Result<void, Error> RecordSyncCheckpointItem(
        const std::string& kind, const std::string& name, int entities, int fields) override;
    [[nodiscard]] Result<void, Error> MarkSyncCheckpointInterrupted(const std::string& reason) override;
    [[nodiscard]] Result<void, Error> MarkSyncCheckpointCompleted() override;
    [[nodiscard]] Result<SyncCheckpointState, Error> LoadSyncCheckpoint() override;
    [[nodiscard]] Result<std::vector<CatalogSearchHit>, Error> SearchFts(
        const std::string& query, int max_results) override;
    [[nodiscard]] Result<SearchPage, Error> SearchFtsPage(
        const std::string& query, const SearchOptions& options) override;
    [[nodiscard]] Result<void, Error> WriteEmbedding(
        const EntityId& entity_id, const std::vector<float>& embedding,
        const std::string& model) override;
    [[nodiscard]] Result<std::vector<CatalogSearchHit>, Error> SearchVss(
        const std::vector<float>& query_embedding, int max_results) override;
    [[nodiscard]] Result<std::vector<CatalogSearchHit>, Error> SearchHybrid(
        const std::string& query_text, const std::vector<float>& query_embedding,
        int max_results) override;
    [[nodiscard]] Result<int, Error> SchemaVersion() override;
    [[nodiscard]] Result<int64_t, Error> EntityCount() override;

    DuckDbCatalogStore(const DuckDbCatalogStore&) = delete;
    DuckDbCatalogStore& operator=(const DuckDbCatalogStore&) = delete;

private:
    DuckDbCatalogStore();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace erpl_adt
