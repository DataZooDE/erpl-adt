#include <erpl_adt/adt/catalog_build.hpp>

#include <erpl_adt/adt/bw_export.hpp>
#include <erpl_adt/adt/catalog_ids.hpp>
#include <erpl_adt/adt/catalog_lineage.hpp>
#include <erpl_adt/adt/ddic.hpp>
#include <erpl_adt/adt/ddic_cds.hpp>

#include <algorithm>
#include <ctime>
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

} // anonymous namespace

// Errors in this class mean nothing downstream can succeed either (bad
// credentials, no network, CSRF handshake broken) — CatalogBuild (and
// CatalogSync's per-item loop, which reuses this same classification) abort
// immediately instead of recording every subsequent lookup as a warning.
bool IsFatalForWholeBuild(const Error& error) {
    switch (error.category) {
        case ErrorCategory::Connection:
        case ErrorCategory::Authentication:
        case ErrorCategory::CsrfToken:
            return true;
        default:
            return false;
    }
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

        if (domain == CatalogDomain::Ddic) {
            auto table = GetTableDefinition(session, entry.object_name, options.resolve_ddic_types);
            if (table.IsErr()) {
                if (IsFatalForWholeBuild(table.Error())) {
                    return Result<void, Error>::Err(std::move(table).Error());
                }
                feed.warnings.push_back("table " + entry.object_name + ": " +
                                         table.Error().ToString());
            } else {
                for (const auto& tf : table.Value().fields) {
                    CatalogField field(entity.id);
                    field.id = MakeFieldId(entity.id, tf.name);
                    field.name = tf.name;
                    field.data_type = tf.type.empty() ? std::optional<std::string>{} : tf.type;
                    field.length = tf.length;
                    field.decimals = tf.decimals;
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
                for (const auto& cf : cds.Value().fields) {
                    if (cf.is_association) continue;  // associations become edges, not fields
                    CatalogField field(entity.id);
                    field.id = MakeFieldId(entity.id, cf.name);
                    field.name = cf.name;
                    feed.fields.push_back(std::move(field));
                }
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

    for (const auto& obj : exported.Value().objects) {
        auto entity = MakeEntity(options.system_sid, CatalogDomain::Bw, obj.type, obj.name,
                                  obj.description,
                                  obj.package_name.empty() ? infoarea : obj.package_name);
        if (!seen_ids.insert(entity.id.Value()).second) {
            continue;  // already captured via another infoarea in this build
        }

        for (const auto& bf : obj.fields) {
            CatalogField field(entity.id);
            field.id = MakeFieldId(entity.id, bf.name);
            field.name = bf.name;
            if (!bf.data_type.empty()) field.data_type = bf.data_type;
            if (bf.length != 0) field.length = bf.length;
            if (bf.decimals != 0) field.decimals = bf.decimals;
            if (!bf.info_object.empty()) field.role = "characteristic";
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

    for (const auto& warning : exported.Value().warnings) {
        feed.warnings.push_back("infoarea " + infoarea + ": " + warning);
    }

    return Result<void, Error>::Ok();
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
