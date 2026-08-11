#include <erpl_adt/adt/catalog_build.hpp>

#include <erpl_adt/adt/bw_export.hpp>
#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/adt/catalog_lineage.hpp>
#include <erpl_adt/adt/ddic.hpp>
#include <erpl_adt/adt/ddic_cds.hpp>
#include <erpl_adt/adt/rfc_function_module.hpp>
#include <erpl_adt/adt/source.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <set>

namespace erpl_adt {

namespace {

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

std::string ToUpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

CatalogDomain ClassifyAbapObjectType(const std::string& object_type) {
    if (object_type.rfind("TABL", 0) == 0) return CatalogDomain::Ddic;
    if (object_type.rfind("DDLS", 0) == 0) return CatalogDomain::Cds;
    return CatalogDomain::Abap;
}

std::string MakeFieldId(const EntityId& entity_id, const std::string& name) {
    return entity_id.Value() + "#" + name;
}

CatalogEntity MakeEntity(const std::string& system_sid,
                          CatalogDomain domain,
                          const std::string& object_type,
                          const std::string& technical_name,
                          const std::string& display_name,
                          const std::optional<std::string>& package_or_infoarea) {
    CatalogEntity entity(DeriveEntityId(system_sid, domain, object_type, technical_name));
    entity.system_sid = system_sid;
    entity.domain = domain;
    entity.object_type = object_type;
    entity.technical_name = technical_name;
    entity.display_name = display_name.empty() ? technical_name : display_name;
    entity.package_or_infoarea = package_or_infoarea;
    entity.extracted_at = UtcTimestampNow();
    return entity;
}

// Best-effort — a failed/missing raw-source fetch is silently skipped
// (leaves entity.raw_json empty), never a warning or hard failure: this is
// purely a fallback for whatever isn't structurally modeled yet, not a
// required part of the build. Only called when
// options.include_raw_source is set (opt-in — see catalog_build.hpp).
void MaybeAttachRawSource(IAdtSession& session, CatalogEntity& entity,
                          const std::string& uri, const CatalogBuildOptions& options) {
    if (!options.include_raw_source || uri.empty()) return;
    auto raw = ReadSource(session, uri + "/source/main");
    if (raw.IsOk() && !raw.Value().empty()) {
        entity.raw_json = raw.Value();
    }
}

} // anonymous namespace

// Errors in this class mean nothing downstream can succeed either (bad
// credentials, no network, CSRF handshake broken) — CatalogBuild (and
// CatalogSync's per-item loop, which reuses this same classification) abort
// immediately instead of recording every subsequent lookup as a warning.
bool IsFatalForWholeBuild(const Error& error) {
    switch (error.category) {
        case ErrorCategory::Connection:
        case ErrorCategory::Authentication:
        // A service that is not activated, or that this user may not reach, is
        // systemic in the same way — every later lookup hits the same wall.
        // These used to arrive as CsrfToken (every 403 did) and were fatal
        // then; keeping them fatal preserves that behaviour.
        case ErrorCategory::Authorization:
        case ErrorCategory::CsrfToken:
            return true;
        default:
            return false;
    }
}

// Walks one function group's function modules, appending an entity + fields
// (one per IMPORTING/EXPORTING/TABLES/CHANGING parameter) + "uses" edges to
// the DDIC structures those parameters reference. An unresolved function
// module (or the listing call itself) is a warning, not a hard failure — one
// bad function module must not fail the whole build, matching
// BuildAbapDdicCdsScope's own tolerance for a single unresolved table/CDS.
//
// A parameter's referenced type is captured as a stub entity
// (domain=Ddic, object_type="unknown") rather than resolved against a real
// TABL/DTEL lookup — the source text alone can't tell a table from a data
// element from a local TYPE, and a second round-trip per parameter to
// disambiguate would be a very large latency cost for a benefit (the exact
// object_type of the target) nothing downstream currently needs. This
// mirrors the project's existing dangling-edge convention (BW lineage stub
// entities in catalog_lineage.cpp).
Result<void, Error> BuildFunctionModuleEntities(IAdtSession& session,
                                                 const std::string& function_group,
                                                 const std::string& owning_package,
                                                 const CatalogBuildOptions& options,
                                                 CatalogFeed& feed,
                                                 std::set<std::string>& seen_ids) {
    auto modules = ListFunctionModules(session, function_group);
    if (modules.IsErr()) {
        if (IsFatalForWholeBuild(modules.Error())) {
            return Result<void, Error>::Err(std::move(modules).Error());
        }
        feed.warnings.push_back("function group " + function_group + ": " +
                                 modules.Error().ToString());
        return Result<void, Error>::Ok();
    }

    for (const auto& fm : modules.Value()) {
        auto fm_entity = MakeEntity(options.system_sid, CatalogDomain::Abap, "FUGR/FF",
                                     fm.object_name, fm.description, owning_package);
        if (!seen_ids.insert(fm_entity.id.Value()).second) {
            continue;  // already captured via another scope in this build
        }
        MaybeAttachRawSource(session, fm_entity, fm.object_uri, options);

        auto signature = GetFunctionModuleSignature(session, fm.object_uri);
        if (signature.IsErr()) {
            if (IsFatalForWholeBuild(signature.Error())) {
                return Result<void, Error>::Err(std::move(signature).Error());
            }
            feed.warnings.push_back("function module " + fm.object_name + ": " +
                                     signature.Error().ToString());
        } else {
            std::set<std::string> edge_targets_this_fm;  // dedupe repeat-type params
            for (const auto& param : signature.Value().parameters) {
                CatalogField field(fm_entity.id);
                field.id = MakeFieldId(fm_entity.id, param.name);
                field.name = param.name;
                field.role = param.kind;
                if (param.type_name.has_value()) field.data_type = *param.type_name;
                if (param.default_value.has_value()) {
                    field.description = (param.is_optional ? "optional, " : "") +
                                        std::string("default: '") + *param.default_value + "'";
                } else if (param.is_optional) {
                    field.description = "optional";
                }
                feed.fields.push_back(std::move(field));

                if (!param.type_name.has_value()) continue;
                if (!edge_targets_this_fm.insert(*param.type_name).second) continue;

                auto target_stub = MakeEntity(options.system_sid, CatalogDomain::Ddic, "unknown",
                                               *param.type_name, *param.type_name, std::nullopt);
                if (seen_ids.insert(target_stub.id.Value()).second) {
                    feed.entities.push_back(target_stub);
                }

                CatalogEdge edge(fm_entity.id, target_stub.id);
                edge.id = fm_entity.id.Value() + "->" + target_stub.id.Value();
                edge.kind = "uses";
                edge.extracted_at = UtcTimestampNow();
                feed.edges.push_back(std::move(edge));
            }
        }

        feed.entities.push_back(std::move(fm_entity));
    }

    return Result<void, Error>::Ok();
}

// Walks one ABAP/DDIC/CDS package tree and appends entities/fields/warnings
// to `feed`. One unresolved *object* is recorded as a warning, not a hard
// failure — the caller still gets everything that *could* be resolved.
// A fatal (connection/auth/CSRF) error aborts the whole build instead, since
// nothing downstream can succeed either. `seen_ids` dedupes entities across
// overlapping/nested scopes (e.g. a parent package listed alongside one of
// its own sub-packages) and, for CatalogSync's resumable per-item loop,
// across separate invocations sharing the same checkpoint lineage.
Result<void, Error> BuildAbapDdicCdsScope(IAdtSession& session,
                                           const std::string& package,
                                           const CatalogBuildOptions& options,
                                           CatalogFeed& feed,
                                           std::set<std::string>& seen_ids) {
    PackageTreeOptions tree_options;
    tree_options.root_package = package;
    tree_options.type_filter = options.type_filter;
    tree_options.max_depth = options.max_depth;

    auto tree = ListPackageTree(session, tree_options);
    if (tree.IsErr()) {
        if (IsFatalForWholeBuild(tree.Error())) {
            return Result<void, Error>::Err(std::move(tree).Error());
        }
        feed.warnings.push_back("package " + package + ": " + tree.Error().ToString());
        return Result<void, Error>::Ok();
    }

    for (const auto& entry : tree.Value()) {
        auto domain = ClassifyAbapObjectType(entry.object_type);
        auto entity = MakeEntity(options.system_sid, domain, entry.object_type,
                                  entry.object_name, entry.description,
                                  entry.package_name.empty() ? package : entry.package_name);
        if (!seen_ids.insert(entity.id.Value()).second) {
            continue;  // already captured via another scope in this build
        }
        MaybeAttachRawSource(session, entity, entry.object_uri, options);

        if (domain == CatalogDomain::Ddic) {
            auto table = GetTableDefinition(session, entry.object_name, options.resolve_ddic_types);
            if (table.IsErr()) {
                if (IsFatalForWholeBuild(table.Error())) {
                    return Result<void, Error>::Err(std::move(table).Error());
                }
                feed.warnings.push_back("table " + entry.object_name + ": " +
                                         table.Error().ToString());
            } else {
                // Dedup FK edges per table — several fields can reference
                // the same check table (e.g. multiple fields checked
                // against T000/mandant-style reference tables).
                std::set<std::string> fk_targets_this_table;
                for (const auto& tf : table.Value().fields) {
                    CatalogField field(entity.id);
                    field.id = MakeFieldId(entity.id, tf.name);
                    field.name = tf.name;
                    field.description =
                        tf.description.empty() ? std::optional<std::string>{} : tf.description;
                    field.data_type = tf.type.empty() ? std::optional<std::string>{} : tf.type;
                    field.length = tf.length;
                    field.decimals = tf.decimals;
                    field.is_key = tf.key_field;
                    if (!tf.fixed_values.empty()) {
                        nlohmann::json arr = nlohmann::json::array();
                        for (const auto& fv : tf.fixed_values) {
                            nlohmann::json fixed_value;
                            fixed_value["low"] = fv.low;
                            if (!fv.high.empty()) fixed_value["high"] = fv.high;
                            if (!fv.text.empty()) fixed_value["text"] = fv.text;
                            arr.push_back(std::move(fixed_value));
                        }
                        field.fixed_values_json = arr.dump();
                    }
                    if (!tf.check_table.empty()) {
                        field.check_table = tf.check_table;
                        if (fk_targets_this_table.insert(tf.check_table).second) {
                            auto target_stub = MakeEntity(options.system_sid, CatalogDomain::Ddic,
                                                          "unknown", tf.check_table,
                                                          tf.check_table, std::nullopt);
                            if (seen_ids.insert(target_stub.id.Value()).second) {
                                feed.entities.push_back(target_stub);
                            }
                            CatalogEdge fk_edge(entity.id, target_stub.id);
                            fk_edge.id = entity.id.Value() + "->" + target_stub.id.Value();
                            fk_edge.kind = "fk";
                            fk_edge.extracted_at = UtcTimestampNow();
                            feed.edges.push_back(std::move(fk_edge));
                        }
                    }
                    feed.fields.push_back(std::move(field));
                }
            }
        } else if (domain == CatalogDomain::Cds) {
            auto cds = GetCdsStructure(session, entry.object_name);
            if (cds.IsErr()) {
                if (IsFatalForWholeBuild(cds.Error())) {
                    return Result<void, Error>::Err(std::move(cds).Error());
                }
                feed.warnings.push_back("cds " + entry.object_name + ": " +
                                         cds.Error().ToString());
            } else {
                if (!cds.Value().source_table.empty()) {
                    entity.source_table = cds.Value().source_table;
                }
                for (const auto& cf : cds.Value().fields) {
                    if (cf.is_association) continue;  // associations become edges, not fields
                    CatalogField field(entity.id);
                    field.id = MakeFieldId(entity.id, cf.name);
                    field.name = cf.name;
                    field.description = cf.description;
                    if (!cf.source_expression.empty()) {
                        field.source_expression = cf.source_expression;
                    }
                    if (!cf.annotations.empty()) {
                        nlohmann::json arr = cf.annotations;
                        field.annotations_json = arr.dump();
                    }
                    feed.fields.push_back(std::move(field));
                }

                // Associations become edges, not fields — see the `continue`
                // above. A target outside this build's scope gets a stub
                // entity, matching the convention used everywhere else in
                // this file (RFC parameters, DDIC foreign keys, BW dataflow).
                for (const auto& assoc : cds.Value().associations) {
                    if (assoc.target.empty()) continue;
                    auto target_stub = MakeEntity(options.system_sid, CatalogDomain::Cds,
                                                  "unknown", assoc.target, assoc.target,
                                                  std::nullopt);
                    if (seen_ids.insert(target_stub.id.Value()).second) {
                        feed.entities.push_back(target_stub);
                    }
                    CatalogEdge assoc_edge(entity.id, target_stub.id);
                    assoc_edge.id = entity.id.Value() + "->" + target_stub.id.Value() + "#" +
                                    assoc.alias;
                    assoc_edge.kind = "association";
                    assoc_edge.extracted_at = UtcTimestampNow();
                    nlohmann::json detail;
                    detail["cardinality"] = assoc.cardinality;
                    detail["on_condition"] = assoc.on_condition;
                    detail["is_composition"] = assoc.is_composition;
                    assoc_edge.detail_json = detail.dump();
                    feed.edges.push_back(std::move(assoc_edge));
                }
            }
        } else if (entry.object_type == "FUGR/F") {
            auto build_result = BuildFunctionModuleEntities(
                session, entry.object_name,
                entry.package_name.empty() ? package : entry.package_name, options, feed,
                seen_ids);
            if (build_result.IsErr()) {
                return Result<void, Error>::Err(std::move(build_result).Error());
            }
        }

        feed.entities.push_back(std::move(entity));
    }

    return Result<void, Error>::Ok();
}

Result<void, Error> BuildBwScope(IAdtSession& session,
                                  const std::string& infoarea,
                                  const CatalogBuildOptions& options,
                                  CatalogFeed& feed,
                                  std::set<std::string>& seen_ids) {
    BwExportOptions bw_options;
    bw_options.infoarea_name = infoarea;
    bw_options.max_depth = options.max_depth;
    bw_options.include_lineage = true;

    auto exported = BwExportInfoarea(session, bw_options);
    if (exported.IsErr()) {
        if (IsFatalForWholeBuild(exported.Error())) {
            return Result<void, Error>::Err(std::move(exported).Error());
        }
        feed.warnings.push_back("infoarea " + infoarea + ": " + exported.Error().ToString());
        return Result<void, Error>::Ok();
    }

    // Name -> object_type index over every object the export walked
    // (including provider objects the xref/orphan-elem edge collectors
    // added), so dataflow_edges below (which reference endpoints by name)
    // can be resolved to the SAME entity IDs the per-object loop creates —
    // not a separate derivation that would silently diverge.
    std::map<std::string, std::string> name_to_type;
    for (const auto& obj : exported.Value().objects) {
        name_to_type.emplace(obj.name, obj.type);  // first wins
    }

    for (const auto& obj : exported.Value().objects) {
        auto entity = MakeEntity(options.system_sid, CatalogDomain::Bw, obj.type, obj.name,
                                  obj.description,
                                  obj.package_name.empty() ? infoarea : obj.package_name);
        if (!obj.subtype.empty()) entity.object_subtype = obj.subtype;
        if (!seen_ids.insert(entity.id.Value()).second) {
            continue;  // already captured via another infoarea in this build
        }

        for (const auto& bf : obj.fields) {
            CatalogField field(entity.id);
            field.id = MakeFieldId(entity.id, bf.name);
            field.name = bf.name;
            if (!bf.description.empty()) field.description = bf.description;
            if (!bf.data_type.empty()) field.data_type = bf.data_type;
            if (bf.length != 0) field.length = bf.length;
            if (bf.decimals != 0) field.decimals = bf.decimals;
            if (!bf.info_object.empty()) field.role = "characteristic";
            feed.fields.push_back(std::move(field));
        }

        // A query (ELEM) has no field list of its own — its "fields" are
        // the characteristics/key figures/variables/filters it's built
        // from, captured as iobj_refs with a role (row/column/free/filter/
        // variable/key_figure/restricted_key_figure/calculated_key_figure)
        // rather than a data type/length. Restricted/calculated key figures
        // additionally carry a human-readable formula/restriction string.
        for (const auto& ref : obj.iobj_refs) {
            CatalogField field(entity.id);
            field.id = MakeFieldId(entity.id, ref.name);
            field.name = ref.name;
            if (!ref.role.empty()) field.role = ref.role;
            if (ref.formula.has_value() && !ref.formula->empty()) field.formula = *ref.formula;
            feed.fields.push_back(std::move(field));
        }

        if (obj.lineage.has_value()) {
            auto conversion = ConvertBwLineageGraph(options.system_sid, *obj.lineage, seen_ids);
            for (auto& stub : conversion.stub_entities) {
                seen_ids.insert(stub.id.Value());
                feed.entities.push_back(std::move(stub));
            }
            for (auto& edge : conversion.edges) {
                feed.edges.push_back(std::move(edge));
            }
        }

        feed.entities.push_back(std::move(entity));
    }

    // InfoProvider<->Query relationships (from xref lookups and orphan-ELEM
    // recovery) — the only edge source when the scope has no DTPA objects
    // at all, which is the common case for a plain "give me this InfoArea"
    // build. Endpoints not walked as part of this infoarea (a provider or
    // query outside the requested scope) get a stub entity, matching the
    // dangling-edge convention used for RFC function-module parameters —
    // ReconcileStubEdges (run once at the end of the whole CatalogBuild)
    // resolves it onto a real entity if one turns up elsewhere in the same
    // build.
    for (const auto& dfe : exported.Value().dataflow_edges) {
        auto from_it = name_to_type.find(dfe.from);
        auto to_it = name_to_type.find(dfe.to);
        auto from_id = DeriveEntityId(options.system_sid, CatalogDomain::Bw,
            from_it != name_to_type.end() ? from_it->second : "unknown", dfe.from);
        auto to_id = DeriveEntityId(options.system_sid, CatalogDomain::Bw,
            to_it != name_to_type.end() ? to_it->second : "unknown", dfe.to);

        if (from_it == name_to_type.end() && seen_ids.insert(from_id.Value()).second) {
            feed.entities.push_back(MakeEntity(options.system_sid, CatalogDomain::Bw, "unknown",
                                                dfe.from, dfe.from, infoarea));
        }
        if (to_it == name_to_type.end() && seen_ids.insert(to_id.Value()).second) {
            feed.entities.push_back(MakeEntity(options.system_sid, CatalogDomain::Bw, "unknown",
                                                dfe.to, dfe.to, infoarea));
        }

        CatalogEdge edge(from_id, to_id);
        edge.id = from_id.Value() + "->" + to_id.Value();
        edge.kind = dfe.type.empty() ? "uses" : dfe.type;
        edge.extracted_at = UtcTimestampNow();
        feed.edges.push_back(std::move(edge));
    }

    for (const auto& warning : exported.Value().warnings) {
        feed.warnings.push_back("infoarea " + infoarea + ": " + warning);
    }

    return Result<void, Error>::Ok();
}

// Rewrites "uses" edges pointing at a stub entity (domain=Ddic,
// object_type="unknown" — created when a function-module parameter's
// referenced type couldn't be disambiguated from source text alone, see
// BuildFunctionModuleEntities) onto a real entity discovered elsewhere in
// this same build, when one with a matching technical_name exists. Without
// this, a table genuinely referenced by an RFC parameter still shows zero
// where-used/lineage on its own real entity page — the edge exists, but
// points at a same-named duplicate the user never navigates to.
//
// Matches by uppercased technical_name only — a real object_type-aware
// disambiguation would need an extra ADT round-trip per parameter (the
// same cost tradeoff already noted in BuildFunctionModuleEntities), so a
// genuine cross-domain name collision (rare) could reconcile onto the
// wrong entity. First real match wins. Scoped to a single CatalogBuild()
// call — a stub created by one build and a real entity added by a later,
// separate build are not reconciled (would need a store-level pass).
void ReconcileStubEdges(CatalogFeed& feed) {
    std::map<std::string, std::string> real_id_by_name;  // upper(name) -> real entity id
    std::map<std::string, std::string> stub_name_by_id;  // stub entity id -> upper(name)
    for (const auto& e : feed.entities) {
        if (e.object_type == "unknown") {
            stub_name_by_id[e.id.Value()] = ToUpperCopy(e.technical_name);
        } else if (!e.technical_name.empty()) {
            real_id_by_name.emplace(ToUpperCopy(e.technical_name), e.id.Value());
        }
    }
    if (real_id_by_name.empty() || stub_name_by_id.empty()) return;

    std::set<std::string> still_referenced_stubs;
    for (auto& edge : feed.edges) {
        auto stub_it = stub_name_by_id.find(edge.to_id.Value());
        if (stub_it == stub_name_by_id.end()) continue;  // not targeting a stub

        auto real_it = real_id_by_name.find(stub_it->second);
        if (real_it == real_id_by_name.end()) {
            still_referenced_stubs.insert(edge.to_id.Value());
            continue;
        }
        auto real_id = EntityId::Create(real_it->second);
        if (real_id.IsErr()) continue;  // malformed id — leave the edge as-is
        edge.to_id = real_id.Value();
        edge.id = edge.from_id.Value() + "->" + edge.to_id.Value();
    }

    // Drop stub entities no edge points at anymore (either reconciled away,
    // or never referenced in the first place shouldn't happen, but is safe
    // to sweep here too).
    feed.entities.erase(
        std::remove_if(feed.entities.begin(), feed.entities.end(),
                        [&](const CatalogEntity& e) {
                            return e.object_type == "unknown" &&
                                   still_referenced_stubs.find(e.id.Value()) ==
                                       still_referenced_stubs.end();
                        }),
        feed.entities.end());
}

Result<CatalogFeed, Error> CatalogBuild(IAdtSession& session, const CatalogBuildOptions& options) {
    CatalogFeed feed;
    feed.system_sid = options.system_sid;
    feed.built_at = UtcTimestampNow();

    std::set<std::string> seen_ids;
    for (const auto& package : options.packages) {
        auto result = BuildAbapDdicCdsScope(session, package, options, feed, seen_ids);
        if (result.IsErr()) {
            return Result<CatalogFeed, Error>::Err(std::move(result).Error());
        }
    }
    for (const auto& infoarea : options.infoareas) {
        auto result = BuildBwScope(session, infoarea, options, feed, seen_ids);
        if (result.IsErr()) {
            return Result<CatalogFeed, Error>::Err(std::move(result).Error());
        }
    }

    ReconcileStubEdges(feed);

    // Deterministic ordering (NFR-2): stable across runs on unchanged data.
    std::sort(feed.entities.begin(), feed.entities.end(),
              [](const CatalogEntity& a, const CatalogEntity& b) {
                  return a.id.Value() < b.id.Value();
              });
    std::sort(feed.fields.begin(), feed.fields.end(),
              [](const CatalogField& a, const CatalogField& b) {
                  if (a.entity_id.Value() != b.entity_id.Value()) {
                      return a.entity_id.Value() < b.entity_id.Value();
                  }
                  return a.name < b.name;
              });

    return Result<CatalogFeed, Error>::Ok(std::move(feed));
}

} // namespace erpl_adt
