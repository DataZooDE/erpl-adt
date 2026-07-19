#include <erpl_adt/adt/catalog_sync.hpp>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <set>

namespace erpl_adt {

namespace {

// Disambiguates sync_runs.id (primary key) for two syncs completing within
// the same wall-clock second — UtcTimestampNow() alone isn't unique enough.
std::atomic<int> g_sync_counter{0};

std::string UtcTimestampNow() {
    std::time_t now = std::time(nullptr);
    struct tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

} // anonymous namespace

CatalogDiff DiffFeedAgainstStore(const CatalogFeed& new_feed,
                                  const std::vector<std::string>& existing_entity_ids) {
    CatalogDiff diff;
    std::set<std::string> existing(existing_entity_ids.begin(), existing_entity_ids.end());
    std::set<std::string> in_feed;

    for (const auto& entity : new_feed.entities) {
        in_feed.insert(entity.id.Value());
        if (existing.count(entity.id.Value())) {
            diff.changed.push_back(entity);
        } else {
            diff.added.push_back(entity);
        }
    }
    for (const auto& id : existing_entity_ids) {
        if (!in_feed.count(id)) {
            diff.removed.push_back(EntityId::Create(id).Value());
        }
    }

    return diff;
}

Result<CatalogSyncRunSummary, Error> CatalogSync(IAdtSession& session, ICatalogStore& store,
                                                   const CatalogBuildOptions& options,
                                                   const std::string& scope_label,
                                                   const CatalogSyncPipelineOptions& pipeline_opts) {
    CatalogSyncRunSummary summary;
    summary.id = "sync-" + UtcTimestampNow() + "-" + std::to_string(g_sync_counter.fetch_add(1));
    summary.started_at = UtcTimestampNow();
    summary.mode = "incremental";
    summary.scope = scope_label;

    std::set<std::string> done_packages;
    std::set<std::string> done_infoareas;
    bool resumed = false;

    if (pipeline_opts.resume) {
        auto loaded = store.LoadSyncCheckpoint();
        if (loaded.IsErr()) {
            return Result<CatalogSyncRunSummary, Error>::Err(std::move(loaded).Error());
        }
        const auto& state = loaded.Value();
        if (state.exists) {
            auto sorted = [](std::vector<std::string> v) {
                std::sort(v.begin(), v.end());
                return v;
            };
            bool scope_matches = state.sid == options.system_sid &&
                                  sorted(state.requested_packages) == sorted(options.packages) &&
                                  sorted(state.requested_infoareas) == sorted(options.infoareas);
            if (!scope_matches) {
                return Result<CatalogSyncRunSummary, Error>::Err(Error{
                    "CatalogSync", "", std::nullopt,
                    "The checkpoint stored in this DuckDB file was recorded for a different "
                    "sid/scope — won't resume. Run without --resume (this starts a fresh "
                    "checkpoint) if the scope has changed on purpose.",
                    std::nullopt});
            }
            done_packages = state.completed_packages;
            done_infoareas = state.completed_infoareas;
            resumed = true;
        }
        // else: --resume was given but there's nothing to resume yet — fall through and
        // start a fresh checkpoint below, same as a first-ever run.
    }
    if (!resumed) {
        auto reset = store.ResetSyncCheckpoint(options.system_sid, options.packages, options.infoareas);
        if (reset.IsErr()) {
            return Result<CatalogSyncRunSummary, Error>::Err(std::move(reset).Error());
        }
    }

    // Seeded from the store on resume (anything already there is, by
    // definition, already "seen" from an earlier item in this checkpoint
    // lineage) — empty on a fresh run, matching CatalogBuild's own seen_ids.
    std::set<std::string> seen_ids;
    if (resumed) {
        auto existing_ids = store.ListEntityIds({});
        if (existing_ids.IsErr()) {
            return Result<CatalogSyncRunSummary, Error>::Err(std::move(existing_ids).Error());
        }
        seen_ids.insert(existing_ids.Value().begin(), existing_ids.Value().end());
    }

    const int total = static_cast<int>(options.packages.size() + options.infoareas.size());
    int index = 0;
    int added_total = 0;
    int changed_total = 0;
    bool interrupted = false;
    Error interrupt_error{"CatalogSync", "", std::nullopt, "", std::nullopt};

    // Processes one package/infoarea: skips it silently if already done (a
    // resumed item), otherwise builds its slice, upserts entities/fields/
    // edges, and appends a checkpoint event. Returns false to signal the
    // caller should stop the whole loop (a fatal error occurred).
    auto process_item = [&](const std::string& kind, const std::string& name,
                             const std::function<Result<void, Error>(CatalogFeed&)>& build_one) {
        ++index;
        bool already_done =
            (kind == "infoarea") ? done_infoareas.count(name) > 0 : done_packages.count(name) > 0;
        if (already_done) {
            return true;
        }

        CatalogFeed item_feed;
        item_feed.system_sid = options.system_sid;
        item_feed.built_at = UtcTimestampNow();

        auto build_result = build_one(item_feed);
        if (build_result.IsErr()) {
            interrupted = true;
            interrupt_error = std::move(build_result).Error();
            return false;
        }

        auto existing_result = store.ListEntityIds({name});
        if (existing_result.IsErr()) {
            interrupted = true;
            interrupt_error = std::move(existing_result).Error();
            return false;
        }
        auto diff = DiffFeedAgainstStore(item_feed, existing_result.Value());

        std::vector<CatalogEntity> to_upsert = diff.added;
        to_upsert.insert(to_upsert.end(), diff.changed.begin(), diff.changed.end());
        std::set<std::string> upsert_ids;
        for (const auto& e : to_upsert) upsert_ids.insert(e.id.Value());
        std::vector<CatalogField> fields_for_upsert;
        for (const auto& f : item_feed.fields) {
            if (upsert_ids.count(f.entity_id.Value())) fields_for_upsert.push_back(f);
        }

        if (!to_upsert.empty() || !fields_for_upsert.empty()) {
            auto upsert_result = store.UpsertEntitiesAndFields(to_upsert, fields_for_upsert);
            if (upsert_result.IsErr()) {
                interrupted = true;
                interrupt_error = std::move(upsert_result).Error();
                return false;
            }
        }
        if (!item_feed.edges.empty()) {
            auto edge_result = store.UpsertEdges(item_feed.edges);
            if (edge_result.IsErr()) {
                interrupted = true;
                interrupt_error = std::move(edge_result).Error();
                return false;
            }
        }

        added_total += static_cast<int>(diff.added.size());
        changed_total += static_cast<int>(diff.changed.size());

        auto recorded = store.RecordSyncCheckpointItem(kind, name,
                                                        static_cast<int>(item_feed.entities.size()),
                                                        static_cast<int>(item_feed.fields.size()));
        if (recorded.IsErr()) {
            interrupted = true;
            interrupt_error = std::move(recorded).Error();
            return false;
        }

        if (pipeline_opts.on_progress) {
            pipeline_opts.on_progress(CatalogSyncProgress{kind, name, index, total});
        }
        return true;
    };

    for (const auto& package : options.packages) {
        if (!process_item("package", package, [&](CatalogFeed& f) {
                return BuildAbapDdicCdsScope(session, package, options, f, seen_ids);
            })) {
            break;
        }
    }
    if (!interrupted) {
        for (const auto& infoarea : options.infoareas) {
            if (!process_item("infoarea", infoarea, [&](CatalogFeed& f) {
                    return BuildBwScope(session, infoarea, options, f, seen_ids);
                })) {
                break;
            }
        }
    }

    if (interrupted) {
        summary.status = "interrupted";
        summary.added = added_total;
        summary.changed = changed_total;
        summary.removed = 0;
        summary.finished_at = UtcTimestampNow();
        (void)store.MarkSyncCheckpointInterrupted(interrupt_error.ToString());  // best-effort
        (void)store.RecordSyncRun(summary);  // best-effort — failure itself is the return value
        return Result<CatalogSyncRunSummary, Error>::Err(std::move(interrupt_error));
    }

    auto completed = store.MarkSyncCheckpointCompleted();
    if (completed.IsErr()) {
        return Result<CatalogSyncRunSummary, Error>::Err(std::move(completed).Error());
    }

    // Removal detection requires the complete picture of "everything this
    // scope actually contains right now" — a resumed run only has the items
    // it personally (re)built in `seen_ids` (plus whatever pre-existed in
    // the store at resume time, which is not the same thing as "confirmed
    // present as of right now"), so skip it rather than risk flagging a
    // still-valid entity as gone.
    int removed_total = 0;
    if (!resumed) {
        std::vector<std::string> scope_filter = options.packages;
        scope_filter.insert(scope_filter.end(), options.infoareas.begin(), options.infoareas.end());
        auto existing_ids_result = store.ListEntityIds(scope_filter);
        if (existing_ids_result.IsErr()) {
            return Result<CatalogSyncRunSummary, Error>::Err(std::move(existing_ids_result).Error());
        }
        std::vector<EntityId> to_remove;
        for (const auto& id_str : existing_ids_result.Value()) {
            if (!seen_ids.count(id_str)) {
                auto id = EntityId::Create(id_str);
                if (id.IsOk()) to_remove.push_back(std::move(id).Value());
            }
        }
        if (!to_remove.empty()) {
            auto delete_result = store.DeleteEntities(to_remove);
            if (delete_result.IsErr()) {
                return Result<CatalogSyncRunSummary, Error>::Err(std::move(delete_result).Error());
            }
        }
        removed_total = static_cast<int>(to_remove.size());
    }

    summary.added = added_total;
    summary.changed = changed_total;
    summary.removed = removed_total;
    summary.status = "ok";
    summary.finished_at = UtcTimestampNow();

    auto record_result = store.RecordSyncRun(summary);
    if (record_result.IsErr()) {
        return Result<CatalogSyncRunSummary, Error>::Err(std::move(record_result).Error());
    }

    return Result<CatalogSyncRunSummary, Error>::Ok(std::move(summary));
}

} // namespace erpl_adt
