#include <erpl_adt/storage/duckdb_catalog_store.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>

#include <duckdb.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <sstream>

namespace erpl_adt {

namespace {

constexpr int kSchemaVersion = 1;

const char* kSchemaDdl = R"sql(
CREATE TABLE IF NOT EXISTS schema_version (
    version    INTEGER NOT NULL,
    applied_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS entities (
    id                  VARCHAR PRIMARY KEY,
    system_sid          VARCHAR NOT NULL,
    domain              VARCHAR NOT NULL,
    object_type         VARCHAR NOT NULL,
    object_subtype      VARCHAR,
    technical_name      VARCHAR NOT NULL,
    display_name        VARCHAR,
    package_or_infoarea VARCHAR,
    created_by          VARCHAR,
    changed_by          VARCHAR,
    changed_at          TIMESTAMP,
    biz_definition      VARCHAR,
    biz_owner           VARCHAR,
    biz_lob             VARCHAR,
    biz_confidentiality VARCHAR,
    biz_curated_by      VARCHAR,
    biz_curated_at      TIMESTAMP,
    extracted_at        TIMESTAMP NOT NULL,
    raw_json            VARCHAR,
    source_table        VARCHAR
);

CREATE TABLE IF NOT EXISTS fields (
    id            VARCHAR PRIMARY KEY,
    entity_id     VARCHAR NOT NULL,
    name          VARCHAR NOT NULL,
    role          VARCHAR,
    description   VARCHAR,
    data_type     VARCHAR,
    length        INTEGER,
    decimals      INTEGER,
    aggregation   VARCHAR,
    unit          VARCHAR,
    formula       VARCHAR,
    is_key        BOOLEAN NOT NULL DEFAULT FALSE,
    check_table   VARCHAR,
    fixed_values  VARCHAR,
    source_expr   VARCHAR,
    annotations   VARCHAR
);

CREATE TABLE IF NOT EXISTS edges (
    id            VARCHAR PRIMARY KEY,
    from_id       VARCHAR NOT NULL,
    to_id         VARCHAR NOT NULL,
    kind          VARCHAR NOT NULL,
    field_mapping VARCHAR,
    resolution    VARCHAR NOT NULL DEFAULT 'resolved',
    extracted_at  TIMESTAMP NOT NULL,
    detail        VARCHAR
);

CREATE TABLE IF NOT EXISTS sync_runs (
    id            VARCHAR PRIMARY KEY,
    started_at    TIMESTAMP,
    finished_at   TIMESTAMP,
    mode          VARCHAR,
    scope         VARCHAR,
    added         INTEGER,
    changed       INTEGER,
    removed       INTEGER,
    status        VARCHAR,
    manifest_json VARCHAR
);

-- At most one row: the currently in-progress/interrupted/completed
-- CatalogSync checkpoint. A fresh (non-resumed) sync deletes and
-- re-inserts this row; packages/infoareas are JSON array text.
CREATE TABLE IF NOT EXISTS sync_checkpoint (
    sid        VARCHAR NOT NULL,
    packages   VARCHAR NOT NULL,
    infoareas  VARCHAR NOT NULL,
    status     VARCHAR NOT NULL,
    reason     VARCHAR,
    started_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS sync_checkpoint_items (
    kind         VARCHAR NOT NULL,
    name         VARCHAR NOT NULL,
    entities     INTEGER,
    fields       INTEGER,
    completed_at TIMESTAMP NOT NULL,
    PRIMARY KEY (kind, name)
);
)sql";

// Fixed to match GeminiEmbeddingProvider::Dimensions() (text-embedding-004).
constexpr int kEmbeddingDimensions = 768;

Error MakeStoreError(const std::string& operation, const std::string& message) {
    return Error{operation, "", std::nullopt, message, std::nullopt, ErrorCategory::Internal};
}

void AppendOptString(duckdb::Appender& app, const std::optional<std::string>& value) {
    if (value.has_value()) {
        app.Append<const char*>(value->c_str());
    } else {
        app.Append<std::nullptr_t>(nullptr);
    }
}

void AppendOptInt(duckdb::Appender& app, const std::optional<int>& value) {
    if (value.has_value()) {
        app.Append<int32_t>(*value);
    } else {
        app.Append<std::nullptr_t>(nullptr);
    }
}

std::string ValueToStringOrEmpty(const duckdb::Value& v) {
    return v.IsNull() ? std::string() : v.ToString();
}

std::optional<std::string> ValueToOptString(const duckdb::Value& v) {
    if (v.IsNull()) return std::nullopt;
    return v.ToString();
}

std::optional<int> ValueToOptInt(const duckdb::Value& v) {
    if (v.IsNull()) return std::nullopt;
    return v.GetValue<int32_t>();
}

// PreparedStatement::Execute(args...) returns the base QueryResult type
// (which may stream). Forcing allow_stream_result=false guarantees a
// MaterializedQueryResult, which is what exposes RowCount()/GetValue() —
// this project only ever wants fully-materialized results.
template <typename... Args>
std::unique_ptr<duckdb::MaterializedQueryResult> ExecuteMaterialized(
    duckdb::PreparedStatement& stmt, Args&&... args) {
    duckdb::vector<duckdb::Value> values{duckdb::Value(std::forward<Args>(args))...};
    auto result = stmt.Execute(values, /*allow_stream_result=*/false);
    return std::unique_ptr<duckdb::MaterializedQueryResult>(
        static_cast<duckdb::MaterializedQueryResult*>(result.release()));
}

// Same as ExecuteMaterialized, but for callers that already built a
// duckdb::vector<Value> themselves (e.g. an ARRAY-typed parameter, which
// duckdb::Value's implicit constructors can't produce).
std::unique_ptr<duckdb::MaterializedQueryResult> ExecuteMaterializedParams(
    duckdb::PreparedStatement& stmt, duckdb::vector<duckdb::Value> params) {
    auto result = stmt.Execute(params, /*allow_stream_result=*/false);
    return std::unique_ptr<duckdb::MaterializedQueryResult>(
        static_cast<duckdb::MaterializedQueryResult*>(result.release()));
}

duckdb::Value MakeArrayValue(const std::vector<float>& embedding) {
    duckdb::vector<duckdb::Value> values;
    values.reserve(embedding.size());
    for (float f : embedding) values.push_back(duckdb::Value::FLOAT(f));
    return duckdb::Value::ARRAY(duckdb::LogicalType::FLOAT, std::move(values));
}

CatalogEntity RowToEntity(duckdb::MaterializedQueryResult& result, idx_t row,
                           std::optional<CatalogDomain>& domain_out) {
    auto domain_result = CatalogDomainFromString(ValueToStringOrEmpty(result.GetValue(2, row)));
    domain_out = domain_result.IsOk() ? std::optional<CatalogDomain>(domain_result.Value())
                                       : std::nullopt;
    CatalogEntity entity(EntityId::Create(ValueToStringOrEmpty(result.GetValue(0, row))).Value());
    entity.system_sid = ValueToStringOrEmpty(result.GetValue(1, row));
    entity.domain = domain_out.value_or(CatalogDomain::Abap);
    entity.object_type = ValueToStringOrEmpty(result.GetValue(3, row));
    entity.object_subtype = ValueToOptString(result.GetValue(4, row));
    entity.technical_name = ValueToStringOrEmpty(result.GetValue(5, row));
    entity.display_name = ValueToStringOrEmpty(result.GetValue(6, row));
    entity.package_or_infoarea = ValueToOptString(result.GetValue(7, row));
    entity.extracted_at = ValueToStringOrEmpty(result.GetValue(8, row));
    entity.changed_at = ValueToOptString(result.GetValue(9, row));
    return entity;
}

} // anonymous namespace

struct DuckDbCatalogStore::Impl {
    std::unique_ptr<duckdb::DuckDB> db;
    std::unique_ptr<duckdb::Connection> con;
};

DuckDbCatalogStore::DuckDbCatalogStore() : impl_(std::make_unique<Impl>()) {}
DuckDbCatalogStore::~DuckDbCatalogStore() = default;

Result<std::unique_ptr<DuckDbCatalogStore>, Error> DuckDbCatalogStore::Open(
    const std::string& path, bool read_only) {
    // unique_ptr can't be constructed via std::make_unique on a class with a
    // private constructor from outside the class — but Open() is a static
    // member, so it has access.
    std::unique_ptr<DuckDbCatalogStore> store(new DuckDbCatalogStore());

    try {
        duckdb::DBConfig config;
        if (read_only) {
            config.options.access_mode = duckdb::AccessMode::READ_ONLY;
        }
        const bool in_memory = path.empty() || path == ":memory:";
        store->impl_->db = std::make_unique<duckdb::DuckDB>(
            in_memory ? nullptr : path.c_str(), read_only ? &config : nullptr);
        store->impl_->con = std::make_unique<duckdb::Connection>(*store->impl_->db);

        if (read_only) {
            // Extension loading is per-process, not per-database-file — a
            // read-only search session started fresh (e.g. `catalog search`)
            // still needs fts/vss LOADed itself to see match_bm25/
            // array_cosine_distance, even though the index was built by an
            // earlier write session. Best-effort: no network on first run
            // just means SearchFts/SearchVss degrade to empty results below.
            // Read-write opens don't need this here — WriteFeed and friends
            // each (re-)install fts with their own error-guarding, and vss is
            // (re-)installed just below — running an extra, unguarded
            // install attempt this early (before the schema DDL even runs)
            // gains nothing for the write path and risks whatever a failed/
            // slow extension fetch does to a brand-new connection's state
            // before any tables exist.
            store->impl_->con->Query("INSTALL fts; LOAD fts;");
            store->impl_->con->Query("INSTALL vss; LOAD vss;");
        } else {
            auto result = store->impl_->con->Query(kSchemaDdl);
            if (result->HasError()) {
                return Result<std::unique_ptr<DuckDbCatalogStore>, Error>::Err(
                    MakeStoreError("DuckDbCatalogStore::Open", result->GetError()));
            }

            auto version_check = store->impl_->con->Query("SELECT COUNT(*) FROM schema_version");
            if (!version_check->HasError() && version_check->RowCount() > 0 &&
                version_check->GetValue<int64_t>(0, 0) == 0) {
                std::ostringstream insert;
                insert << "INSERT INTO schema_version VALUES (" << kSchemaVersion
                       << ", now())";
                store->impl_->con->Query(insert.str());
            }

            // VSS is best-effort: on a first run without network access, the
            // extension can't be installed, and entity_embeddings simply
            // doesn't exist — WriteEmbedding/SearchVss degrade accordingly
            // rather than failing the whole Open() call.
            auto vss_install = store->impl_->con->Query("INSTALL vss; LOAD vss;");
            if (!vss_install->HasError()) {
                store->impl_->con->Query("SET hnsw_enable_experimental_persistence = true;");
                std::ostringstream create_embeddings;
                // No PRIMARY KEY here deliberately — a PK forces an ART
                // index alongside the HNSW vector index, which duckdb's vss
                // extension does not reliably support on the same column
                // set; uniqueness is instead enforced by WriteEmbedding's
                // explicit delete-then-insert.
                create_embeddings << "CREATE TABLE IF NOT EXISTS entity_embeddings ("
                                  << "entity_id VARCHAR NOT NULL, model VARCHAR NOT NULL, "
                                  << "embedding FLOAT[" << kEmbeddingDimensions << "] NOT NULL)";
                store->impl_->con->Query(create_embeddings.str());
                // Deliberately not creating a `USING HNSW` index here: on this
                // duckdb build + network-installed vss extension combination,
                // creating the HNSW index reproducibly crashes the process on
                // a later, unrelated query (not a catchable C++ exception —
                // a hard abort). SearchVss falls back to an unindexed
                // array_cosine_distance scan, which is correct (just not
                // sub-linear) — acceptable for the catalog scale this
                // targets (tens of thousands of entities, not millions).
                // Revisit once the vss extension / duckdb version pairing
                // stabilizes.
            }
        }
    } catch (const std::exception& ex) {
        return Result<std::unique_ptr<DuckDbCatalogStore>, Error>::Err(
            MakeStoreError("DuckDbCatalogStore::Open", ex.what()));
    }

    return Result<std::unique_ptr<DuckDbCatalogStore>, Error>::Ok(std::move(store));
}

Result<void, Error> DuckDbCatalogStore::WriteFeed(const CatalogFeed& feed) {
    auto& con = *impl_->con;
    try {
        con.BeginTransaction();
        con.Query("DELETE FROM edges");
        con.Query("DELETE FROM fields");
        con.Query("DELETE FROM entities");

        {
            duckdb::Appender app(con, "entities");
            for (const auto& e : feed.entities) {
                app.BeginRow();
                app.Append<const char*>(e.id.Value().c_str());
                app.Append<const char*>(e.system_sid.c_str());
                app.Append<const char*>(ToString(e.domain).c_str());
                app.Append<const char*>(e.object_type.c_str());
                AppendOptString(app, e.object_subtype);
                app.Append<const char*>(e.technical_name.c_str());
                app.Append<const char*>(e.display_name.c_str());
                AppendOptString(app, e.package_or_infoarea);
                AppendOptString(app, e.created_by);
                AppendOptString(app, e.changed_by);
                AppendOptString(app, e.changed_at);
                AppendOptString(app, e.biz_definition);
                AppendOptString(app, e.biz_owner);
                AppendOptString(app, e.biz_lob);
                AppendOptString(app, e.biz_confidentiality);
                AppendOptString(app, e.biz_curated_by);
                AppendOptString(app, e.biz_curated_at);
                app.Append<const char*>(e.extracted_at.c_str());
                if (e.raw_json.empty()) {
                    app.Append<std::nullptr_t>(nullptr);
                } else {
                    app.Append<const char*>(e.raw_json.c_str());
                }
                AppendOptString(app, e.source_table);
                app.EndRow();
            }
            app.Close();
        }

        {
            duckdb::Appender app(con, "fields");
            for (const auto& f : feed.fields) {
                app.BeginRow();
                app.Append<const char*>(f.id.c_str());
                app.Append<const char*>(f.entity_id.Value().c_str());
                app.Append<const char*>(f.name.c_str());
                AppendOptString(app, f.role);
                AppendOptString(app, f.description);
                AppendOptString(app, f.data_type);
                AppendOptInt(app, f.length);
                AppendOptInt(app, f.decimals);
                AppendOptString(app, f.aggregation);
                AppendOptString(app, f.unit);
                AppendOptString(app, f.formula);
                app.Append<bool>(f.is_key);
                AppendOptString(app, f.check_table);
                AppendOptString(app, f.fixed_values_json);
                AppendOptString(app, f.source_expression);
                AppendOptString(app, f.annotations_json);
                app.EndRow();
            }
            app.Close();
        }

        {
            duckdb::Appender app(con, "edges");
            for (const auto& e : feed.edges) {
                app.BeginRow();
                app.Append<const char*>(e.id.c_str());
                app.Append<const char*>(e.from_id.Value().c_str());
                app.Append<const char*>(e.to_id.Value().c_str());
                app.Append<const char*>(e.kind.c_str());
                if (e.field_mapping_json.empty()) {
                    app.Append<std::nullptr_t>(nullptr);
                } else {
                    app.Append<const char*>(e.field_mapping_json.c_str());
                }
                app.Append<const char*>(e.resolution.c_str());
                app.Append<const char*>(e.extracted_at.c_str());
                AppendOptString(app, e.detail_json);
                app.EndRow();
            }
            app.Close();
        }

        // Rebuild the search text + FTS index (best-effort: FTS requires the
        // `fts` extension to be installed/loaded, which needs network access
        // the first time — a missing index degrades SearchFts to "no
        // results" rather than failing the whole write).
        con.Query(
            "CREATE OR REPLACE TABLE search_docs AS "
            "SELECT id AS entity_id, "
            "concat_ws(' ', display_name, technical_name, object_type, "
            "biz_definition, biz_owner, biz_lob) AS text FROM entities");
        auto install = con.Query("INSTALL fts; LOAD fts;");
        if (!install->HasError()) {
            con.Query(
                "PRAGMA create_fts_index('search_docs', 'entity_id', 'text', "
                "stemmer='english', stopwords='english', overwrite=1)");
        }

        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("WriteFeed", ex.what()));
    }

    return Result<void, Error>::Ok();
}

Result<std::optional<CatalogEntity>, Error> DuckDbCatalogStore::GetEntity(const EntityId& id) {
    try {
        auto stmt = impl_->con->Prepare(
            "SELECT id, system_sid, domain, object_type, object_subtype, technical_name, "
            "display_name, package_or_infoarea, created_by, changed_by, changed_at, "
            "biz_definition, biz_owner, biz_lob, biz_confidentiality, biz_curated_by, "
            "biz_curated_at, extracted_at, raw_json, source_table FROM entities WHERE id = $1");
        if (stmt->HasError()) {
            return Result<std::optional<CatalogEntity>, Error>::Err(
                MakeStoreError("GetEntity", stmt->GetError()));
        }
        auto result = ExecuteMaterialized(*stmt, id.Value());
        if (result->HasError()) {
            return Result<std::optional<CatalogEntity>, Error>::Err(
                MakeStoreError("GetEntity", result->GetError()));
        }
        if (result->RowCount() == 0) {
            return Result<std::optional<CatalogEntity>, Error>::Ok(std::nullopt);
        }

        auto domain_result =
            CatalogDomainFromString(ValueToStringOrEmpty(result->GetValue(2, 0)));
        if (domain_result.IsErr()) {
            return Result<std::optional<CatalogEntity>, Error>::Err(
                MakeStoreError("GetEntity", domain_result.Error()));
        }

        CatalogEntity entity(EntityId::Create(ValueToStringOrEmpty(result->GetValue(0, 0))).Value());
        entity.system_sid = ValueToStringOrEmpty(result->GetValue(1, 0));
        entity.domain = domain_result.Value();
        entity.object_type = ValueToStringOrEmpty(result->GetValue(3, 0));
        entity.object_subtype = ValueToOptString(result->GetValue(4, 0));
        entity.technical_name = ValueToStringOrEmpty(result->GetValue(5, 0));
        entity.display_name = ValueToStringOrEmpty(result->GetValue(6, 0));
        entity.package_or_infoarea = ValueToOptString(result->GetValue(7, 0));
        entity.created_by = ValueToOptString(result->GetValue(8, 0));
        entity.changed_by = ValueToOptString(result->GetValue(9, 0));
        entity.changed_at = ValueToOptString(result->GetValue(10, 0));
        entity.biz_definition = ValueToOptString(result->GetValue(11, 0));
        entity.biz_owner = ValueToOptString(result->GetValue(12, 0));
        entity.biz_lob = ValueToOptString(result->GetValue(13, 0));
        entity.biz_confidentiality = ValueToOptString(result->GetValue(14, 0));
        entity.biz_curated_by = ValueToOptString(result->GetValue(15, 0));
        entity.biz_curated_at = ValueToOptString(result->GetValue(16, 0));
        entity.extracted_at = ValueToStringOrEmpty(result->GetValue(17, 0));
        entity.raw_json = ValueToStringOrEmpty(result->GetValue(18, 0));
        entity.source_table = ValueToOptString(result->GetValue(19, 0));

        return Result<std::optional<CatalogEntity>, Error>::Ok(std::move(entity));
    } catch (const std::exception& ex) {
        return Result<std::optional<CatalogEntity>, Error>::Err(MakeStoreError("GetEntity", ex.what()));
    }
}

Result<std::vector<CatalogField>, Error> DuckDbCatalogStore::GetFields(const EntityId& entity_id) {
    try {
        auto stmt = impl_->con->Prepare(
            "SELECT id, entity_id, name, role, description, data_type, length, decimals, "
            "aggregation, unit, formula, is_key, check_table, fixed_values, source_expr, "
            "annotations FROM fields WHERE entity_id = $1 ORDER BY name");
        if (stmt->HasError()) {
            return Result<std::vector<CatalogField>, Error>::Err(
                MakeStoreError("GetFields", stmt->GetError()));
        }
        auto result = ExecuteMaterialized(*stmt, entity_id.Value());
        if (result->HasError()) {
            return Result<std::vector<CatalogField>, Error>::Err(
                MakeStoreError("GetFields", result->GetError()));
        }

        std::vector<CatalogField> fields;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            CatalogField field(EntityId::Create(ValueToStringOrEmpty(result->GetValue(1, row))).Value());
            field.id = ValueToStringOrEmpty(result->GetValue(0, row));
            field.name = ValueToStringOrEmpty(result->GetValue(2, row));
            field.role = ValueToOptString(result->GetValue(3, row));
            field.description = ValueToOptString(result->GetValue(4, row));
            field.data_type = ValueToOptString(result->GetValue(5, row));
            field.length = ValueToOptInt(result->GetValue(6, row));
            field.decimals = ValueToOptInt(result->GetValue(7, row));
            field.aggregation = ValueToOptString(result->GetValue(8, row));
            field.unit = ValueToOptString(result->GetValue(9, row));
            field.formula = ValueToOptString(result->GetValue(10, row));
            field.is_key = !result->GetValue(11, row).IsNull() &&
                          result->GetValue<bool>(11, row);
            field.check_table = ValueToOptString(result->GetValue(12, row));
            field.fixed_values_json = ValueToOptString(result->GetValue(13, row));
            field.source_expression = ValueToOptString(result->GetValue(14, row));
            field.annotations_json = ValueToOptString(result->GetValue(15, row));
            fields.push_back(std::move(field));
        }
        return Result<std::vector<CatalogField>, Error>::Ok(std::move(fields));
    } catch (const std::exception& ex) {
        return Result<std::vector<CatalogField>, Error>::Err(MakeStoreError("GetFields", ex.what()));
    }
}

Result<std::vector<CatalogSearchHit>, Error> DuckDbCatalogStore::SearchFts(
    const std::string& query, int max_results) {
    auto page = SearchFtsPage(query, SearchOptions{max_results, 0, std::nullopt, std::nullopt,
                                                    std::nullopt, false});
    if (page.IsErr()) return Result<std::vector<CatalogSearchHit>, Error>::Err(page.Error());
    return Result<std::vector<CatalogSearchHit>, Error>::Ok(std::move(page.Value().hits));
}

Result<ICatalogStore::SearchPage, Error> DuckDbCatalogStore::SearchFtsPage(
    const std::string& query, const SearchOptions& options) {
    try {
        // Empty (or literal "*") query means "browse, don't search" — the
        // caller wants a stable listing to populate a discovery/browse view
        // with no text typed yet, not a relevance-ranked match. Routing this
        // through match_bm25 doesn't work: the FTS tokenizer/stemmer has no
        // wildcard concept, so an empty or "*" query string simply matches
        // nothing. Use a plain listing instead of fighting the tokenizer.
        const bool is_browse_all = query.empty() || query == "*";

        // Request one extra row so has_more can be computed without a
        // second COUNT query.
        const int fetch_limit = options.max_results + 1;

        std::ostringstream sql;
        duckdb::vector<duckdb::Value> params;
        if (is_browse_all) {
            sql << "SELECT e.id, e.system_sid, e.domain, e.object_type, e.object_subtype, "
                   "e.technical_name, e.display_name, e.package_or_infoarea, e.extracted_at, "
                   "e.changed_at, 1.0 AS score FROM entities e WHERE 1=1";
        } else {
            sql << "SELECT e.id, e.system_sid, e.domain, e.object_type, e.object_subtype, "
                   "e.technical_name, e.display_name, e.package_or_infoarea, e.extracted_at, "
                   "e.changed_at, fts_main_search_docs.match_bm25(e.id, $"
                << (params.size() + 1) << ") AS score FROM entities e";
            params.push_back(duckdb::Value(query));
            sql << " WHERE score IS NOT NULL";
        }
        if (options.domain.has_value()) {
            sql << " AND e.domain = $" << (params.size() + 1);
            params.push_back(duckdb::Value(*options.domain));
        }
        if (options.object_type.has_value()) {
            sql << " AND e.object_type = $" << (params.size() + 1);
            params.push_back(duckdb::Value(*options.object_type));
        }
        if (options.object_subtype.has_value()) {
            sql << " AND e.object_subtype = $" << (params.size() + 1);
            params.push_back(duckdb::Value(*options.object_subtype));
        }
        if (options.curated_only) {
            sql << " AND e.biz_definition IS NOT NULL";
        }
        sql << (is_browse_all ? " ORDER BY e.technical_name" : " ORDER BY score DESC");
        sql << " LIMIT $" << (params.size() + 1) << " OFFSET $" << (params.size() + 2);
        params.push_back(duckdb::Value::INTEGER(fetch_limit));
        params.push_back(duckdb::Value::INTEGER(options.offset));

        auto stmt = impl_->con->Prepare(sql.str());
        if (stmt->HasError()) {
            // FTS index not built (e.g. extension unavailable offline) —
            // degrade to no results rather than a hard error.
            return Result<SearchPage, Error>::Ok(SearchPage{});
        }
        auto result = ExecuteMaterializedParams(*stmt, std::move(params));
        if (result->HasError()) {
            return Result<SearchPage, Error>::Ok(SearchPage{});
        }

        std::vector<CatalogSearchHit> hits;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            auto domain_result =
                CatalogDomainFromString(ValueToStringOrEmpty(result->GetValue(2, row)));
            if (domain_result.IsErr()) continue;

            CatalogEntity entity(EntityId::Create(ValueToStringOrEmpty(result->GetValue(0, row))).Value());
            entity.system_sid = ValueToStringOrEmpty(result->GetValue(1, row));
            entity.domain = domain_result.Value();
            entity.object_type = ValueToStringOrEmpty(result->GetValue(3, row));
            entity.object_subtype = ValueToOptString(result->GetValue(4, row));
            entity.technical_name = ValueToStringOrEmpty(result->GetValue(5, row));
            entity.display_name = ValueToStringOrEmpty(result->GetValue(6, row));
            entity.package_or_infoarea = ValueToOptString(result->GetValue(7, row));
            entity.extracted_at = ValueToStringOrEmpty(result->GetValue(8, row));
            entity.changed_at = ValueToOptString(result->GetValue(9, row));

            double score = result->GetValue(10, row).IsNull()
                               ? 0.0
                               : result->GetValue(10, row).GetValue<double>();
            hits.push_back(CatalogSearchHit{std::move(entity), score});
        }

        SearchPage page;
        page.has_more = static_cast<int>(hits.size()) > options.max_results;
        if (page.has_more) {
            hits.erase(hits.begin() + options.max_results, hits.end());
        }
        page.hits = std::move(hits);
        return Result<SearchPage, Error>::Ok(std::move(page));
    } catch (const std::exception&) {
        return Result<SearchPage, Error>::Ok(SearchPage{});
    }
}

Result<void, Error> DuckDbCatalogStore::WriteEmbedding(const EntityId& entity_id,
                                                        const std::vector<float>& embedding,
                                                        const std::string& model) {
    if (embedding.size() != static_cast<size_t>(kEmbeddingDimensions)) {
        return Result<void, Error>::Err(MakeStoreError(
            "WriteEmbedding", "embedding must have " + std::to_string(kEmbeddingDimensions) +
                                   " dimensions, got " + std::to_string(embedding.size())));
    }
    try {
        // Explicit delete-then-insert rather than INSERT OR REPLACE — the
        // HNSW index does not reliably support upsert-style writes on the
        // same statement; a plain DELETE + INSERT is the supported update
        // path for an HNSW-indexed column.
        auto delete_stmt = impl_->con->Prepare("DELETE FROM entity_embeddings WHERE entity_id = $1");
        if (delete_stmt->HasError()) {
            return Result<void, Error>::Err(MakeStoreError(
                "WriteEmbedding",
                "entity_embeddings table unavailable (vss extension not loaded): " +
                    delete_stmt->GetError()));
        }
        auto delete_result = ExecuteMaterialized(*delete_stmt, entity_id.Value());
        if (delete_result->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("WriteEmbedding", delete_result->GetError()));
        }

        auto insert_stmt = impl_->con->Prepare("INSERT INTO entity_embeddings VALUES ($1, $2, $3)");
        if (insert_stmt->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("WriteEmbedding", insert_stmt->GetError()));
        }
        duckdb::vector<duckdb::Value> params;
        params.push_back(duckdb::Value(entity_id.Value()));
        params.push_back(duckdb::Value(model));
        params.push_back(MakeArrayValue(embedding));

        auto result = ExecuteMaterializedParams(*insert_stmt, std::move(params));
        if (result->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("WriteEmbedding", result->GetError()));
        }
    } catch (const std::exception& ex) {
        return Result<void, Error>::Err(MakeStoreError("WriteEmbedding", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<std::vector<CatalogSearchHit>, Error> DuckDbCatalogStore::SearchVss(
    const std::vector<float>& query_embedding, int max_results) {
    try {
        auto stmt = impl_->con->Prepare(
            "SELECT e.id, e.system_sid, e.domain, e.object_type, e.object_subtype, "
            "e.technical_name, e.display_name, e.package_or_infoarea, e.extracted_at, "
            "e.changed_at, "
            "1.0 - array_cosine_distance(v.embedding, $1) AS similarity "
            "FROM entity_embeddings v JOIN entities e ON e.id = v.entity_id "
            "ORDER BY similarity DESC LIMIT $2");
        if (stmt->HasError()) {
            // entity_embeddings unavailable (vss extension not loaded, or
            // nothing embedded yet) — degrade to no results.
            return Result<std::vector<CatalogSearchHit>, Error>::Ok({});
        }

        duckdb::vector<duckdb::Value> params;
        params.push_back(MakeArrayValue(query_embedding));
        params.push_back(duckdb::Value::INTEGER(max_results));
        auto result = ExecuteMaterializedParams(*stmt, std::move(params));
        if (result->HasError()) {
            return Result<std::vector<CatalogSearchHit>, Error>::Ok({});
        }

        std::vector<CatalogSearchHit> hits;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            std::optional<CatalogDomain> domain;
            auto entity = RowToEntity(*result, row, domain);
            if (!domain.has_value()) continue;

            double score = result->GetValue(10, row).IsNull()
                               ? 0.0
                               : result->GetValue(10, row).GetValue<double>();
            hits.push_back(CatalogSearchHit{std::move(entity), score});
        }
        return Result<std::vector<CatalogSearchHit>, Error>::Ok(std::move(hits));
    } catch (const std::exception&) {
        return Result<std::vector<CatalogSearchHit>, Error>::Ok({});
    }
}

Result<std::vector<CatalogSearchHit>, Error> DuckDbCatalogStore::SearchHybrid(
    const std::string& query_text, const std::vector<float>& query_embedding, int max_results) {
    auto fts_result = SearchFts(query_text, max_results * 3);
    if (fts_result.IsErr()) return fts_result;
    auto vss_result = SearchVss(query_embedding, max_results * 3);
    if (vss_result.IsErr()) return vss_result;

    // Reciprocal-rank fusion, k=60 (standard RRF constant) — same
    // tie-breaking rule every call (NFR-4: deterministic ranking).
    constexpr double kRrfK = 60.0;
    std::map<std::string, double> combined_score;
    std::map<std::string, CatalogEntity> entities_by_id;

    auto accumulate = [&](const std::vector<CatalogSearchHit>& hits) {
        int rank = 0;
        for (const auto& hit : hits) {
            ++rank;
            combined_score[hit.entity.id.Value()] += 1.0 / (kRrfK + rank);
            entities_by_id.emplace(hit.entity.id.Value(), hit.entity);
        }
    };
    accumulate(fts_result.Value());
    accumulate(vss_result.Value());

    std::vector<CatalogSearchHit> hits;
    hits.reserve(combined_score.size());
    for (auto& [id, score] : combined_score) {
        hits.push_back(CatalogSearchHit{entities_by_id.at(id), score});
    }
    std::sort(hits.begin(), hits.end(), [](const CatalogSearchHit& a, const CatalogSearchHit& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.entity.id.Value() < b.entity.id.Value();
    });
    if (static_cast<int>(hits.size()) > max_results) {
        hits.erase(hits.begin() + max_results, hits.end());
    }

    return Result<std::vector<CatalogSearchHit>, Error>::Ok(std::move(hits));
}

namespace {

Result<std::vector<CatalogEdge>, Error> QueryEdges(duckdb::Connection& con, const char* sql,
                                                     const std::string& id, int max_results) {
    try {
        auto stmt = con.Prepare(sql);
        if (stmt->HasError()) {
            return Result<std::vector<CatalogEdge>, Error>::Err(
                MakeStoreError("GetEdges", stmt->GetError()));
        }
        auto result = ExecuteMaterialized(*stmt, id, max_results);
        if (result->HasError()) {
            return Result<std::vector<CatalogEdge>, Error>::Err(
                MakeStoreError("GetEdges", result->GetError()));
        }

        std::vector<CatalogEdge> edges;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            CatalogEdge edge(
                EntityId::Create(ValueToStringOrEmpty(result->GetValue(1, row))).Value(),
                EntityId::Create(ValueToStringOrEmpty(result->GetValue(2, row))).Value());
            edge.id = ValueToStringOrEmpty(result->GetValue(0, row));
            edge.kind = ValueToStringOrEmpty(result->GetValue(3, row));
            edge.field_mapping_json = ValueToStringOrEmpty(result->GetValue(4, row));
            edge.resolution = ValueToStringOrEmpty(result->GetValue(5, row));
            edge.extracted_at = ValueToStringOrEmpty(result->GetValue(6, row));
            edge.detail_json = ValueToOptString(result->GetValue(7, row));
            edges.push_back(std::move(edge));
        }
        return Result<std::vector<CatalogEdge>, Error>::Ok(std::move(edges));
    } catch (const std::exception& ex) {
        return Result<std::vector<CatalogEdge>, Error>::Err(MakeStoreError("GetEdges", ex.what()));
    }
}

} // anonymous namespace

Result<std::vector<CatalogEdge>, Error> DuckDbCatalogStore::GetEdgesTo(const EntityId& id,
                                                                        int max_results) {
    return QueryEdges(*impl_->con,
                       "SELECT id, from_id, to_id, kind, field_mapping, resolution, extracted_at, "
                       "detail "
                       "FROM edges WHERE to_id = $1 LIMIT $2",
                       id.Value(), max_results);
}

Result<std::vector<CatalogEdge>, Error> DuckDbCatalogStore::GetEdgesFrom(const EntityId& id,
                                                                          int max_results) {
    return QueryEdges(*impl_->con,
                       "SELECT id, from_id, to_id, kind, field_mapping, resolution, extracted_at, "
                       "detail "
                       "FROM edges WHERE from_id = $1 LIMIT $2",
                       id.Value(), max_results);
}

Result<std::vector<CatalogSyncRunSummary>, Error> DuckDbCatalogStore::RecentSyncRuns(
    int max_results) {
    try {
        auto stmt = impl_->con->Prepare(
            "SELECT id, started_at, finished_at, mode, scope, added, changed, removed, status "
            "FROM sync_runs ORDER BY started_at DESC LIMIT $1");
        if (stmt->HasError()) {
            return Result<std::vector<CatalogSyncRunSummary>, Error>::Err(
                MakeStoreError("RecentSyncRuns", stmt->GetError()));
        }
        auto result = ExecuteMaterialized(*stmt, max_results);
        if (result->HasError()) {
            return Result<std::vector<CatalogSyncRunSummary>, Error>::Err(
                MakeStoreError("RecentSyncRuns", result->GetError()));
        }

        std::vector<CatalogSyncRunSummary> runs;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            CatalogSyncRunSummary run;
            run.id = ValueToStringOrEmpty(result->GetValue(0, row));
            run.started_at = ValueToStringOrEmpty(result->GetValue(1, row));
            run.finished_at = ValueToStringOrEmpty(result->GetValue(2, row));
            run.mode = ValueToStringOrEmpty(result->GetValue(3, row));
            run.scope = ValueToStringOrEmpty(result->GetValue(4, row));
            run.added = ValueToOptInt(result->GetValue(5, row)).value_or(0);
            run.changed = ValueToOptInt(result->GetValue(6, row)).value_or(0);
            run.removed = ValueToOptInt(result->GetValue(7, row)).value_or(0);
            run.status = ValueToStringOrEmpty(result->GetValue(8, row));
            runs.push_back(std::move(run));
        }
        return Result<std::vector<CatalogSyncRunSummary>, Error>::Ok(std::move(runs));
    } catch (const std::exception& ex) {
        return Result<std::vector<CatalogSyncRunSummary>, Error>::Err(
            MakeStoreError("RecentSyncRuns", ex.what()));
    }
}

Result<DuckDbCatalogStore::CatalogStats, Error> DuckDbCatalogStore::Stats() {
    try {
        CatalogStats stats;
        auto entities = impl_->con->Query("SELECT COUNT(*) FROM entities");
        if (!entities->HasError() && entities->RowCount() > 0) {
            stats.entity_count = entities->GetValue<int64_t>(0, 0);
        }
        auto fields = impl_->con->Query("SELECT COUNT(*) FROM fields");
        if (!fields->HasError() && fields->RowCount() > 0) {
            stats.field_count = fields->GetValue<int64_t>(0, 0);
        }
        auto edges = impl_->con->Query("SELECT COUNT(*) FROM edges");
        if (!edges->HasError() && edges->RowCount() > 0) {
            stats.edge_count = edges->GetValue<int64_t>(0, 0);
        }
        auto unresolved =
            impl_->con->Query("SELECT COUNT(*) FROM edges WHERE resolution != 'resolved'");
        if (!unresolved->HasError() && unresolved->RowCount() > 0) {
            stats.unresolved_edge_count = unresolved->GetValue<int64_t>(0, 0);
        }
        auto curated =
            impl_->con->Query("SELECT COUNT(*) FROM entities WHERE biz_definition IS NOT NULL");
        if (!curated->HasError() && curated->RowCount() > 0) {
            stats.curated_entity_count = curated->GetValue<int64_t>(0, 0);
        }
        return Result<CatalogStats, Error>::Ok(stats);
    } catch (const std::exception& ex) {
        return Result<CatalogStats, Error>::Err(MakeStoreError("Stats", ex.what()));
    }
}

Result<std::vector<DuckDbCatalogStore::ObjectTypeCount>, Error>
DuckDbCatalogStore::ListObjectTypeCounts(const std::string& query) {
    try {
        // Empty/"*" means "browse all", matching SearchFtsPage's own
        // convention — narrowing counts to the same FTS match a
        // SearchFtsPage call with this query would return, so the
        // Discover UI's chip counts track whatever the user has typed
        // instead of always reflecting the whole catalog.
        const bool is_browse_all = query.empty() || query == "*";
        std::ostringstream sql;
        if (is_browse_all) {
            sql << "SELECT domain, object_type, COUNT(*) AS cnt FROM entities e";
        } else {
            sql << "SELECT domain, object_type, COUNT(*) AS cnt FROM entities e "
                   "WHERE fts_main_search_docs.match_bm25(e.id, $1) IS NOT NULL";
        }
        sql << " GROUP BY domain, object_type ORDER BY domain, object_type";

        auto stmt = impl_->con->Prepare(sql.str());
        if (stmt->HasError()) {
            // FTS index not built (e.g. extension unavailable offline) —
            // degrade to no counts rather than a hard error, matching
            // SearchFtsPage's own tolerance.
            return Result<std::vector<ObjectTypeCount>, Error>::Ok({});
        }
        auto result = is_browse_all ? ExecuteMaterialized(*stmt)
                                     : ExecuteMaterialized(*stmt, query);
        if (result->HasError()) {
            return Result<std::vector<ObjectTypeCount>, Error>::Err(
                MakeStoreError("ListObjectTypeCounts", result->GetError()));
        }

        std::vector<ObjectTypeCount> counts;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            ObjectTypeCount c;
            c.domain = ValueToStringOrEmpty(result->GetValue(0, row));
            c.object_type = ValueToStringOrEmpty(result->GetValue(1, row));
            c.count = result->GetValue(2, row).IsNull() ? 0 : result->GetValue<int64_t>(2, row);
            counts.push_back(std::move(c));
        }
        return Result<std::vector<ObjectTypeCount>, Error>::Ok(std::move(counts));
    } catch (const std::exception& ex) {
        return Result<std::vector<ObjectTypeCount>, Error>::Err(
            MakeStoreError("ListObjectTypeCounts", ex.what()));
    }
}

Result<std::vector<DuckDbCatalogStore::ObjectSubtypeCount>, Error>
DuckDbCatalogStore::ListObjectSubtypeCounts(const std::string& query) {
    try {
        const bool is_browse_all = query.empty() || query == "*";
        std::ostringstream sql;
        if (is_browse_all) {
            sql << "SELECT domain, object_type, object_subtype, COUNT(*) AS cnt FROM entities e "
                   "WHERE object_subtype IS NOT NULL";
        } else {
            sql << "SELECT domain, object_type, object_subtype, COUNT(*) AS cnt FROM entities e "
                   "WHERE object_subtype IS NOT NULL "
                   "AND fts_main_search_docs.match_bm25(e.id, $1) IS NOT NULL";
        }
        sql << " GROUP BY domain, object_type, object_subtype "
               "ORDER BY domain, object_type, object_subtype";

        auto stmt = impl_->con->Prepare(sql.str());
        if (stmt->HasError()) {
            return Result<std::vector<ObjectSubtypeCount>, Error>::Ok({});
        }
        auto result = is_browse_all ? ExecuteMaterialized(*stmt)
                                     : ExecuteMaterialized(*stmt, query);
        if (result->HasError()) {
            return Result<std::vector<ObjectSubtypeCount>, Error>::Err(
                MakeStoreError("ListObjectSubtypeCounts", result->GetError()));
        }

        std::vector<ObjectSubtypeCount> counts;
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            ObjectSubtypeCount c;
            c.domain = ValueToStringOrEmpty(result->GetValue(0, row));
            c.object_type = ValueToStringOrEmpty(result->GetValue(1, row));
            c.object_subtype = ValueToStringOrEmpty(result->GetValue(2, row));
            c.count = result->GetValue(3, row).IsNull() ? 0 : result->GetValue<int64_t>(3, row);
            counts.push_back(std::move(c));
        }
        return Result<std::vector<ObjectSubtypeCount>, Error>::Ok(std::move(counts));
    } catch (const std::exception& ex) {
        return Result<std::vector<ObjectSubtypeCount>, Error>::Err(
            MakeStoreError("ListObjectSubtypeCounts", ex.what()));
    }
}

Result<void, Error> DuckDbCatalogStore::ApplyOverlay(const EntityId& id,
                                                       const OverlayFields& fields,
                                                       const std::string& curated_by) {
    try {
        std::vector<std::string> set_clauses;
        duckdb::vector<duckdb::Value> params;
        int param_index = 1;

        auto add_field = [&](const char* column, const std::optional<std::string>& value) {
            if (!value.has_value()) return;
            set_clauses.push_back(std::string(column) + " = $" + std::to_string(param_index++));
            params.push_back(duckdb::Value(*value));
        };
        add_field("biz_definition", fields.definition);
        add_field("biz_owner", fields.owner);
        add_field("biz_lob", fields.lob);
        add_field("biz_confidentiality", fields.confidentiality);

        if (set_clauses.empty()) {
            return Result<void, Error>::Ok();  // nothing to change
        }
        set_clauses.push_back("biz_curated_by = $" + std::to_string(param_index++));
        params.push_back(duckdb::Value(curated_by));
        set_clauses.push_back("biz_curated_at = now()");

        std::ostringstream sql;
        sql << "UPDATE entities SET ";
        for (size_t i = 0; i < set_clauses.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << set_clauses[i];
        }
        sql << " WHERE id = $" << param_index;
        params.push_back(duckdb::Value(id.Value()));

        auto stmt = impl_->con->Prepare(sql.str());
        if (stmt->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("ApplyOverlay", stmt->GetError()));
        }
        auto result = ExecuteMaterializedParams(*stmt, std::move(params));
        if (result->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("ApplyOverlay", result->GetError()));
        }
    } catch (const std::exception& ex) {
        return Result<void, Error>::Err(MakeStoreError("ApplyOverlay", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<std::vector<std::string>, Error> DuckDbCatalogStore::ListEntityIds(
    const std::vector<std::string>& package_or_infoarea_filter) {
    try {
        std::unique_ptr<duckdb::MaterializedQueryResult> result;
        if (package_or_infoarea_filter.empty()) {
            result = impl_->con->Query("SELECT id FROM entities");
        } else {
            std::ostringstream sql;
            sql << "SELECT id FROM entities WHERE package_or_infoarea IN (";
            duckdb::vector<duckdb::Value> params;
            for (size_t i = 0; i < package_or_infoarea_filter.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << "$" << (i + 1);
                params.push_back(duckdb::Value(package_or_infoarea_filter[i]));
            }
            sql << ")";
            auto stmt = impl_->con->Prepare(sql.str());
            if (stmt->HasError()) {
                return Result<std::vector<std::string>, Error>::Err(
                    MakeStoreError("ListEntityIds", stmt->GetError()));
            }
            result = ExecuteMaterializedParams(*stmt, std::move(params));
        }
        if (result->HasError()) {
            return Result<std::vector<std::string>, Error>::Err(
                MakeStoreError("ListEntityIds", result->GetError()));
        }
        std::vector<std::string> ids;
        ids.reserve(result->RowCount());
        for (idx_t row = 0; row < result->RowCount(); ++row) {
            ids.push_back(ValueToStringOrEmpty(result->GetValue(0, row)));
        }
        return Result<std::vector<std::string>, Error>::Ok(std::move(ids));
    } catch (const std::exception& ex) {
        return Result<std::vector<std::string>, Error>::Err(MakeStoreError("ListEntityIds", ex.what()));
    }
}

Result<void, Error> DuckDbCatalogStore::UpsertEntitiesAndFields(
    const std::vector<CatalogEntity>& entities, const std::vector<CatalogField>& fields) {
    auto& con = *impl_->con;
    try {
        // A freshly extracted CatalogEntity from CatalogBuild never carries
        // biz_* — those come only from catalog annotate. Re-deriving an
        // entity via `catalog sync` must not silently wipe curation that's
        // already in the store, so carry forward any existing biz_* fields
        // the incoming entity doesn't set itself.
        std::vector<CatalogEntity> merged_entities;
        merged_entities.reserve(entities.size());
        for (const auto& e : entities) {
            CatalogEntity merged = e;
            auto existing = GetEntity(e.id);
            if (existing.IsOk() && existing.Value().has_value()) {
                const auto& prior = *existing.Value();
                if (!merged.biz_definition.has_value()) merged.biz_definition = prior.biz_definition;
                if (!merged.biz_owner.has_value()) merged.biz_owner = prior.biz_owner;
                if (!merged.biz_lob.has_value()) merged.biz_lob = prior.biz_lob;
                if (!merged.biz_confidentiality.has_value()) {
                    merged.biz_confidentiality = prior.biz_confidentiality;
                }
                if (!merged.biz_curated_by.has_value()) merged.biz_curated_by = prior.biz_curated_by;
                if (!merged.biz_curated_at.has_value()) merged.biz_curated_at = prior.biz_curated_at;
            }
            merged_entities.push_back(std::move(merged));
        }

        con.BeginTransaction();

        auto delete_entity_stmt = con.Prepare("DELETE FROM entities WHERE id = $1");
        auto delete_fields_stmt = con.Prepare("DELETE FROM fields WHERE entity_id = $1");
        for (const auto& e : merged_entities) {
            ExecuteMaterialized(*delete_entity_stmt, e.id.Value());
            ExecuteMaterialized(*delete_fields_stmt, e.id.Value());
        }

        {
            duckdb::Appender app(con, "entities");
            for (const auto& e : merged_entities) {
                app.BeginRow();
                app.Append<const char*>(e.id.Value().c_str());
                app.Append<const char*>(e.system_sid.c_str());
                app.Append<const char*>(ToString(e.domain).c_str());
                app.Append<const char*>(e.object_type.c_str());
                AppendOptString(app, e.object_subtype);
                app.Append<const char*>(e.technical_name.c_str());
                app.Append<const char*>(e.display_name.c_str());
                AppendOptString(app, e.package_or_infoarea);
                AppendOptString(app, e.created_by);
                AppendOptString(app, e.changed_by);
                AppendOptString(app, e.changed_at);
                AppendOptString(app, e.biz_definition);
                AppendOptString(app, e.biz_owner);
                AppendOptString(app, e.biz_lob);
                AppendOptString(app, e.biz_confidentiality);
                AppendOptString(app, e.biz_curated_by);
                AppendOptString(app, e.biz_curated_at);
                app.Append<const char*>(e.extracted_at.c_str());
                if (e.raw_json.empty()) {
                    app.Append<std::nullptr_t>(nullptr);
                } else {
                    app.Append<const char*>(e.raw_json.c_str());
                }
                AppendOptString(app, e.source_table);
                app.EndRow();
            }
            app.Close();
        }
        {
            duckdb::Appender app(con, "fields");
            for (const auto& f : fields) {
                app.BeginRow();
                app.Append<const char*>(f.id.c_str());
                app.Append<const char*>(f.entity_id.Value().c_str());
                app.Append<const char*>(f.name.c_str());
                AppendOptString(app, f.role);
                AppendOptString(app, f.description);
                AppendOptString(app, f.data_type);
                AppendOptInt(app, f.length);
                AppendOptInt(app, f.decimals);
                AppendOptString(app, f.aggregation);
                AppendOptString(app, f.unit);
                AppendOptString(app, f.formula);
                app.Append<bool>(f.is_key);
                AppendOptString(app, f.check_table);
                AppendOptString(app, f.fixed_values_json);
                AppendOptString(app, f.source_expression);
                AppendOptString(app, f.annotations_json);
                app.EndRow();
            }
            app.Close();
        }

        con.Query(
            "CREATE OR REPLACE TABLE search_docs AS "
            "SELECT id AS entity_id, "
            "concat_ws(' ', display_name, technical_name, object_type, "
            "biz_definition, biz_owner, biz_lob) AS text FROM entities");
        auto install = con.Query("INSTALL fts; LOAD fts;");
        if (!install->HasError()) {
            con.Query(
                "PRAGMA create_fts_index('search_docs', 'entity_id', 'text', "
                "stemmer='english', stopwords='english', overwrite=1)");
        }

        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("UpsertEntitiesAndFields", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::DeleteEntities(const std::vector<EntityId>& ids) {
    auto& con = *impl_->con;
    try {
        con.BeginTransaction();
        auto delete_entity_stmt = con.Prepare("DELETE FROM entities WHERE id = $1");
        auto delete_fields_stmt = con.Prepare("DELETE FROM fields WHERE entity_id = $1");
        for (const auto& id : ids) {
            ExecuteMaterialized(*delete_entity_stmt, id.Value());
            ExecuteMaterialized(*delete_fields_stmt, id.Value());
        }
        con.Query(
            "CREATE OR REPLACE TABLE search_docs AS "
            "SELECT id AS entity_id, "
            "concat_ws(' ', display_name, technical_name, object_type, "
            "biz_definition, biz_owner, biz_lob) AS text FROM entities");
        auto install = con.Query("INSTALL fts; LOAD fts;");
        if (!install->HasError()) {
            con.Query(
                "PRAGMA create_fts_index('search_docs', 'entity_id', 'text', "
                "stemmer='english', stopwords='english', overwrite=1)");
        }
        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("DeleteEntities", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::UpsertEdges(const std::vector<CatalogEdge>& edges) {
    auto& con = *impl_->con;
    try {
        con.BeginTransaction();

        auto delete_edge_stmt = con.Prepare("DELETE FROM edges WHERE id = $1");
        for (const auto& e : edges) {
            ExecuteMaterialized(*delete_edge_stmt, e.id);
        }

        {
            duckdb::Appender app(con, "edges");
            for (const auto& e : edges) {
                app.BeginRow();
                app.Append<const char*>(e.id.c_str());
                app.Append<const char*>(e.from_id.Value().c_str());
                app.Append<const char*>(e.to_id.Value().c_str());
                app.Append<const char*>(e.kind.c_str());
                if (e.field_mapping_json.empty()) {
                    app.Append<std::nullptr_t>(nullptr);
                } else {
                    app.Append<const char*>(e.field_mapping_json.c_str());
                }
                app.Append<const char*>(e.resolution.c_str());
                app.Append<const char*>(e.extracted_at.c_str());
                AppendOptString(app, e.detail_json);
                app.EndRow();
            }
            app.Close();
        }

        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("UpsertEdges", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::RecordSyncRun(const CatalogSyncRunSummary& run) {
    try {
        auto stmt = impl_->con->Prepare(
            "INSERT INTO sync_runs VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)");
        if (stmt->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("RecordSyncRun", stmt->GetError()));
        }
        duckdb::vector<duckdb::Value> params;
        params.push_back(duckdb::Value(run.id));
        params.push_back(duckdb::Value::TIMESTAMP(duckdb::Timestamp::FromString(run.started_at, false)));
        params.push_back(duckdb::Value::TIMESTAMP(duckdb::Timestamp::FromString(run.finished_at, false)));
        params.push_back(duckdb::Value(run.mode));
        params.push_back(duckdb::Value(run.scope));
        params.push_back(duckdb::Value::INTEGER(run.added));
        params.push_back(duckdb::Value::INTEGER(run.changed));
        params.push_back(duckdb::Value::INTEGER(run.removed));
        params.push_back(duckdb::Value(run.status));
        params.push_back(duckdb::Value(std::string("{}")));  // manifest_json — reserved for later use

        auto result = ExecuteMaterializedParams(*stmt, std::move(params));
        if (result->HasError()) {
            return Result<void, Error>::Err(MakeStoreError("RecordSyncRun", result->GetError()));
        }
    } catch (const std::exception& ex) {
        return Result<void, Error>::Err(MakeStoreError("RecordSyncRun", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::ResetSyncCheckpoint(
    const std::string& sid, const std::vector<std::string>& packages,
    const std::vector<std::string>& infoareas) {
    auto& con = *impl_->con;
    try {
        con.BeginTransaction();
        con.Query("DELETE FROM sync_checkpoint");
        con.Query("DELETE FROM sync_checkpoint_items");

        auto stmt = con.Prepare(
            "INSERT INTO sync_checkpoint (sid, packages, infoareas, status, reason, started_at, "
            "updated_at) VALUES ($1, $2, $3, 'in_progress', NULL, current_timestamp, "
            "current_timestamp)");
        if (stmt->HasError()) {
            con.Rollback();
            return Result<void, Error>::Err(MakeStoreError("ResetSyncCheckpoint", stmt->GetError()));
        }
        ExecuteMaterialized(*stmt, sid, nlohmann::json(packages).dump(),
                            nlohmann::json(infoareas).dump());

        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("ResetSyncCheckpoint", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::RecordSyncCheckpointItem(
    const std::string& kind, const std::string& name, int entities, int fields) {
    auto& con = *impl_->con;
    try {
        con.BeginTransaction();
        auto delete_stmt =
            con.Prepare("DELETE FROM sync_checkpoint_items WHERE kind = $1 AND name = $2");
        ExecuteMaterialized(*delete_stmt, kind, name);

        auto insert_stmt =
            con.Prepare("INSERT INTO sync_checkpoint_items VALUES ($1, $2, $3, $4, current_timestamp)");
        ExecuteMaterialized(*insert_stmt, kind, name, entities, fields);

        con.Query("UPDATE sync_checkpoint SET updated_at = current_timestamp");
        con.Commit();
    } catch (const std::exception& ex) {
        con.Rollback();
        return Result<void, Error>::Err(MakeStoreError("RecordSyncCheckpointItem", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::MarkSyncCheckpointInterrupted(const std::string& reason) {
    try {
        auto stmt = impl_->con->Prepare(
            "UPDATE sync_checkpoint SET status = 'interrupted', reason = $1, "
            "updated_at = current_timestamp");
        if (stmt->HasError()) {
            return Result<void, Error>::Err(
                MakeStoreError("MarkSyncCheckpointInterrupted", stmt->GetError()));
        }
        auto result = ExecuteMaterialized(*stmt, reason);
        if (result->HasError()) {
            return Result<void, Error>::Err(
                MakeStoreError("MarkSyncCheckpointInterrupted", result->GetError()));
        }
    } catch (const std::exception& ex) {
        return Result<void, Error>::Err(MakeStoreError("MarkSyncCheckpointInterrupted", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<void, Error> DuckDbCatalogStore::MarkSyncCheckpointCompleted() {
    try {
        auto result = impl_->con->Query(
            "UPDATE sync_checkpoint SET status = 'completed', reason = NULL, "
            "updated_at = current_timestamp");
        if (result->HasError()) {
            return Result<void, Error>::Err(
                MakeStoreError("MarkSyncCheckpointCompleted", result->GetError()));
        }
    } catch (const std::exception& ex) {
        return Result<void, Error>::Err(MakeStoreError("MarkSyncCheckpointCompleted", ex.what()));
    }
    return Result<void, Error>::Ok();
}

Result<SyncCheckpointState, Error> DuckDbCatalogStore::LoadSyncCheckpoint() {
    try {
        SyncCheckpointState state;
        auto row =
            impl_->con->Query("SELECT sid, packages, infoareas, status FROM sync_checkpoint LIMIT 1");
        if (row->HasError()) {
            return Result<SyncCheckpointState, Error>::Err(
                MakeStoreError("LoadSyncCheckpoint", row->GetError()));
        }
        if (row->RowCount() == 0) {
            return Result<SyncCheckpointState, Error>::Ok(state);  // exists=false
        }

        state.exists = true;
        state.sid = ValueToStringOrEmpty(row->GetValue(0, 0));
        try {
            state.requested_packages =
                nlohmann::json::parse(ValueToStringOrEmpty(row->GetValue(1, 0)))
                    .get<std::vector<std::string>>();
            state.requested_infoareas =
                nlohmann::json::parse(ValueToStringOrEmpty(row->GetValue(2, 0)))
                    .get<std::vector<std::string>>();
        } catch (const nlohmann::json::exception&) {
            // We write this JSON ourselves — a parse failure here would mean
            // file corruption, not a real input to validate against.
        }
        state.interrupted = (ValueToStringOrEmpty(row->GetValue(3, 0)) == "interrupted");

        auto items = impl_->con->Query("SELECT kind, name FROM sync_checkpoint_items");
        if (items->HasError()) {
            return Result<SyncCheckpointState, Error>::Err(
                MakeStoreError("LoadSyncCheckpoint", items->GetError()));
        }
        for (idx_t r = 0; r < items->RowCount(); ++r) {
            auto kind = ValueToStringOrEmpty(items->GetValue(0, r));
            auto name = ValueToStringOrEmpty(items->GetValue(1, r));
            if (kind == "infoarea") {
                state.completed_infoareas.insert(name);
            } else {
                state.completed_packages.insert(name);
            }
        }
        return Result<SyncCheckpointState, Error>::Ok(std::move(state));
    } catch (const std::exception& ex) {
        return Result<SyncCheckpointState, Error>::Err(MakeStoreError("LoadSyncCheckpoint", ex.what()));
    }
}

Result<int, Error> DuckDbCatalogStore::SchemaVersion() {
    try {
        auto result = impl_->con->Query(
            "SELECT version FROM schema_version ORDER BY applied_at DESC LIMIT 1");
        if (result->HasError() || result->RowCount() == 0) {
            return Result<int, Error>::Ok(kSchemaVersion);
        }
        return Result<int, Error>::Ok(result->GetValue<int32_t>(0, 0));
    } catch (const std::exception& ex) {
        return Result<int, Error>::Err(MakeStoreError("SchemaVersion", ex.what()));
    }
}

Result<int64_t, Error> DuckDbCatalogStore::EntityCount() {
    try {
        auto result = impl_->con->Query("SELECT COUNT(*) FROM entities");
        if (result->HasError()) {
            return Result<int64_t, Error>::Err(MakeStoreError("EntityCount", result->GetError()));
        }
        return Result<int64_t, Error>::Ok(result->GetValue<int64_t>(0, 0));
    } catch (const std::exception& ex) {
        return Result<int64_t, Error>::Err(MakeStoreError("EntityCount", ex.what()));
    }
}

} // namespace erpl_adt
