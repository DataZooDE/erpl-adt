#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/adt/catalog_sync.hpp>
#include <erpl_adt/storage/duckdb_catalog_store.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

CatalogEntity MakeEntity(const std::string& name,
                          std::optional<std::string> package_or_infoarea = std::nullopt) {
    auto id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", name);
    CatalogEntity e(id);
    e.system_sid = "A4H";
    e.domain = CatalogDomain::Ddic;
    e.object_type = "TABL";
    e.technical_name = name;
    e.display_name = name;
    e.package_or_infoarea = std::move(package_or_infoarea);
    e.extracted_at = "2026-07-19T12:00:00Z";
    return e;
}

const char* kEmptyTreeXml =
    "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
    "<asx:values><DATA><TREE_CONTENT></TREE_CONTENT></DATA></asx:values></asx:abap>";

Error MakeConnectionError() {
    return Error{"ListPackageTree", "/sap/bc/adt/repository/nodestructure", std::nullopt,
                 "Connection refused", std::nullopt, ErrorCategory::Connection};
}

} // anonymous namespace

TEST_CASE("DiffFeedAgainstStore: classifies added/changed/removed", "[adt][catalog][sync]") {
    CatalogFeed feed;
    feed.entities = {MakeEntity("EXISTING"), MakeEntity("BRANDNEW")};

    auto existing_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "EXISTING");
    auto removed_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "GONE");
    std::vector<std::string> existing_ids = {existing_id.Value(), removed_id.Value()};

    auto diff = DiffFeedAgainstStore(feed, existing_ids);

    REQUIRE(diff.added.size() == 1);
    CHECK(diff.added[0].technical_name == "BRANDNEW");

    REQUIRE(diff.changed.size() == 1);
    CHECK(diff.changed[0].technical_name == "EXISTING");

    REQUIRE(diff.removed.size() == 1);
    CHECK(diff.removed[0].Value() == removed_id.Value());
}

TEST_CASE("DiffFeedAgainstStore: an empty store means everything is added",
          "[adt][catalog][sync]") {
    CatalogFeed feed;
    feed.entities = {MakeEntity("A"), MakeEntity("B")};

    auto diff = DiffFeedAgainstStore(feed, {});
    CHECK(diff.added.size() == 2);
    CHECK(diff.changed.empty());
    CHECK(diff.removed.empty());
}

TEST_CASE("CatalogSync: writes only the delta and records a sync_runs row",
          "[adt][catalog][sync]") {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    // Seed the store with one entity that will be "removed" this sync (not
    // present in the fresh build) and one that will be "changed" (present
    // in both).
    CatalogFeed seed;
    // Must carry package_or_infoarea="ZTEST" — CatalogSync now scopes its
    // removal candidates to the sync's own packages/infoareas, so an entity
    // outside that scope (package_or_infoarea unset or different) is never
    // considered for removal regardless of whether it's in the fresh feed.
    seed.entities = {MakeEntity("ZTEST_SUB_STALE", "ZTEST")};
    // Give ZCL_EXAMPLE (from package_contents.xml) a pre-existing row so the
    // sync counts it as "changed" rather than "added".
    // CatalogBuild derives IDs from the entry's full object_type string
    // ("CLAS/OC" from the SEU_ADT_REPOSITORY_OBJ_NODE fixture below), not a
    // shortened "CLAS" — the seeded row must match that exactly for the
    // sync to see it as "changed" rather than "added".
    auto clas_id = DeriveEntityId("A4H", CatalogDomain::Abap, "CLAS/OC", "ZCL_EXAMPLE");
    CatalogEntity stale_clas(clas_id);
    stale_clas.system_sid = "A4H";
    stale_clas.domain = CatalogDomain::Abap;
    stale_clas.object_type = "CLAS/OC";
    stale_clas.technical_name = "ZCL_EXAMPLE";
    stale_clas.display_name = "stale description";
    stale_clas.package_or_infoarea = "ZTEST";  // must be in-scope to be seen as "changed"
    stale_clas.extracted_at = "2026-07-19T09:00:00Z";
    seed.entities.push_back(stale_clas);
    REQUIRE(store->WriteFeed(seed).IsOk());

    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml">
<asx:values><DATA><TREE_CONTENT>
<SEU_ADT_REPOSITORY_OBJ_NODE><OBJECT_TYPE>CLAS/OC</OBJECT_TYPE>
<OBJECT_NAME>ZCL_EXAMPLE</OBJECT_NAME><OBJECT_URI>/sap/bc/adt/oo/classes/zcl_example</OBJECT_URI>
<EXPANDABLE></EXPANDABLE><DESCRIPTION>Example class</DESCRIPTION></SEU_ADT_REPOSITORY_OBJ_NODE>
</TREE_CONTENT></DATA></asx:values></asx:abap>)"}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogSync(mock, *store, options, "package:ZTEST");
    REQUIRE(result.IsOk());
    const auto& summary = result.Value();
    CHECK(summary.added == 0);
    CHECK(summary.changed == 1);   // ZCL_EXAMPLE
    CHECK(summary.removed == 1);   // ZTEST_SUB_STALE
    CHECK(summary.mode == "incremental");
    CHECK(summary.scope == "package:ZTEST");

    // Store reflects the delta.
    auto stale_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "ZTEST_SUB_STALE");
    auto stale_after = store->GetEntity(stale_id);
    REQUIRE(stale_after.IsOk());
    CHECK_FALSE(stale_after.Value().has_value());  // removed

    auto clas_after = store->GetEntity(clas_id);
    REQUIRE(clas_after.IsOk());
    REQUIRE(clas_after.Value().has_value());
    CHECK(clas_after.Value()->display_name == "Example class");  // updated, not stale

    auto runs = store->RecentSyncRuns(10);
    REQUIRE(runs.IsOk());
    REQUIRE(runs.Value().size() == 1);
    CHECK(runs.Value()[0].mode == "incremental");
}

TEST_CASE("CatalogSync: never removes an entity outside its own scope, even if a "
          "same-named entity from a different package is stale",
          "[adt][catalog][sync]") {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    // Seed an entity belonging to a DIFFERENT package than the one about to
    // be synced. A correct sync of --package ZTEST must leave it untouched.
    CatalogFeed seed;
    seed.entities = {MakeEntity("OTHER_PKG_TABLE", "SOME_OTHER_PACKAGE")};
    REQUIRE(store->WriteFeed(seed).IsOk());

    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
                  "<asx:values><DATA><TREE_CONTENT></TREE_CONTENT></DATA></asx:values></asx:abap>"}));

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZTEST"};

    auto result = CatalogSync(mock, *store, options, "package:ZTEST");
    REQUIRE(result.IsOk());
    CHECK(result.Value().removed == 0);  // OTHER_PKG_TABLE must survive

    auto other_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "OTHER_PKG_TABLE");
    auto still_there = store->GetEntity(other_id);
    REQUIRE(still_there.IsOk());
    CHECK(still_there.Value().has_value());
}

TEST_CASE("CatalogSync: a fatal error mid-scope checkpoints prior items, and --resume completes "
          "the rest without redoing them",
          "[adt][catalog][sync][resume]") {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZA", "ZB", "ZC"};

    // First attempt: ZA succeeds, ZB fails fatally — ZC must never be reached.
    {
        MockAdtSession mock;
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTreeXml}));
        mock.EnqueuePost(Result<HttpResponse, Error>::Err(MakeConnectionError()));

        auto result = CatalogSync(mock, *store, options, "package:ZA,ZB,ZC", {});
        REQUIRE(result.IsErr());
        CHECK(mock.PostCallCount() == 2);  // ZA + ZB attempted, ZC never reached

        auto runs = store->RecentSyncRuns(10);
        REQUIRE(runs.IsOk());
        REQUIRE(runs.Value().size() == 1);
        CHECK(runs.Value()[0].status == "interrupted");
    }

    auto loaded = store->LoadSyncCheckpoint();
    REQUIRE(loaded.IsOk());
    CHECK(loaded.Value().interrupted);
    CHECK(loaded.Value().completed_packages == std::set<std::string>{"ZA"});

    // Resume: only ZB and ZC should be (re)attempted — ZA must not be redone.
    {
        MockAdtSession mock;
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTreeXml}));
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTreeXml}));

        CatalogSyncPipelineOptions pipeline;
        pipeline.resume = true;
        std::vector<CatalogSyncProgress> progress_calls;
        pipeline.on_progress = [&](const CatalogSyncProgress& p) { progress_calls.push_back(p); };

        auto result = CatalogSync(mock, *store, options, "package:ZA,ZB,ZC", pipeline);
        REQUIRE(result.IsOk());
        CHECK(mock.PostCallCount() == 2);  // ZA skipped — only ZB and ZC hit ADT this time
        REQUIRE(progress_calls.size() == 2);
        CHECK(progress_calls[0].item_name == "ZB");
        CHECK(progress_calls[1].item_name == "ZC");
    }

    auto final_state = store->LoadSyncCheckpoint();
    REQUIRE(final_state.IsOk());
    CHECK_FALSE(final_state.Value().interrupted);
    CHECK(final_state.Value().completed_packages == std::set<std::string>{"ZA", "ZB", "ZC"});
}

TEST_CASE("CatalogSync: removal detection is skipped on a resumed run", "[adt][catalog][sync][resume]") {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());

    // A stale entity in-scope that a full (non-resumed) sync of {ZA,ZB}
    // would normally remove, since neither package's fresh walk reports it.
    CatalogFeed seed;
    seed.entities = {MakeEntity("STALE_IN_SCOPE", "ZA")};
    REQUIRE(store->WriteFeed(seed).IsOk());

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZA", "ZB"};

    // First attempt: ZA succeeds, ZB fails fatally.
    {
        MockAdtSession mock;
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTreeXml}));
        mock.EnqueuePost(Result<HttpResponse, Error>::Err(MakeConnectionError()));

        REQUIRE(CatalogSync(mock, *store, options, "package:ZA,ZB", {}).IsErr());
    }

    // Resume: ZB succeeds this time — the run completes, but since it was
    // resumed, removal must be skipped.
    {
        MockAdtSession mock;
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kEmptyTreeXml}));

        CatalogSyncPipelineOptions pipeline;
        pipeline.resume = true;

        auto result = CatalogSync(mock, *store, options, "package:ZA,ZB", pipeline);
        REQUIRE(result.IsOk());
        CHECK(result.Value().removed == 0);
    }

    auto stale_id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "STALE_IN_SCOPE");
    auto still_there = store->GetEntity(stale_id);
    REQUIRE(still_there.IsOk());
    CHECK(still_there.Value().has_value());  // removal never attempted — must survive
}

TEST_CASE("CatalogSync: a checkpoint recorded for a different scope refuses to resume",
          "[adt][catalog][sync][resume]") {
    std::shared_ptr<DuckDbCatalogStore> store(DuckDbCatalogStore::Open(":memory:").Value());
    REQUIRE(store->ResetSyncCheckpoint("A4H", {"ZOLD"}, {}).IsOk());

    CatalogBuildOptions options;
    options.system_sid = "A4H";
    options.packages = {"ZNEW"};  // different scope than the checkpoint

    MockAdtSession mock;  // no responses enqueued — must fail before any ADT call
    CatalogSyncPipelineOptions pipeline;
    pipeline.resume = true;

    auto result = CatalogSync(mock, *store, options, "package:ZNEW", pipeline);
    REQUIRE(result.IsErr());
    CHECK(mock.PostCallCount() == 0);
}
