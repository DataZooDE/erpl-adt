#include <erpl_adt/mcp/catalog_tool_handlers.hpp>

#include <erpl_adt/adt/catalog_overlay.hpp>
#include <erpl_adt/core/log.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <sstream>

namespace erpl_adt {

namespace {

ToolResult MakeOkResult(const nlohmann::json& data) {
    return ToolResult{false, nlohmann::json::array({{{"type", "text"}, {"text", data.dump()}}})};
}

ToolResult MakeErrorResult(const Error& error) {
    return ToolResult{true, nlohmann::json::array({{{"type", "text"}, {"text", error.ToJson()}}})};
}

ToolResult MakeParamError(const std::string& msg) {
    return ToolResult{true, nlohmann::json::array({{{"type", "text"}, {"text", msg}}})};
}

std::optional<std::string> RequireString(const nlohmann::json& params, const std::string& key,
                                         ToolResult& out_error) {
    if (!params.contains(key) || !params[key].is_string() || params[key].get<std::string>().empty()) {
        out_error = MakeParamError("Missing required parameter: " + key);
        return std::nullopt;
    }
    return params[key].get<std::string>();
}

std::string OptString(const nlohmann::json& params, const std::string& key,
                      const std::string& default_val = "") {
    if (params.contains(key) && params[key].is_string()) return params[key].get<std::string>();
    return default_val;
}

int OptInt(const nlohmann::json& params, const std::string& key, int default_val) {
    if (params.contains(key) && params[key].is_number_integer()) return params[key].get<int>();
    return default_val;
}

nlohmann::json StringProp(const std::string& desc) {
    return {{"type", "string"}, {"description", desc}};
}
nlohmann::json IntProp(const std::string& desc) {
    return {{"type", "integer"}, {"description", desc}};
}
nlohmann::json MakeSchema(nlohmann::json properties, nlohmann::json required) {
    if (properties.is_null()) properties = nlohmann::json::object();
    if (required.is_null()) required = nlohmann::json::array();
    return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}};
}

// FR-MCP-3: every catalog_* response carries schema_version and
// cache_synced_at so a caller can tell how fresh an answer is without a
// separate round-trip.
void AttachCacheMeta(ICatalogStore& store, nlohmann::json& j) {
    auto version = store.SchemaVersion();
    j["schema_version"] = version.IsOk() ? version.Value() : 0;

    auto runs = store.RecentSyncRuns(1);
    if (runs.IsOk() && !runs.Value().empty()) {
        j["cache_synced_at"] = runs.Value()[0].finished_at;
    } else {
        j["cache_synced_at"] = nullptr;  // no `catalog sync` has run yet (full sink only)
    }
}

// NFR-5: every catalog_* call logs (tool, latency_ms, result_count) at -v so
// the §5.1 latency targets are checkable in the field, not just claimed.
void LogToolLatency(const std::string& tool, std::chrono::steady_clock::time_point start,
                     size_t result_count) {
    auto latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();
    std::ostringstream msg;
    msg << "tool=" << tool << " latency_ms=" << latency_ms << " result_count=" << result_count;
    LogInfo("catalog", msg.str());
}

nlohmann::json EntityToJson(const CatalogEntity& e) {
    nlohmann::json ej;
    ej["id"] = e.id.Value();
    ej["domain"] = ToString(e.domain);
    ej["object_type"] = e.object_type;
    if (e.object_subtype.has_value()) ej["object_subtype"] = *e.object_subtype;
    ej["technical_name"] = e.technical_name;
    ej["display_name"] = e.display_name;
    if (e.package_or_infoarea.has_value()) ej["package_or_infoarea"] = *e.package_or_infoarea;
    if (e.biz_definition.has_value()) ej["biz_definition"] = *e.biz_definition;
    if (e.biz_owner.has_value()) ej["biz_owner"] = *e.biz_owner;
    if (e.biz_lob.has_value()) ej["biz_lob"] = *e.biz_lob;
    if (e.biz_confidentiality.has_value()) ej["biz_confidentiality"] = *e.biz_confidentiality;
    if (e.biz_curated_by.has_value()) ej["biz_curated_by"] = *e.biz_curated_by;
    if (e.biz_curated_at.has_value()) ej["biz_curated_at"] = *e.biz_curated_at;
    if (!e.extracted_at.empty()) ej["extracted_at"] = e.extracted_at;
    if (e.changed_at.has_value()) ej["changed_at"] = *e.changed_at;
    if (e.source_table.has_value()) ej["source_table"] = *e.source_table;
    if (!e.raw_json.empty()) ej["raw_source"] = e.raw_json;
    return ej;
}

nlohmann::json EdgeToJson(const CatalogEdge& e) {
    nlohmann::json ej;
    ej["id"] = e.id;
    ej["from_id"] = e.from_id.Value();
    ej["to_id"] = e.to_id.Value();
    ej["kind"] = e.kind;
    ej["resolution"] = e.resolution;
    if (!e.field_mapping_json.empty()) {
        ej["field_mapping"] = nlohmann::json::parse(e.field_mapping_json, nullptr, false);
    }
    if (e.detail_json.has_value()) {
        ej["detail"] = nlohmann::json::parse(*e.detail_json, nullptr, false);
    }
    return ej;
}

} // anonymous namespace

void RegisterCatalogStoreTools(ToolRegistry& registry, std::shared_ptr<ICatalogStore> store) {
    registry.Register(
        "catalog_search",
        "Fast full-text/semantic search over an already-sunk catalog cache — no ADT/SAP call. "
        "Use for interactive lookups (agent context assembly, type-ahead); use catalog_build/"
        "catalog_export instead when you need a live, current-state extraction.",
        MakeSchema(
            {{"query",
              StringProp("Search text — empty (or \"*\") browses all entities in "
                          "technical-name order instead of ranking a match")},
             {"mode", StringProp("fts (default) | vss | hybrid — vss/hybrid require embeddings")},
             {"max_results", IntProp("Maximum results per page (default: 20)")},
             {"cursor", IntProp("Opaque pagination cursor from a prior response's next_cursor "
                                "(default: 0, first page)")},
             {"domain", StringProp("Filter to one domain: ABAP | DDIC | CDS | BW")},
             {"object_type",
              StringProp("Filter to one exact object type, e.g. \"TABL/DT\", \"IOBJ\" — see "
                          "catalog_object_types for what's actually present")},
             {"subtype",
              StringProp("Filter to one exact object subtype, e.g. \"REP\" (a real BW query, "
                          "as opposed to \"VAR\"/\"CKF\"/\"RKF\"/\"FILT\"/\"STR\" which also "
                          "carry object_type ELEM) — see catalog_object_subtypes for what's "
                          "actually present")},
             {"curated_only", {{"type", "boolean"},
                               {"description", "Only entities with a business definition curated"}}}},
            {}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            if (params.contains("query") && !params["query"].is_string()) {
                return MakeParamError("query must be a string");
            }
            // Empty query is deliberately allowed (unlike RequireString's
            // usual empty-rejects-as-missing rule) — it's the "browse
            // everything" signal, not a malformed request.
            auto query = OptString(params, "query", "");
            auto mode = OptString(params, "mode", "fts");
            if (mode != "fts") {
                return MakeParamError("mode '" + mode +
                                      "' requires an embedding provider; only 'fts' is available "
                                      "via this tool today (vss/hybrid: use `catalog search` CLI)");
            }
            ICatalogStore::SearchOptions options;
            options.max_results = OptInt(params, "max_results", 20);
            options.offset = OptInt(params, "cursor", 0);
            if (params.contains("domain") && params["domain"].is_string()) {
                options.domain = params["domain"].get<std::string>();
            }
            if (params.contains("object_type") && params["object_type"].is_string()) {
                options.object_type = params["object_type"].get<std::string>();
            }
            if (params.contains("subtype") && params["subtype"].is_string()) {
                options.object_subtype = params["subtype"].get<std::string>();
            }
            if (params.contains("curated_only") && params["curated_only"].is_boolean()) {
                options.curated_only = params["curated_only"].get<bool>();
            }

            auto t0 = std::chrono::steady_clock::now();
            auto result = store->SearchFtsPage(query, options);
            if (result.IsErr()) return MakeErrorResult(result.Error());
            LogToolLatency("catalog_search", t0, result.Value().hits.size());

            nlohmann::json j;
            j["hits"] = nlohmann::json::array();
            for (const auto& hit : result.Value().hits) {
                nlohmann::json hj = EntityToJson(hit.entity);
                hj["score"] = hit.score;
                j["hits"].push_back(std::move(hj));
            }
            j["has_more"] = result.Value().has_more;
            j["next_cursor"] = result.Value().has_more
                                    ? nlohmann::json(options.offset +
                                                      static_cast<int>(result.Value().hits.size()))
                                    : nlohmann::json(nullptr);
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_get",
        "Fetch one catalog entity (with its fields) by ID from the cache — no ADT/SAP call.",
        MakeSchema({{"id", StringProp("Entity ID (from catalog_search/catalog_build)")}}, {"id"}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto id_str = RequireString(params, "id", err);
            if (!id_str) return err;
            auto id = EntityId::Create(*id_str);
            if (id.IsErr()) return MakeParamError(id.Error());

            auto entity_result = store->GetEntity(id.Value());
            if (entity_result.IsErr()) return MakeErrorResult(entity_result.Error());
            if (!entity_result.Value().has_value()) {
                return MakeParamError("No entity with id: " + *id_str);
            }
            auto fields_result = store->GetFields(id.Value());
            if (fields_result.IsErr()) return MakeErrorResult(fields_result.Error());

            nlohmann::json j = EntityToJson(*entity_result.Value());
            j["fields"] = nlohmann::json::array();
            for (const auto& f : fields_result.Value()) {
                nlohmann::json fj;
                fj["name"] = f.name;
                if (f.data_type.has_value()) fj["data_type"] = *f.data_type;
                if (f.role.has_value()) fj["role"] = *f.role;
                if (f.description.has_value()) fj["description"] = *f.description;
                if (f.length.has_value()) fj["length"] = *f.length;
                if (f.decimals.has_value()) fj["decimals"] = *f.decimals;
                if (f.aggregation.has_value()) fj["aggregation"] = *f.aggregation;
                if (f.unit.has_value()) fj["unit"] = *f.unit;
                if (f.formula.has_value()) fj["formula"] = *f.formula;
                if (f.is_key) fj["is_key"] = true;
                if (f.check_table.has_value()) fj["check_table"] = *f.check_table;
                if (f.fixed_values_json.has_value()) {
                    fj["fixed_values"] = nlohmann::json::parse(*f.fixed_values_json, nullptr, false);
                }
                if (f.source_expression.has_value()) fj["source_expression"] = *f.source_expression;
                if (f.annotations_json.has_value()) {
                    fj["annotations"] = nlohmann::json::parse(*f.annotations_json, nullptr, false);
                }
                j["fields"].push_back(std::move(fj));
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_where_used",
        "Catalog-wide where-used: every cached entity with an edge pointing at the given ID "
        "(FR-7) — no ADT/SAP call.",
        MakeSchema(
            {{"id", StringProp("Entity ID")}, {"max_results", IntProp("Maximum results (default: 50)")}},
            {"id"}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto id_str = RequireString(params, "id", err);
            if (!id_str) return err;
            auto id = EntityId::Create(*id_str);
            if (id.IsErr()) return MakeParamError(id.Error());
            int max_results = OptInt(params, "max_results", 50);

            auto edges_result = store->GetEdgesTo(id.Value(), max_results);
            if (edges_result.IsErr()) return MakeErrorResult(edges_result.Error());

            nlohmann::json j;
            j["edges"] = nlohmann::json::array();
            for (const auto& edge : edges_result.Value()) {
                j["edges"].push_back(EdgeToJson(edge));
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_lineage",
        "End-to-end lineage from the cache: outgoing edges (what this entity feeds into) up to "
        "max_depth hops, following field_mapping/lineage/uses edges — no ADT/SAP call.",
        MakeSchema(
            {{"id", StringProp("Entity ID")},
             {"max_depth", IntProp("Max hops to follow (default: 5)")}},
            {"id"}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto id_str = RequireString(params, "id", err);
            if (!id_str) return err;
            auto start_id = EntityId::Create(*id_str);
            if (start_id.IsErr()) return MakeParamError(start_id.Error());
            int max_depth = OptInt(params, "max_depth", 5);

            nlohmann::json chain = nlohmann::json::array();
            std::set<std::string> visited = {start_id.Value().Value()};
            EntityId current = start_id.Value();

            for (int depth = 0; depth < max_depth; ++depth) {
                auto edges_result = store->GetEdgesFrom(current, 50);
                if (edges_result.IsErr()) return MakeErrorResult(edges_result.Error());
                if (edges_result.Value().empty()) break;

                // Deterministic: first not-yet-visited outgoing edge, in the
                // order the store returns them.
                bool advanced = false;
                for (const auto& edge : edges_result.Value()) {
                    if (!visited.insert(edge.to_id.Value()).second) continue;
                    chain.push_back(EdgeToJson(edge));
                    current = edge.to_id;
                    advanced = true;
                    break;
                }
                if (!advanced) break;
            }

            nlohmann::json j;
            j["chain"] = std::move(chain);
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_driver_tree",
        "Calculated key-figure formula for a field, if captured (FR-4) — no ADT/SAP call.",
        MakeSchema({{"id", StringProp("Entity ID")}}, {"id"}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto id_str = RequireString(params, "id", err);
            if (!id_str) return err;
            auto id = EntityId::Create(*id_str);
            if (id.IsErr()) return MakeParamError(id.Error());

            auto fields_result = store->GetFields(id.Value());
            if (fields_result.IsErr()) return MakeErrorResult(fields_result.Error());

            nlohmann::json j;
            j["fields"] = nlohmann::json::array();
            for (const auto& f : fields_result.Value()) {
                if (!f.formula.has_value() || f.formula->empty()) continue;
                nlohmann::json fj;
                fj["name"] = f.name;
                fj["formula"] = *f.formula;
                j["fields"].push_back(std::move(fj));
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_sync_status",
        "Recent `catalog sync` run history (sync_runs) — no ADT/SAP call.",
        MakeSchema({{"max_results", IntProp("Maximum runs to return (default: 10)")}}, {}),
        [store](const nlohmann::json& params) -> ToolResult {
            int max_results = OptInt(params, "max_results", 10);
            auto runs_result = store->RecentSyncRuns(max_results);
            if (runs_result.IsErr()) return MakeErrorResult(runs_result.Error());

            nlohmann::json j;
            j["runs"] = nlohmann::json::array();
            for (const auto& run : runs_result.Value()) {
                j["runs"].push_back({{"id", run.id},
                                     {"started_at", run.started_at},
                                     {"finished_at", run.finished_at},
                                     {"mode", run.mode},
                                     {"scope", run.scope},
                                     {"added", run.added},
                                     {"changed", run.changed},
                                     {"removed", run.removed},
                                     {"status", run.status}});
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_stats",
        "Cheap health check: row counts, curated-entity count, unresolved-edge count — no ADT/"
        "SAP call.",
        MakeSchema(nlohmann::json::object(), nlohmann::json::array()),
        [store](const nlohmann::json&) -> ToolResult {
            auto stats_result = store->Stats();
            if (stats_result.IsErr()) return MakeErrorResult(stats_result.Error());
            const auto& stats = stats_result.Value();

            nlohmann::json j;
            j["entity_count"] = stats.entity_count;
            j["field_count"] = stats.field_count;
            j["edge_count"] = stats.edge_count;
            j["unresolved_edge_count"] = stats.unresolved_edge_count;
            j["curated_entity_count"] = stats.curated_entity_count;
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_object_types",
        "Distinct (domain, object_type) pairs actually present in the cache, with counts — no "
        "ADT/SAP call. Lets a caller build an object-type filter (e.g. \"only IOBJ\", \"only "
        "TABL/DT\") from what's really there instead of guessing at every domain's possible "
        "object types, which vary a lot (BW: IOBJ/ADSO/CUBE/ELEM/...; ABAP: TABL/CLAS/FUGR/...).",
        MakeSchema(nlohmann::json::object(), nlohmann::json::array()),
        [store](const nlohmann::json&) -> ToolResult {
            auto types_result = store->ListObjectTypeCounts();
            if (types_result.IsErr()) return MakeErrorResult(types_result.Error());

            nlohmann::json j;
            j["types"] = nlohmann::json::array();
            for (const auto& t : types_result.Value()) {
                j["types"].push_back(
                    {{"domain", t.domain}, {"object_type", t.object_type}, {"count", t.count}});
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_object_subtypes",
        "Distinct (domain, object_type, object_subtype) triples actually present in the cache, "
        "with counts — no ADT/SAP call. Only meaningful where object_subtype is set (BW ELEM "
        "today: REP is a real query, VAR/CKF/RKF/FILT/STR are variables/key figures/filters/"
        "structures that share object_type ELEM but aren't queries). Use to build a filter "
        "that isolates real queries from everything else ELEM covers.",
        MakeSchema(nlohmann::json::object(), nlohmann::json::array()),
        [store](const nlohmann::json&) -> ToolResult {
            auto subtypes_result = store->ListObjectSubtypeCounts();
            if (subtypes_result.IsErr()) return MakeErrorResult(subtypes_result.Error());

            nlohmann::json j;
            j["subtypes"] = nlohmann::json::array();
            for (const auto& t : subtypes_result.Value()) {
                j["subtypes"].push_back({{"domain", t.domain},
                                         {"object_type", t.object_type},
                                         {"object_subtype", t.object_subtype},
                                         {"count", t.count}});
            }
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });

    registry.Register(
        "catalog_annotate",
        "Curate business-glossary fields (definition/owner/LoB/confidentiality) onto one cached "
        "entity — writes only the overlay, never SAP. An unknown id is reported as an error, "
        "not silently ignored.",
        MakeSchema(
            {{"id", StringProp("Entity ID")},
             {"definition", StringProp("Business definition")},
             {"owner", StringProp("Business owner/contact")},
             {"lob", StringProp("Line of Business")},
             {"confidentiality", StringProp("Public | Internal | Confidential")},
             {"curated_by", StringProp("Attribution (default: mcp)")}},
            {"id"}),
        [store](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto id_str = RequireString(params, "id", err);
            if (!id_str) return err;

            OverlayEntry entry;
            entry.entity_id = *id_str;
            if (params.contains("definition")) entry.definition = OptString(params, "definition");
            if (params.contains("owner")) entry.owner = OptString(params, "owner");
            if (params.contains("lob")) entry.lob = OptString(params, "lob");
            if (params.contains("confidentiality")) {
                entry.confidentiality = OptString(params, "confidentiality");
            }

            auto curated_by = OptString(params, "curated_by", "mcp");
            auto result = ApplyOverlay(*store, {entry}, curated_by);
            if (!result.orphan_ids.empty()) {
                return MakeParamError("No entity with id: " + *id_str);
            }
            if (!result.write_errors.empty()) {
                return MakeParamError(result.write_errors[0]);
            }

            nlohmann::json j;
            j["applied"] = result.applied_count;
            AttachCacheMeta(*store, j);
            return MakeOkResult(j);
        });
}

} // namespace erpl_adt
