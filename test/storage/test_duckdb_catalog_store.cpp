#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/storage/duckdb_catalog_store.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace erpl_adt;

namespace {

// Portable setenv/unsetenv (MSVC has no POSIX setenv).
void SetEnvVar(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

void UnsetEnvVar(const char* key) {
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

// RAII: forces DuckDB's extension directory to an un-creatable path so any
// `INSTALL fts; LOAD fts;` fails deterministically, with no network — the
// path's parent is a regular file, so creating it as a directory yields
// ENOTDIR (fails even for root). This reproduces the Windows x64-windows-static
// condition (fts extension unavailable) on any platform. See issue #27.
struct BlockedExtensionDir {
    std::filesystem::path block_file;

    BlockedExtensionDir() {
        block_file = std::filesystem::temp_directory_path() / "erpl_adt_issue27_block_file";
        std::ofstream(block_file) << "not a directory";
        // Point at "<regular file>/sub" — can never be created as a directory.
        SetEnvVar("ERPL_ADT_DUCKDB_EXTENSION_DIR", (block_file / "sub").string());
    }

    ~BlockedExtensionDir() {
        UnsetEnvVar("ERPL_ADT_DUCKDB_EXTENSION_DIR");
        std::error_code ec;
        std::filesystem::remove(block_file, ec);
    }
};

CatalogFeed MakeSampleFeed() {
    CatalogFeed feed;
    feed.system_sid = "A4H";
    feed.built_at = "2026-07-19T10:00:00Z";

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CatalogEntity tabl(tabl_id);
    tabl.system_sid = "A4H";
    tabl.domain = CatalogDomain::Ddic;
    tabl.object_type = "TABL";
    tabl.technical_name = "SFLIGHT";
    tabl.display_name = "Flight schedule";
    tabl.package_or_infoarea = "STEST";
    tabl.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(tabl);

    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    CatalogEntity clas(clas_id);
    clas.system_sid = "A4H";
    clas.domain = CatalogDomain::Abap;
    clas.object_type = "CLAS";
    clas.technical_name = "ZCL_PROCUREMENT";
    clas.display_name = "Procurement value calculator";
    clas.extracted_at = "2026-07-19T10:00:00Z";
    clas.changed_at = "2026-07-18T09:00:00Z";
    feed.entities.push_back(clas);

    CatalogField f1(tabl_id);
    f1.id = tabl_id.Value() + "#CARRID";
    f1.name = "CARRID";
    f1.data_type = "S_CARR_ID";
    feed.fields.push_back(std::move(f1));

    CatalogField f2(tabl_id);
    f2.id = tabl_id.Value() + "#PRICE";
    f2.name = "PRICE";
    f2.description = "Ticket price";
    f2.data_type = "S_PRICE";
    f2.length = 15;
    f2.decimals = 2;
    feed.fields.push_back(std::move(f2));

    CatalogEdge edge(clas_id, tabl_id);
    edge.id = clas_id.Value() + "->" + tabl_id.Value();
    edge.kind = "uses";
    edge.extracted_at = "2026-07-19T10:00:00Z";
    feed.edges.push_back(std::move(edge));

    return feed;
}

// entity_embeddings uses a fixed-width FLOAT[768] column (matching the real
// Gemini text-embedding-004 output width) — toy test vectors must match that
// width even though only a couple of dimensions carry signal.
std::vector<float> MakeVec(std::initializer_list<std::pair<int, float>> sets) {
    std::vector<float> v(768, 0.0f);
    for (const auto& [idx, val] : sets) v[idx] = val;
    return v;
}

// Capability probes: whether DuckDB's fts / vss extensions actually loaded in
// this build and environment. On x64-windows-static (and on any offline build
// whose extension directory can't fetch the extension binary) INSTALL/LOAD
// fails, so match_bm25 full-text search and the entity_embeddings vector table
// don't exist — the store degrades to empty results by design (see
// RebuildFtsIndex / issue #27). Tests that assert search *hits* SKIP when the
// relevant extension is unavailable, rather than asserting results that can't
// exist. On Linux (and any host where the extension loads) these return true
// and the gated tests run in full — the gate is "did the extension load?",
// never "is this Windows?", so a networkless Linux build hits the same skip.
//
// Each probe opens a throwaway in-memory store so it observes the exact same
// INSTALL/LOAD path the real store uses, honoring ERPL_ADT_DUCKDB_EXTENSION_DIR
// (so BlockedExtensionDir also forces these to false).
bool FtsAvailable() {
    auto store_result = DuckDbCatalogStore::Open(":memory:");
    if (store_result.IsErr()) return false;
    auto store = std::move(store_result).Value();
    if (store->WriteFeed(MakeSampleFeed()).IsErr()) return false;
    // "procurement" matches ZCL_PROCUREMENT's display_name via match_bm25 when
    // fts loaded; degrades to an empty (Ok) page when it didn't.
    auto hits = store->SearchFts("procurement", 1);
    return hits.IsOk() && !hits.Value().empty();
}

bool VssAvailable() {
    auto store_result = DuckDbCatalogStore::Open(":memory:");
    if (store_result.IsErr()) return false;
    auto store = std::move(store_result).Value();
    if (store->WriteFeed(MakeSampleFeed()).IsErr()) return false;
    // WriteEmbedding fails with "entity_embeddings table unavailable" when the
    // vss extension didn't load (the table is only created when INSTALL vss
    // succeeds in Open()).
    auto probe_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    return store->WriteEmbedding(probe_id, MakeVec({{0, 1.0f}}), "probe-model").IsOk();
}

} // anonymous namespace

TEST_CASE("DuckDbCatalogStore: opens an in-memory database", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:");
    REQUIRE(store.IsOk());
    REQUIRE(store.Value() != nullptr);
}

TEST_CASE("DuckDbCatalogStore: WriteFeed then GetEntity round-trips an entity",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto feed = MakeSampleFeed();

    auto write_result = store->WriteFeed(feed);
    REQUIRE(write_result.IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto get_result = store->GetEntity(tabl_id);
    REQUIRE(get_result.IsOk());
    REQUIRE(get_result.Value().has_value());

    const auto& entity = *get_result.Value();
    CHECK(entity.technical_name == "SFLIGHT");
    CHECK(entity.domain == CatalogDomain::Ddic);
    CHECK(entity.display_name == "Flight schedule");
    REQUIRE(entity.package_or_infoarea.has_value());
    CHECK(*entity.package_or_infoarea == "STEST");
}

TEST_CASE("DuckDbCatalogStore: WriteFeed persists data even when the FTS index "
          "rebuild fails (regression #27)",
          "[storage][duckdb]") {
    // Regression for issue #27 (Windows x64-windows-static data loss): the
    // best-effort `INSTALL fts; LOAD fts;` used to run INSIDE WriteFeed's write
    // transaction. When it errors (extension unavailable / offline), DuckDB
    // aborts the transaction and the subsequent Commit() silently ROLLS BACK
    // without raising — WriteFeed returns Ok() but every row is discarded.
    // Here we force that fts failure deterministically; the data must survive.
    BlockedExtensionDir blocked_fts;

    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    auto write_result = store->WriteFeed(MakeSampleFeed());
    REQUIRE(write_result.IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto get_result = store->GetEntity(tabl_id);
    REQUIRE(get_result.IsOk());
    REQUIRE(get_result.Value().has_value());
    CHECK(get_result.Value()->technical_name == "SFLIGHT");
}

TEST_CASE("DuckDbCatalogStore: GetEntity returns nullopt for an unknown id",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto result = store->GetEntity(EntityId::Create("nonexistent").Value());
    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().has_value());
}

TEST_CASE("DuckDbCatalogStore: GetFields returns an entity's fields", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto feed = MakeSampleFeed();
    REQUIRE(store->WriteFeed(feed).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto result = store->GetFields(tabl_id);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);

    bool found_price = false;
    for (const auto& f : result.Value()) {
        if (f.name == "PRICE") {
            found_price = true;
            REQUIRE(f.length.has_value());
            CHECK(*f.length == 15);
            REQUIRE(f.description.has_value());
            CHECK(*f.description == "Ticket price");
        }
    }
    CHECK(found_price);
}

TEST_CASE("DuckDbCatalogStore: EntityCount reflects a written feed", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto feed = MakeSampleFeed();
    REQUIRE(store->WriteFeed(feed).IsOk());

    auto count = store->EntityCount();
    REQUIRE(count.IsOk());
    CHECK(count.Value() == 2);
}

TEST_CASE("DuckDbCatalogStore: ListObjectTypeCounts returns distinct (domain, object_type) "
          "with counts",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());  // 1 TABL (DDIC), 1 CLAS (ABAP)

    auto result = store->ListObjectTypeCounts();
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);

    bool found_tabl = false, found_clas = false;
    for (const auto& c : result.Value()) {
        if (c.object_type == "TABL") {
            found_tabl = true;
            CHECK(c.domain == "DDIC");
            CHECK(c.count == 1);
        }
        if (c.object_type == "CLAS") {
            found_clas = true;
            CHECK(c.domain == "ABAP");
            CHECK(c.count == 1);
        }
    }
    CHECK(found_tabl);
    CHECK(found_clas);
}

TEST_CASE("DuckDbCatalogStore: object_subtype round-trips through WriteFeed/GetEntity",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    CatalogFeed feed;
    feed.system_sid = "A4H";
    auto query_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    CatalogEntity query(query_id);
    query.system_sid = "A4H";
    query.domain = CatalogDomain::Bw;
    query.object_type = "ELEM";
    query.object_subtype = "REP";
    query.technical_name = "0BPC_BPF_ACTIVITY_REP";
    query.display_name = "BPC BPF Activity Report";
    query.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(query);

    REQUIRE(store->WriteFeed(feed).IsOk());

    auto get_result = store->GetEntity(query_id);
    REQUIRE(get_result.IsOk());
    REQUIRE(get_result.Value().has_value());
    REQUIRE(get_result.Value()->object_subtype.has_value());
    CHECK(*get_result.Value()->object_subtype == "REP");
}

TEST_CASE("DuckDbCatalogStore: SearchFtsPage filters by object_subtype",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    CatalogFeed feed;
    feed.system_sid = "A4H";
    auto query_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    CatalogEntity query(query_id);
    query.system_sid = "A4H";
    query.domain = CatalogDomain::Bw;
    query.object_type = "ELEM";
    query.object_subtype = "REP";
    query.technical_name = "0BPC_BPF_ACTIVITY_REP";
    query.display_name = "BPC BPF Activity Report";
    query.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(query);

    auto var_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0CMONTH");
    CatalogEntity var(var_id);
    var.system_sid = "A4H";
    var.domain = CatalogDomain::Bw;
    var.object_type = "ELEM";
    var.object_subtype = "VAR";
    var.technical_name = "0CMONTH";
    var.display_name = "Current Calendar Month";
    var.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(var);

    REQUIRE(store->WriteFeed(feed).IsOk());

    ICatalogStore::SearchOptions options;
    options.max_results = 10;
    options.object_subtype = "REP";
    auto result = store->SearchFtsPage("", options);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().hits.size() == 1);
    CHECK(result.Value().hits[0].entity.technical_name == "0BPC_BPF_ACTIVITY_REP");
}

TEST_CASE("DuckDbCatalogStore: ListObjectSubtypeCounts returns distinct "
          "(domain, object_type, object_subtype) with counts",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    CatalogFeed feed;
    feed.system_sid = "A4H";
    auto query_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    CatalogEntity query(query_id);
    query.system_sid = "A4H";
    query.domain = CatalogDomain::Bw;
    query.object_type = "ELEM";
    query.object_subtype = "REP";
    query.technical_name = "0BPC_BPF_ACTIVITY_REP";
    query.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(query);

    // A TABL entity has no subtype at all — must not show up as a spurious
    // (DDIC, TABL, "") row.
    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CatalogEntity tabl(tabl_id);
    tabl.system_sid = "A4H";
    tabl.domain = CatalogDomain::Ddic;
    tabl.object_type = "TABL";
    tabl.technical_name = "SFLIGHT";
    tabl.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(tabl);

    REQUIRE(store->WriteFeed(feed).IsOk());

    auto result = store->ListObjectSubtypeCounts();
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].domain == "BW");
    CHECK(result.Value()[0].object_type == "ELEM");
    CHECK(result.Value()[0].object_subtype == "REP");
    CHECK(result.Value()[0].count == 1);
}

TEST_CASE("DuckDbCatalogStore: ListObjectTypeCounts narrows to the current search "
          "query instead of always reflecting the whole catalog",
          "[storage][duckdb]") {
    if (!FtsAvailable()) SKIP("fts extension unavailable — query narrowing depends on match_bm25");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());  // 1 TABL (SFLIGHT), 1 CLAS (ZCL_PROCUREMENT)

    // Unfiltered: both object types present.
    auto all = store->ListObjectTypeCounts();
    REQUIRE(all.IsOk());
    REQUIRE(all.Value().size() == 2);

    // Narrowed to a query matching only the CLAS entity ("procurement",
    // present in ZCL_PROCUREMENT's display_name) — only CLAS should count.
    auto narrowed = store->ListObjectTypeCounts("procurement");
    REQUIRE(narrowed.IsOk());
    REQUIRE(narrowed.Value().size() == 1);
    CHECK(narrowed.Value()[0].object_type == "CLAS");
    CHECK(narrowed.Value()[0].count == 1);

    // A query matching nothing narrows counts to empty, not the whole catalog.
    auto none = store->ListObjectTypeCounts("nonexistent_zzz");
    REQUIRE(none.IsOk());
    CHECK(none.Value().empty());
}

TEST_CASE("DuckDbCatalogStore: ListObjectSubtypeCounts narrows to the current "
          "search query",
          "[storage][duckdb]") {
    if (!FtsAvailable()) SKIP("fts extension unavailable — query narrowing depends on match_bm25");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    CatalogFeed feed;
    feed.system_sid = "A4H";
    auto rep_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0BPC_BPF_ACTIVITY_REP");
    CatalogEntity rep(rep_id);
    rep.system_sid = "A4H";
    rep.domain = CatalogDomain::Bw;
    rep.object_type = "ELEM";
    rep.object_subtype = "REP";
    rep.technical_name = "0BPC_BPF_ACTIVITY_REP";
    rep.display_name = "BPC BPF Activity Report";
    rep.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(rep);

    auto var_id = DeriveEntityId("A4H", CatalogDomain::Bw, "ELEM", "0CMONTH");
    CatalogEntity var(var_id);
    var.system_sid = "A4H";
    var.domain = CatalogDomain::Bw;
    var.object_type = "ELEM";
    var.object_subtype = "VAR";
    var.technical_name = "0CMONTH";
    var.display_name = "Current Calendar Month";
    var.extracted_at = "2026-07-19T10:00:00Z";
    feed.entities.push_back(var);

    REQUIRE(store->WriteFeed(feed).IsOk());

    auto all = store->ListObjectSubtypeCounts();
    REQUIRE(all.IsOk());
    REQUIRE(all.Value().size() == 2);

    auto narrowed = store->ListObjectSubtypeCounts("activity");
    REQUIRE(narrowed.IsOk());
    REQUIRE(narrowed.Value().size() == 1);
    CHECK(narrowed.Value()[0].object_subtype == "REP");
}

TEST_CASE("DuckDbCatalogStore: WriteFeed replaces prior content (full rebuild)",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    CatalogFeed smaller;
    smaller.system_sid = "A4H";
    smaller.built_at = "2026-07-19T11:00:00Z";
    auto only_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CatalogEntity only(only_id);
    only.system_sid = "A4H";
    only.domain = CatalogDomain::Ddic;
    only.object_type = "TABL";
    only.technical_name = "SFLIGHT";
    only.display_name = "Flight schedule";
    only.extracted_at = "2026-07-19T11:00:00Z";
    smaller.entities.push_back(std::move(only));

    REQUIRE(store->WriteFeed(smaller).IsOk());

    auto count = store->EntityCount();
    REQUIRE(count.IsOk());
    CHECK(count.Value() == 1);
}

TEST_CASE("DuckDbCatalogStore: SearchFts finds a curated entity by full-text match",
          "[storage][duckdb]") {
    if (!FtsAvailable()) SKIP("fts extension unavailable — full-text match returns no hits");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFts("procurement", 10);
    REQUIRE(result.IsOk());
    REQUIRE_FALSE(result.Value().empty());
    CHECK(result.Value()[0].entity.technical_name == "ZCL_PROCUREMENT");
}

TEST_CASE("DuckDbCatalogStore: SearchFts carries changed_at through to hits",
          "[storage][duckdb]") {
    if (!FtsAvailable()) SKIP("fts extension unavailable — full-text match returns no hits");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFts("procurement", 10);
    REQUIRE(result.IsOk());
    REQUIRE_FALSE(result.Value().empty());
    REQUIRE(result.Value()[0].entity.changed_at.has_value());
    // DuckDB TIMESTAMP round-trips as "YYYY-MM-DD HH:MM:SS" (space, no
    // trailing Z), not the ISO-8601 string that was written in — matches
    // existing extracted_at behavior elsewhere in this store.
    CHECK(*result.Value()[0].entity.changed_at == "2026-07-18 09:00:00");
}

TEST_CASE("DuckDbCatalogStore: SearchFts with an empty query browses all entities",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFts("", 10);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);
    // Stable order: technical_name ascending.
    CHECK(result.Value()[0].entity.technical_name == "SFLIGHT");
    CHECK(result.Value()[1].entity.technical_name == "ZCL_PROCUREMENT");
}

TEST_CASE("DuckDbCatalogStore: SearchFts with \"*\" browses all entities",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFts("*", 10);
    REQUIRE(result.IsOk());
    CHECK(result.Value().size() == 2);
}

TEST_CASE("DuckDbCatalogStore: SearchFts browse-all respects max_results",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFts("", 1);
    REQUIRE(result.IsOk());
    CHECK(result.Value().size() == 1);
}

TEST_CASE("DuckDbCatalogStore: SearchFtsPage sets has_more at the page boundary",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());  // 2 entities total

    auto page1 = store->SearchFtsPage("", {1, 0, std::nullopt, std::nullopt, std::nullopt, false});
    REQUIRE(page1.IsOk());
    REQUIRE(page1.Value().hits.size() == 1);
    CHECK(page1.Value().has_more == true);
    CHECK(page1.Value().hits[0].entity.technical_name == "SFLIGHT");

    auto page2 = store->SearchFtsPage("", {1, 1, std::nullopt, std::nullopt, std::nullopt, false});
    REQUIRE(page2.IsOk());
    REQUIRE(page2.Value().hits.size() == 1);
    CHECK(page2.Value().has_more == false);
    CHECK(page2.Value().hits[0].entity.technical_name == "ZCL_PROCUREMENT");
}

TEST_CASE("DuckDbCatalogStore: SearchFtsPage filters by domain", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto result = store->SearchFtsPage("", {10, 0, std::string("ABAP"), std::nullopt, std::nullopt, false});
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().hits.size() == 1);
    CHECK(result.Value().hits[0].entity.technical_name == "ZCL_PROCUREMENT");
}

TEST_CASE("DuckDbCatalogStore: SearchFtsPage filters to curated-only entities",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    REQUIRE(store
                ->ApplyOverlay(tabl_id, {std::string("Flight schedule master data"), std::nullopt,
                                        std::nullopt, std::nullopt},
                               "test")
                .IsOk());

    auto result = store->SearchFtsPage("", {10, 0, std::nullopt, std::nullopt, std::nullopt, true});
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().hits.size() == 1);
    CHECK(result.Value().hits[0].entity.technical_name == "SFLIGHT");
}

TEST_CASE("DuckDbCatalogStore: WriteEmbedding then SearchVss ranks the nearest vector first",
          "[storage][duckdb][vss]") {
    if (!VssAvailable()) SKIP("vss extension unavailable — entity_embeddings table absent");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");

    // 4-d toy vectors: tabl_id near [1,0,0,0], clas_id near [0,1,0,0].
    REQUIRE(store->WriteEmbedding(tabl_id, MakeVec({{0, 1.0f}}), "test-model").IsOk());
    REQUIRE(store->WriteEmbedding(clas_id, MakeVec({{1, 1.0f}}), "test-model").IsOk());

    auto result = store->SearchVss(MakeVec({{0, 0.9f}, {1, 0.1f}}), 10);
    REQUIRE(result.IsOk());
    REQUIRE_FALSE(result.Value().empty());
    CHECK(result.Value()[0].entity.id.Value() == tabl_id.Value());
}

TEST_CASE("DuckDbCatalogStore: WriteEmbedding upserts (re-embedding replaces the prior vector)",
          "[storage][duckdb][vss]") {
    if (!VssAvailable()) SKIP("vss extension unavailable — entity_embeddings table absent");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    REQUIRE(store->WriteEmbedding(tabl_id, MakeVec({{0, 1.0f}}), "test-model").IsOk());
    REQUIRE(store->WriteEmbedding(tabl_id, MakeVec({{2, 1.0f}}), "test-model-v2").IsOk());

    auto result = store->SearchVss(MakeVec({{2, 1.0f}}), 10);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);  // not 2 — upsert, not append
    CHECK(result.Value()[0].entity.id.Value() == tabl_id.Value());
}

TEST_CASE("DuckDbCatalogStore: SearchHybrid returns results even when only one signal matches",
          "[storage][duckdb][vss]") {
    // Needs vss for the WriteEmbedding below; the fts signal is optional here
    // (the vss signal alone satisfies the "only one signal matches" assertion).
    if (!VssAvailable()) SKIP("vss extension unavailable — entity_embeddings table absent");
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    REQUIRE(store->WriteEmbedding(clas_id, MakeVec({{1, 1.0f}}), "test-model").IsOk());

    // FTS matches "procurement" (ZCL_PROCUREMENT's display_name); VSS query
    // vector also points at the same entity's embedding.
    auto result = store->SearchHybrid("procurement", MakeVec({{1, 1.0f}}), 10);
    REQUIRE(result.IsOk());
    REQUIRE_FALSE(result.Value().empty());
    CHECK(result.Value()[0].entity.id.Value() == clas_id.Value());
}

TEST_CASE("DuckDbCatalogStore: GetEdgesTo returns catalog-wide where-used",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");

    auto result = store->GetEdgesTo(tabl_id, 10);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].from_id.Value() == clas_id.Value());
    CHECK(result.Value()[0].kind == "uses");
}

TEST_CASE("DuckDbCatalogStore: GetEdgesFrom returns outgoing edges", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");

    auto result = store->GetEdgesFrom(clas_id, 10);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].to_id.Value() == tabl_id.Value());
}

TEST_CASE("DuckDbCatalogStore: Stats reflects a written feed", "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto stats = store->Stats();
    REQUIRE(stats.IsOk());
    CHECK(stats.Value().entity_count == 2);
    CHECK(stats.Value().field_count == 2);
    CHECK(stats.Value().edge_count == 1);
}

TEST_CASE("DuckDbCatalogStore: RecentSyncRuns is empty before any sync has run",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto runs = store->RecentSyncRuns(10);
    REQUIRE(runs.IsOk());
    CHECK(runs.Value().empty());
}

TEST_CASE("DuckDbCatalogStore: ListEntityIds returns every stored entity id",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto ids = store->ListEntityIds();
    REQUIRE(ids.IsOk());
    CHECK(ids.Value().size() == 2);
}

TEST_CASE("DuckDbCatalogStore: UpsertEntitiesAndFields adds a new entity without touching others",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto new_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "NEWTABLE");
    CatalogEntity new_entity(new_id);
    new_entity.system_sid = "A4H";
    new_entity.domain = CatalogDomain::Ddic;
    new_entity.object_type = "TABL";
    new_entity.technical_name = "NEWTABLE";
    new_entity.display_name = "New table";
    new_entity.extracted_at = "2026-07-19T12:00:00Z";

    auto result = store->UpsertEntitiesAndFields({new_entity}, {});
    REQUIRE(result.IsOk());

    auto count = store->EntityCount();
    REQUIRE(count.IsOk());
    CHECK(count.Value() == 3);  // 2 original + 1 new

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto original = store->GetEntity(tabl_id);
    REQUIRE(original.IsOk());
    CHECK(original.Value().has_value());  // untouched
}

TEST_CASE("DuckDbCatalogStore: UpsertEntitiesAndFields replaces an existing entity's fields",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CatalogEntity updated(tabl_id);
    updated.system_sid = "A4H";
    updated.domain = CatalogDomain::Ddic;
    updated.object_type = "TABL";
    updated.technical_name = "SFLIGHT";
    updated.display_name = "Flight schedule (renamed)";
    updated.extracted_at = "2026-07-19T12:00:00Z";

    CatalogField new_field(tabl_id);
    new_field.id = tabl_id.Value() + "#NEWCOL";
    new_field.name = "NEWCOL";

    auto result = store->UpsertEntitiesAndFields({updated}, {new_field});
    REQUIRE(result.IsOk());

    auto entity = store->GetEntity(tabl_id);
    REQUIRE(entity.IsOk());
    REQUIRE(entity.Value().has_value());
    CHECK(entity.Value()->display_name == "Flight schedule (renamed)");

    auto fields = store->GetFields(tabl_id);
    REQUIRE(fields.IsOk());
    REQUIRE(fields.Value().size() == 1);  // old fields (CARRID, PRICE) replaced
    CHECK(fields.Value()[0].name == "NEWCOL");
}

TEST_CASE("DuckDbCatalogStore: UpsertEntitiesAndFields preserves existing business-overlay "
          "curation (a re-derived entity must not wipe biz_* fields)",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    ICatalogStore::OverlayFields overlay;
    overlay.definition = "Flight schedule master data";
    overlay.owner = "steward@example.com";
    REQUIRE(store->ApplyOverlay(tabl_id, overlay, "steward@example.com").IsOk());

    // Simulate `catalog sync` re-deriving SFLIGHT fresh from SAP — a freshly
    // built CatalogEntity never carries biz_* (only `catalog annotate` sets
    // those), so this mirrors exactly what CatalogSync passes in.
    CatalogEntity fresh(tabl_id);
    fresh.system_sid = "A4H";
    fresh.domain = CatalogDomain::Ddic;
    fresh.object_type = "TABL";
    fresh.technical_name = "SFLIGHT";
    fresh.display_name = "Flight schedule";
    fresh.extracted_at = "2026-07-19T13:00:00Z";

    REQUIRE(store->UpsertEntitiesAndFields({fresh}, {}).IsOk());

    auto entity = store->GetEntity(tabl_id);
    REQUIRE(entity.IsOk());
    REQUIRE(entity.Value().has_value());
    REQUIRE(entity.Value()->biz_definition.has_value());
    CHECK(*entity.Value()->biz_definition == "Flight schedule master data");
    REQUIRE(entity.Value()->biz_owner.has_value());
    CHECK(*entity.Value()->biz_owner == "steward@example.com");
}

TEST_CASE("DuckDbCatalogStore: ListEntityIds with a scope filter only returns matching entities",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());  // both entities have no package_or_infoarea except SFLIGHT="STEST"

    auto scoped = store->ListEntityIds({"STEST"});
    REQUIRE(scoped.IsOk());
    REQUIRE(scoped.Value().size() == 1);
    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    CHECK(scoped.Value()[0] == tabl_id.Value());

    auto unscoped = store->ListEntityIds();
    REQUIRE(unscoped.IsOk());
    CHECK(unscoped.Value().size() == 2);
}

TEST_CASE("DuckDbCatalogStore: DeleteEntities removes the entity and its fields",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto result = store->DeleteEntities({tabl_id});
    REQUIRE(result.IsOk());

    auto entity = store->GetEntity(tabl_id);
    REQUIRE(entity.IsOk());
    CHECK_FALSE(entity.Value().has_value());

    auto fields = store->GetFields(tabl_id);
    REQUIRE(fields.IsOk());
    CHECK(fields.Value().empty());

    auto count = store->EntityCount();
    REQUIRE(count.IsOk());
    CHECK(count.Value() == 1);  // only ZCL_PROCUREMENT remains
}

TEST_CASE("DuckDbCatalogStore: UpsertEdges writes a new edge without a prior WriteFeed",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->WriteFeed(MakeSampleFeed()).IsOk());  // entities must exist for FK-shaped lookups

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");

    CatalogEdge new_edge(clas_id, tabl_id);
    new_edge.id = "new-edge-1";
    new_edge.kind = "lineage";
    new_edge.extracted_at = "2026-07-19T11:00:00Z";

    auto result = store->UpsertEdges({new_edge});
    REQUIRE(result.IsOk());

    auto outgoing = store->GetEdgesFrom(clas_id, 10);
    REQUIRE(outgoing.IsOk());
    bool found = false;
    for (const auto& e : outgoing.Value()) {
        if (e.id == "new-edge-1") found = true;
    }
    CHECK(found);
}

TEST_CASE("DuckDbCatalogStore: UpsertEdges replaces an existing edge with the same id, not duplicates",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto feed = MakeSampleFeed();
    REQUIRE(store->WriteFeed(feed).IsOk());

    auto tabl_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "SFLIGHT");
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS", "ZCL_PROCUREMENT");
    auto existing_edge_id = feed.edges.front().id;

    CatalogEdge updated(clas_id, tabl_id);
    updated.id = existing_edge_id;
    updated.kind = "lineage";  // was "uses" in MakeSampleFeed
    updated.extracted_at = "2026-07-19T12:00:00Z";

    REQUIRE(store->UpsertEdges({updated}).IsOk());

    auto outgoing = store->GetEdgesFrom(clas_id, 10);
    REQUIRE(outgoing.IsOk());
    CHECK(outgoing.Value().size() == 1);  // replaced, not duplicated
    CHECK(outgoing.Value().front().kind == "lineage");
}

TEST_CASE("DuckDbCatalogStore: RecordSyncRun then RecentSyncRuns round-trips",
          "[storage][duckdb][sync]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();

    CatalogSyncRunSummary run;
    run.id = "run-1";
    run.started_at = "2026-07-19T12:00:00Z";
    run.finished_at = "2026-07-19T12:00:05Z";
    run.mode = "incremental";
    run.scope = "package:ZTEST";
    run.added = 3;
    run.changed = 1;
    run.removed = 0;
    run.status = "ok";

    REQUIRE(store->RecordSyncRun(run).IsOk());

    auto runs = store->RecentSyncRuns(10);
    REQUIRE(runs.IsOk());
    REQUIRE(runs.Value().size() == 1);
    CHECK(runs.Value()[0].id == "run-1");
    CHECK(runs.Value()[0].added == 3);
    CHECK(runs.Value()[0].mode == "incremental");
}

TEST_CASE("DuckDbCatalogStore: LoadSyncCheckpoint on a fresh store returns exists=false",
          "[storage][duckdb][checkpoint]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto loaded = store->LoadSyncCheckpoint();
    REQUIRE(loaded.IsOk());
    CHECK_FALSE(loaded.Value().exists);
}

TEST_CASE("DuckDbCatalogStore: ResetSyncCheckpoint then LoadSyncCheckpoint reflects the "
          "requested scope with no items done yet",
          "[storage][duckdb][checkpoint]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZA", "ZB"}, {"IA1"}).IsOk());

    auto loaded = store->LoadSyncCheckpoint();
    REQUIRE(loaded.IsOk());
    const auto& state = loaded.Value();
    CHECK(state.exists);
    CHECK(state.sid == "A4H");
    CHECK(state.requested_packages == std::vector<std::string>{"ZA", "ZB"});
    CHECK(state.requested_infoareas == std::vector<std::string>{"IA1"});
    CHECK(state.completed_packages.empty());
    CHECK_FALSE(state.interrupted);
}

TEST_CASE("DuckDbCatalogStore: RecordSyncCheckpointItem accumulates completed items, upserts "
          "on repeat",
          "[storage][duckdb][checkpoint]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZA", "ZB", "ZC"}, {}).IsOk());
    REQUIRE(store->RecordSyncCheckpointItem("package", "ZA", 12, 8).IsOk());
    REQUIRE(store->RecordSyncCheckpointItem("package", "ZB", 3, 1).IsOk());
    REQUIRE(store->RecordSyncCheckpointItem("package", "ZA", 12, 8).IsOk());  // repeat, no duplicate

    auto loaded = store->LoadSyncCheckpoint();
    REQUIRE(loaded.IsOk());
    CHECK(loaded.Value().completed_packages == std::set<std::string>{"ZA", "ZB"});
    CHECK_FALSE(loaded.Value().completed_packages.count("ZC"));
}

TEST_CASE("DuckDbCatalogStore: MarkSyncCheckpointInterrupted then MarkSyncCheckpointCompleted "
          "clears the interrupted flag",
          "[storage][duckdb][checkpoint]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZA", "ZB"}, {}).IsOk());
    REQUIRE(store->RecordSyncCheckpointItem("package", "ZA", 1, 1).IsOk());
    REQUIRE(store->MarkSyncCheckpointInterrupted("Connection: timeout").IsOk());

    auto mid = store->LoadSyncCheckpoint();
    REQUIRE(mid.IsOk());
    CHECK(mid.Value().interrupted);

    REQUIRE(store->RecordSyncCheckpointItem("package", "ZB", 1, 1).IsOk());
    REQUIRE(store->MarkSyncCheckpointCompleted().IsOk());

    auto final_state = store->LoadSyncCheckpoint();
    REQUIRE(final_state.IsOk());
    CHECK_FALSE(final_state.Value().interrupted);
    CHECK(final_state.Value().completed_packages == std::set<std::string>{"ZA", "ZB"});
}

TEST_CASE("DuckDbCatalogStore: ResetSyncCheckpoint discards prior checkpoint state",
          "[storage][duckdb][checkpoint]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZA"}, {}).IsOk());
    REQUIRE(store->RecordSyncCheckpointItem("package", "ZA", 1, 1).IsOk());

    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZB"}, {}).IsOk());

    auto loaded = store->LoadSyncCheckpoint();
    REQUIRE(loaded.IsOk());
    CHECK(loaded.Value().requested_packages == std::vector<std::string>{"ZB"});
    CHECK(loaded.Value().completed_packages.empty());
}

TEST_CASE("DuckDbCatalogStore: SchemaVersion reports the current schema",
          "[storage][duckdb]") {
    auto store = DuckDbCatalogStore::Open(":memory:").Value();
    auto version = store->SchemaVersion();
    REQUIRE(version.IsOk());
    CHECK(version.Value() >= 1);
}
