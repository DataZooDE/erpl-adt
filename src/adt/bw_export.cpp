#include <erpl_adt/adt/bw_export.hpp>

#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/bw_nodes.hpp>
#include <erpl_adt/adt/bw_object.hpp>
#include <erpl_adt/adt/bw_query.hpp>
#include <erpl_adt/adt/bw_rsds.hpp>
#include <erpl_adt/adt/bw_search.hpp>
#include <erpl_adt/adt/bw_xref.hpp>

#include <nlohmann/json.hpp>

#include <ctime>
#include <queue>
#include <set>
#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Extract source-system from RSDS URI.
// Pattern: /sap/bw/modeling/rsds/{NAME}/{LOGSYS}/a
// ---------------------------------------------------------------------------
std::string ExtractSourceSystemFromUri(const std::string& uri) {
    if (uri.empty()) return "";
    // split by '/'
    std::vector<std::string> parts;
    std::istringstream ss(uri);
    std::string tok;
    while (std::getline(ss, tok, '/')) {
        if (!tok.empty()) parts.push_back(tok);
    }
    // find "rsds" part
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (parts[i] == "rsds") {
            return parts[i + 2];
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// TypeMatches — check if an object type passes the filter
// ---------------------------------------------------------------------------
bool TypeMatches(const std::string& type,
                 const std::vector<std::string>& filter) {
    if (filter.empty()) return true;
    for (const auto& f : filter) {
        if (f == type) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CollectObjectDetail — fetch per-type detail and fill BwExportedObject.
// Partial failures are recorded in exp.warnings rather than returned as Error.
// ---------------------------------------------------------------------------
void CollectObjectDetail(IAdtSession& session,
                         const BwNodeEntry& node,
                         const BwExportOptions& opts,
                         BwExportedObject& obj,
                         BwInfoareaExport& exp) {
    const std::string& type = node.type;
    const std::string& name = node.name;

    if (type == "ADSO") {
        auto res = BwReadAdsoDetail(session, name, opts.version);
        if (res.IsErr()) {
            exp.warnings.push_back("ADSO " + name + ": " + res.Error().message);
            return;
        }
        const auto& d = res.Value();
        obj.description = d.description;
        obj.package_name = d.package_name;
        for (const auto& f : d.fields) {
            BwExportedField ef;
            ef.name = f.name;
            ef.description = f.description;
            ef.info_object = f.info_object;
            ef.data_type = f.data_type;
            ef.length = f.length;
            ef.decimals = f.decimals;
            ef.key = f.key;
            obj.fields.push_back(std::move(ef));
        }
        exp.provenance.push_back(
            {"BwReadAdsoDetail",
             "/sap/bw/modeling/adso/" + name + "/" + opts.version,
             "ok"});

    } else if (type == "RSDS") {
        std::string source_sys = ExtractSourceSystemFromUri(node.uri);
        auto res = BwReadRsdsDetail(session, name, source_sys, opts.version);
        if (res.IsErr()) {
            exp.warnings.push_back("RSDS " + name + ": " + res.Error().message);
            return;
        }
        const auto& d = res.Value();
        obj.description = d.description;
        obj.package_name = d.package_name;
        for (const auto& f : d.fields) {
            BwExportedField ef;
            ef.name = f.name;
            ef.description = f.description;
            ef.segment_id = f.segment_id;
            ef.data_type = f.data_type;
            ef.length = f.length;
            ef.decimals = f.decimals;
            ef.key = f.key;
            obj.fields.push_back(std::move(ef));
        }
        exp.provenance.push_back(
            {"BwReadRsdsDetail",
             "/sap/bw/modeling/rsds/" + name + "/" + source_sys + "/" + opts.version,
             "ok"});

    } else if (type == "TRFN") {
        auto res = BwReadTransformation(session, name, opts.version);
        if (res.IsErr()) {
            exp.warnings.push_back("TRFN " + name + ": " + res.Error().message);
            return;
        }
        const auto& d = res.Value();
        obj.description = d.description;
        obj.trfn_source_name = d.source_name;
        obj.trfn_source_type = d.source_type;
        obj.trfn_target_name = d.target_name;
        obj.trfn_target_type = d.target_type;
        exp.provenance.push_back(
            {"BwReadTransformation",
             "/sap/bw/modeling/trfn/" + name + "/" + opts.version,
             "ok"});

    } else if (type == "DTPA") {
        auto dres = BwReadDtpDetail(session, name, opts.version);
        if (dres.IsErr()) {
            exp.warnings.push_back("DTPA " + name + ": " + dres.Error().message);
            return;
        }
        const auto& d = dres.Value();
        obj.description = d.description;
        obj.dtp_source_name = d.source_name;
        obj.dtp_source_type = d.source_type;
        obj.dtp_target_name = d.target_name;
        obj.dtp_target_type = d.target_type;
        exp.provenance.push_back(
            {"BwReadDtpDetail",
             "/sap/bw/modeling/dtpa/" + name + "/" + opts.version,
             "ok"});

        if (opts.include_lineage) {
            BwLineageGraphOptions lg;
            lg.dtp_name = name;
            lg.version = opts.version;
            lg.include_xref = false;  // avoid slow xref per DTP in batch export
            auto lres = BwBuildLineageGraph(session, lg);
            if (lres.IsErr()) {
                exp.warnings.push_back("DTPA lineage " + name + ": " +
                                       lres.Error().message);
            } else {
                obj.lineage = lres.Value();
                for (const auto& w : lres.Value().warnings) {
                    exp.warnings.push_back("DTPA lineage " + name + ": " + w);
                }
            }
        }

    } else if (type == "QUERY") {
        if (opts.include_queries) {
            auto qres = BwAssembleQueryGraph(session, "query", name, opts.version);
            if (qres.IsErr()) {
                exp.warnings.push_back("QUERY " + name + ": " + qres.Error().message);
            } else {
                obj.query_graph = qres.Value();
            }
        }
        exp.provenance.push_back(
            {"BwAssembleQueryGraph",
             "/sap/bw/modeling/query/" + name + "/" + opts.version,
             opts.include_queries ? "ok" : "skipped"});

    } else {
        // Types with no known standalone ADT detail endpoint: metadata from
        // BFS nodes or search results is already sufficient — skip BwReadObject.
        static const std::set<std::string> kNoDetailTypes = {
            "CUBE", "MPRO", "HCPR", "ELEM", "IOBJ",
        };
        if (kNoDetailTypes.count(type)) {
            return;
        }

        // Fallback: read generic metadata only
        BwReadOptions ro;
        ro.object_type = type;
        ro.object_name = name;
        ro.version = opts.version;
        if (!node.uri.empty()) ro.uri = node.uri;
        auto mres = BwReadObject(session, ro);
        if (mres.IsErr()) {
            exp.warnings.push_back(type + " " + name + ": " + mres.Error().message);
            return;
        }
        const auto& m = mres.Value();
        obj.description = m.description;
        obj.package_name = m.package_name;
        obj.status = m.status;
        exp.provenance.push_back(
            {"BwReadObject",
             "/sap/bw/modeling/" + type + "/" + name + "/" + opts.version,
             "ok"});
    }
}

// ---------------------------------------------------------------------------
// BuildDataflowGraph — merge all DTP lineage graphs into exp.dataflow_*
// ---------------------------------------------------------------------------
void BuildDataflowGraph(BwInfoareaExport& exp) {
    std::set<std::string> seen_nodes;
    std::set<std::string> seen_edges;

    for (const auto& obj : exp.objects) {
        if (!obj.lineage.has_value()) continue;
        for (const auto& n : obj.lineage->nodes) {
            if (!n.id.empty() && seen_nodes.insert(n.id).second) {
                exp.dataflow_nodes.push_back(n);
            }
        }
        for (const auto& e : obj.lineage->edges) {
            if (!e.id.empty() && seen_edges.insert(e.id).second) {
                exp.dataflow_edges.push_back(e);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// CollectInfoproviderXrefEdges — for each CUBE/MPRO/ADSO/HCPR in exp.objects,
// call the xref API to get consuming queries and add them as dataflow edges.
// ---------------------------------------------------------------------------
void CollectInfoproviderXrefEdges(IAdtSession& session, BwInfoareaExport& exp) {
    static const std::set<std::string> kInfoproviderTypes = {
        "CUBE", "MPRO", "HCPR", "ADSO", "DSO",
    };

    // Build index of objects already known (type:name → ptr)
    std::map<std::string, bool> known_keys;
    for (const auto& obj : exp.objects) {
        known_keys[obj.type + ":" + obj.name] = true;
    }

    // Track edges already added
    std::set<std::string> seen_edges;
    for (const auto& e : exp.dataflow_edges) {
        seen_edges.insert(e.from + "->" + e.to);
    }
    int edge_idx = static_cast<int>(exp.dataflow_edges.size());

    // Snapshot the infoproviders we want to iterate (don't iterate while appending)
    std::vector<BwExportedObject> providers;
    for (const auto& obj : exp.objects) {
        if (kInfoproviderTypes.count(obj.type)) {
            providers.push_back(obj);
        }
    }

    for (const auto& prov : providers) {
        BwXrefOptions xref;
        xref.object_type = prov.type;
        xref.object_name = prov.name;
        xref.object_version = "A";

        const std::string prov_ep = "/sap/bw/modeling/repo/is/xref?objectType=" +
                                    prov.type + "&objectName=" + prov.name;
        auto xres = BwGetXrefs(session, xref);
        if (xres.IsErr()) {
            exp.warnings.push_back("xref " + prov.type + " " + prov.name + ": " +
                                   xres.Error().message);
            exp.provenance.push_back({"BwGetXrefs", prov_ep, "error"});
            continue;
        }
        exp.provenance.push_back({"BwGetXrefs", prov_ep, "ok"});

        for (const auto& entry : xres.Value()) {
            if (entry.name.empty() || entry.type.empty()) continue;

            // Add the xref target as an object if not yet in the export.
            std::string key = entry.type + ":" + entry.name;
            if (!known_keys.count(key)) {
                BwExportedObject new_obj;
                new_obj.name = entry.name;
                new_obj.type = entry.type;
                new_obj.status = entry.status;
                new_obj.description = entry.description;
                new_obj.uri = entry.uri;
                exp.objects.push_back(std::move(new_obj));
                known_keys[key] = true;
            }

            // Add edge infoprovider → consumer
            std::string edge_key = prov.name + "->" + entry.name;
            if (seen_edges.insert(edge_key).second) {
                BwLineageEdge edge;
                edge.id = "edge:xref:" + std::to_string(++edge_idx);
                edge.from = prov.name;
                edge.to = entry.name;
                edge.type = "xref";
                exp.dataflow_edges.push_back(std::move(edge));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// CollectOrphanElemEdges — for each ELEM that has no incoming dataflow edge,
// fetch its query XML and parse providerName to derive the missing edge.
//
// This covers two cases xref alone cannot handle:
//   1. ELEM→infoprovider edges where the provider was not in kInfoproviderTypes
//      (e.g., an ELEM whose provider is itself another reusable ELEM/QUERY).
//   2. CKF/RKF that reference a CUBE/MPRO directly (the xref is CUBE→consumers,
//      which may not enumerate all CKF/RKF subtypes on every SAP version).
// ---------------------------------------------------------------------------
void CollectOrphanElemEdges(IAdtSession& session,
                             const std::string& version,
                             BwInfoareaExport& exp) {
    // Subtype → BwReadQueryComponent component_type mapping
    auto SubtypeToComponentType = [](const std::string& subtype) -> std::string {
        if (subtype == "REP")  return "QUERY";
        if (subtype == "CKF")  return "CKF";
        if (subtype == "RKF")  return "RKF";
        if (subtype == "VAR")  return "VARIABLE";
        if (subtype == "FILT") return "FILTER";
        if (subtype == "STR")  return "STRUCTURE";
        // Unknown subtypes: attempt QUERY (most common for ELEM)
        return "QUERY";
    };

    // Build set of names that already have incoming edges.
    std::set<std::string> has_incoming;
    for (const auto& e : exp.dataflow_edges) {
        has_incoming.insert(e.to);
    }

    // Build object index and edge dedup set.
    std::map<std::string, bool> known_keys;
    for (const auto& obj : exp.objects) {
        known_keys[obj.type + ":" + obj.name] = true;
    }
    std::set<std::string> seen_edges;
    for (const auto& e : exp.dataflow_edges) {
        seen_edges.insert(e.from + "->" + e.to);
    }
    int edge_idx = static_cast<int>(exp.dataflow_edges.size());

    // Map a BwQueryComponentRef to a BwQueryIobjRef role string.
    // Returns empty string for refs that should be skipped.
    auto RefToIobjRole = [](const BwQueryComponentRef& ref) -> std::string {
        if (ref.name.empty()) return "";
        const auto& t = ref.type;
        if (t == "DIMENSION")   return "dimension";
        if (t == "FILTER_FIELD") return "filter";
        // subComponents: Qry:Variable, variable, VARIABLE, etc.
        if (t.find("ariable") != std::string::npos) return "variable";
        // Key figures: token-based (KEY_FIGURE), restricted/calculated (RKF, CKF), or normalized xsi:type variants.
        if (t == "KEY_FIGURE" || t == "RKF" || t == "CKF" || t.find("KEYFIG") != std::string::npos) return "key_figure";
        // Skip MEMBER (characteristic value hints) and unrecognised refs.
        return "";
    };

    // Snapshot ALL ELEM objects to iterate.
    // - iobj_refs are collected for every ELEM (including those already covered by xref).
    // - Provider edges are only added for ELEMs that have no incoming edge yet.
    // exp.objects may grow during the loop (provider-less elems are appended to the snapshot,
    // not to exp.objects), so snapshot once up front.
    std::vector<std::pair<size_t, BwExportedObject>> elems;  // index into exp.objects + copy
    for (size_t i = 0; i < exp.objects.size(); ++i) {
        const auto& obj = exp.objects[i];
        if (obj.type == "ELEM" && !obj.name.empty()) {
            elems.emplace_back(i, obj);
        }
    }

    for (auto& [obj_idx, elem] : elems) {
        const auto comp_type = SubtypeToComponentType(elem.subtype);
        const std::string ep = "/sap/bw/modeling/query/" + elem.name + "/" + version;

        auto res = BwReadQueryComponent(session, comp_type, elem.name, version, "");
        if (res.IsErr()) {
            exp.warnings.push_back("elem-provider " + elem.name + ": " + res.Error().message);
            exp.provenance.push_back({"BwReadQueryComponent", ep, "error"});
            continue;
        }
        exp.provenance.push_back({"BwReadQueryComponent", ep, "ok"});

        const auto& detail = res.Value();

        // --- Harvest iobj_refs from detail.references ---
        std::set<std::string> seen_iobj;
        std::vector<BwQueryIobjRef> iobj_refs;
        for (const auto& ref : detail.references) {
            const auto role = RefToIobjRole(ref);
            if (role.empty()) continue;
            std::string key = role + ":" + ref.name;
            if (seen_iobj.insert(key).second) {
                iobj_refs.push_back({ref.name, role});
            }
        }
        // Write back into the live exp.objects entry.
        if (!iobj_refs.empty()) {
            exp.objects[obj_idx].iobj_refs = std::move(iobj_refs);
        }

        // --- Provider edge (orphan ELEMs only) ---
        if (detail.info_provider.empty()) continue;
        if (has_incoming.count(elem.name)) continue;

        const std::string provider = detail.info_provider;
        bool provider_known = known_keys.count("CUBE:" + provider) ||
                              known_keys.count("MPRO:" + provider) ||
                              known_keys.count("ADSO:" + provider) ||
                              known_keys.count("HCPR:" + provider) ||
                              known_keys.count("ELEM:" + provider) ||
                              known_keys.count("RSDS:" + provider);
        if (!provider_known) continue;

        std::string edge_key = provider + "->" + elem.name;
        if (seen_edges.insert(edge_key).second) {
            BwLineageEdge edge;
            edge.id = "edge:elem:" + std::to_string(++edge_idx);
            edge.from = provider;
            edge.to = elem.name;
            edge.type = "elem-provider";
            exp.dataflow_edges.push_back(std::move(edge));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// BwExportInfoarea
// ---------------------------------------------------------------------------
Result<BwInfoareaExport, Error> BwExportInfoarea(
    IAdtSession& session,
    const BwExportOptions& options) {

    if (options.infoarea_name.empty()) {
        return Result<BwInfoareaExport, Error>::Err(Error{
            "BwExportInfoarea", "", std::nullopt,
            "infoarea_name must not be empty",
            std::nullopt, ErrorCategory::Internal});
    }

    BwInfoareaExport exp;
    exp.infoarea = options.infoarea_name;
    exp.exported_at = UtcTimestampNow();

    // BFS over infoarea tree.
    // Containers: AREA and semanticalFolder — both may have child objects.
    // semanticalFolder nodes use endpoint_override since they have no standalone
    // infoproviderstructure/{type}/{name} URL; their URI IS the structure endpoint.
    struct QueueEntry {
        std::string area_name;
        std::optional<std::string> endpoint_override;
        int depth;
    };
    std::queue<QueueEntry> q;
    q.push({options.infoarea_name, std::nullopt, 0});
    std::set<std::string> visited_endpoints;
    visited_endpoints.insert(options.infoarea_name);

    while (!q.empty()) {
        auto entry = q.front();
        q.pop();
        const auto& area_name = entry.area_name;
        const int depth = entry.depth;

        BwNodesOptions no;
        no.object_type = "AREA";
        no.object_name = area_name;
        if (entry.endpoint_override.has_value()) {
            no.endpoint_override = entry.endpoint_override;
        }

        std::string prov_endpoint = entry.endpoint_override.value_or(
            "/sap/bw/modeling/repo/infoproviderstructure/AREA/" + area_name);
        exp.provenance.push_back({"BwGetNodes", prov_endpoint, "ok"});

        auto nodes_res = BwGetNodes(session, no);
        if (nodes_res.IsErr()) {
            exp.warnings.push_back("GetNodes " + area_name + ": " +
                                   nodes_res.Error().message);
            continue;
        }

        for (const auto& node : nodes_res.Value()) {
            // Recurse into container types: AREA and semanticalFolder
            if (node.type == "AREA" || node.type == "semanticalFolder") {
                // Use URI as dedup key for semanticalFolder (name may collide)
                std::string key = node.uri.empty() ? node.name : node.uri;
                if (depth + 1 <= options.max_depth &&
                    visited_endpoints.find(key) == visited_endpoints.end()) {
                    visited_endpoints.insert(key);
                    std::optional<std::string> ep_override;
                    if (!node.uri.empty() && node.type == "semanticalFolder") {
                        ep_override = node.uri;
                    }
                    q.push({node.name, ep_override, depth + 1});
                }
                continue;
            }

            // Apply types filter
            if (!TypeMatches(node.type, options.types_filter)) continue;

            BwExportedObject obj;
            obj.name = node.name;
            obj.type = node.type;
            obj.subtype = node.subtype;
            obj.status = node.status;
            obj.description = node.description;
            obj.uri = node.uri;

            CollectObjectDetail(session, node, options, obj, exp);
            exp.objects.push_back(std::move(obj));
        }
    }

    // Search supplement: use BW search filtered by infoArea to find IOBJ/ELEM
    // and any other objects not part of the InfoProvider tree.
    if (options.include_search_supplement) {
        std::set<std::string> found_keys;
        for (const auto& obj : exp.objects) {
            found_keys.insert(obj.type + ":" + obj.name);
        }

        BwSearchOptions sopts;
        sopts.query = "*";
        sopts.info_area = options.infoarea_name;
        sopts.max_results = 500;

        std::string search_ep = "/sap/bw/modeling/repo/is/bwsearch?infoArea=" +
                                options.infoarea_name;
        auto search_res = BwSearchObjects(session, sopts);
        if (search_res.IsErr()) {
            exp.warnings.push_back("search supplement: " +
                                   search_res.Error().message);
            exp.provenance.push_back({"BwSearchObjects", search_ep, "error"});
        } else {
            exp.provenance.push_back({"BwSearchObjects", search_ep, "ok"});
            // The search supplement is intended to recover ELEM/IOBJ objects
            // that the BFS tree misses (queries, info objects).  Infoprovider
            // types (CUBE, MPRO, ADSO, …) returned by the search belong to
            // other infoareas — admitting them would cascade into hundreds of
            // extra xref calls and pollute the diagram.
            static const std::set<std::string> kSearchAllowedTypes = {
                "ELEM", "IOBJ",
            };
            for (const auto& sr : search_res.Value()) {
                if (sr.type == "AREA" || sr.type == "semanticalFolder") continue;
                if (!kSearchAllowedTypes.count(sr.type)) continue;
                if (!TypeMatches(sr.type, options.types_filter)) continue;
                std::string key = sr.type + ":" + sr.name;
                if (found_keys.count(key)) continue;
                found_keys.insert(key);

                BwNodeEntry node;
                node.name = sr.name;
                node.type = sr.type;
                node.subtype = sr.subtype;
                node.description = sr.description;
                node.status = sr.status;
                node.uri = sr.uri;

                BwExportedObject obj;
                obj.name = node.name;
                obj.type = node.type;
                obj.subtype = node.subtype;
                obj.status = node.status;
                obj.description = node.description;
                obj.uri = node.uri;

                CollectObjectDetail(session, node, options, obj, exp);
                exp.objects.push_back(std::move(obj));
            }
        }
    }

    if (options.include_xref_edges) {
        CollectInfoproviderXrefEdges(session, exp);
    }
    if (options.include_elem_provider_edges) {
        CollectOrphanElemEdges(session, options.version, exp);
    }
    BuildDataflowGraph(exp);
    return Result<BwInfoareaExport, Error>::Ok(std::move(exp));
}

// ---------------------------------------------------------------------------
// BwRenderExportCatalogJson
// ---------------------------------------------------------------------------
std::string BwRenderExportCatalogJson(const BwInfoareaExport& exp) {
    nlohmann::json j;
    j["schema_version"] = exp.schema_version;
    j["contract"] = exp.contract;
    j["infoarea"] = exp.infoarea;
    j["exported_at"] = exp.exported_at;

    nlohmann::json objects = nlohmann::json::array();
    for (const auto& obj : exp.objects) {
        nlohmann::json oj;
        oj["name"] = obj.name;
        oj["type"] = obj.type;
        if (!obj.subtype.empty()) oj["subtype"] = obj.subtype;
        if (!obj.status.empty()) oj["status"] = obj.status;
        if (!obj.description.empty()) oj["description"] = obj.description;
        if (!obj.package_name.empty()) oj["package_name"] = obj.package_name;
        if (!obj.uri.empty()) oj["uri"] = obj.uri;

        // Fields
        if (!obj.fields.empty()) {
            nlohmann::json fields = nlohmann::json::array();
            for (const auto& f : obj.fields) {
                nlohmann::json fj;
                fj["name"] = f.name;
                if (!f.description.empty()) fj["description"] = f.description;
                if (!f.data_type.empty()) fj["data_type"] = f.data_type;
                if (!f.info_object.empty()) fj["info_object"] = f.info_object;
                if (!f.segment_id.empty()) fj["segment_id"] = f.segment_id;
                if (f.length > 0) fj["length"] = f.length;
                if (f.decimals > 0) fj["decimals"] = f.decimals;
                fj["key"] = f.key;
                fields.push_back(std::move(fj));
            }
            oj["fields"] = std::move(fields);
        }

        // DTPA
        if (!obj.dtp_source_name.empty()) {
            oj["source"] = {{"name", obj.dtp_source_name},
                            {"type", obj.dtp_source_type}};
            oj["target"] = {{"name", obj.dtp_target_name},
                            {"type", obj.dtp_target_type}};
        }
        if (obj.lineage.has_value()) {
            nlohmann::json lg;
            lg["dtp"] = obj.name;
            nlohmann::json lnodes = nlohmann::json::array();
            for (const auto& n : obj.lineage->nodes) {
                lnodes.push_back(
                    {{"id", n.id}, {"type", n.type}, {"name", n.name}});
            }
            lg["nodes"] = std::move(lnodes);
            nlohmann::json ledges = nlohmann::json::array();
            for (const auto& e : obj.lineage->edges) {
                ledges.push_back(
                    {{"id", e.id}, {"from", e.from}, {"to", e.to},
                     {"type", e.type}});
            }
            lg["edges"] = std::move(ledges);
            oj["lineage"] = std::move(lg);
        }

        // TRFN
        if (!obj.trfn_source_name.empty()) {
            oj["source"] = {{"name", obj.trfn_source_name},
                            {"type", obj.trfn_source_type}};
            oj["target"] = {{"name", obj.trfn_target_name},
                            {"type", obj.trfn_target_type}};
        }

        // QUERY
        if (obj.query_graph.has_value()) {
            oj["query_info_provider"] = obj.query_info_provider;
            oj["query_node_count"] =
                static_cast<int>(obj.query_graph->nodes.size());
        }

        // IOBJ refs (dimensions, filters, variables used by this query)
        if (!obj.iobj_refs.empty()) {
            nlohmann::json refs = nlohmann::json::array();
            for (const auto& r : obj.iobj_refs) {
                refs.push_back({{"name", r.name}, {"role", r.role}});
            }
            oj["iobj_refs"] = std::move(refs);
        }

        objects.push_back(std::move(oj));
    }
    j["objects"] = std::move(objects);

    // Dataflow section
    {
        nlohmann::json df;
        nlohmann::json dnodes = nlohmann::json::array();
        for (const auto& n : exp.dataflow_nodes) {
            dnodes.push_back(
                {{"id", n.id}, {"type", n.type}, {"name", n.name},
                 {"role", n.role}});
        }
        df["nodes"] = std::move(dnodes);
        nlohmann::json dedges = nlohmann::json::array();
        for (const auto& e : exp.dataflow_edges) {
            dedges.push_back(
                {{"id", e.id}, {"from", e.from}, {"to", e.to},
                 {"type", e.type}});
        }
        df["edges"] = std::move(dedges);
        j["dataflow"] = std::move(df);
    }

    j["warnings"] = exp.warnings;

    nlohmann::json prov = nlohmann::json::array();
    for (const auto& p : exp.provenance) {
        prov.push_back({{"operation", p.operation},
                        {"endpoint", p.endpoint},
                        {"status", p.status}});
    }
    j["provenance"] = std::move(prov);

    return j.dump(2);
}

// ---------------------------------------------------------------------------
// BwRenderExportOpenMetadataJson
// ---------------------------------------------------------------------------
std::string BwRenderExportOpenMetadataJson(
    const BwInfoareaExport& exp,
    const std::string& service_name,
    const std::string& system_id) {

    // FQN pattern: service.system.infoarea.object
    auto fqn = [&](const std::string& name) -> std::string {
        std::string s = service_name;
        if (!system_id.empty()) s += "." + system_id;
        s += "." + exp.infoarea + "." + name;
        return s;
    };

    nlohmann::json root;
    root["serviceType"] = "SapBw";
    root["serviceName"] = service_name;
    root["systemId"] = system_id;
    root["infoarea"] = exp.infoarea;
    root["exportedAt"] = exp.exported_at;

    nlohmann::json tables = nlohmann::json::array();
    for (const auto& obj : exp.objects) {
        if (obj.type != "ADSO" && obj.type != "RSDS") continue;
        nlohmann::json t;
        t["name"] = obj.name;
        t["fullyQualifiedName"] = fqn(obj.name);
        t["tableType"] = obj.type == "ADSO" ? "Regular" : "External";
        if (!obj.description.empty()) t["description"] = obj.description;
        nlohmann::json cols = nlohmann::json::array();
        for (const auto& f : obj.fields) {
            nlohmann::json c;
            c["name"] = f.name;
            c["dataType"] = f.data_type.empty() ? "VARCHAR" : f.data_type;
            if (!f.description.empty()) c["description"] = f.description;
            c["constraint"] = f.key ? "PRIMARY_KEY" : "NULL";
            cols.push_back(std::move(c));
        }
        t["columns"] = std::move(cols);
        tables.push_back(std::move(t));
    }
    root["tables"] = std::move(tables);

    // Lineage edges: DTP connections as FQN → FQN
    nlohmann::json lineage = nlohmann::json::array();
    for (const auto& obj : exp.objects) {
        if (obj.type != "DTPA") continue;
        if (obj.dtp_source_name.empty() || obj.dtp_target_name.empty()) continue;
        nlohmann::json le;
        le["fromEntity"] = fqn(obj.dtp_source_name);
        le["toEntity"] = fqn(obj.dtp_target_name);
        le["via"] = obj.name;
        le["viaType"] = "DTP";
        lineage.push_back(std::move(le));
    }
    root["lineage"] = std::move(lineage);

    root["warnings"] = exp.warnings;

    return root.dump(2);
}

// ---------------------------------------------------------------------------
// BwRenderExportMermaid
// ---------------------------------------------------------------------------
std::string BwRenderExportMermaid(const BwInfoareaExport& exp,
                                   const BwMermaidOptions& mopts) {
    std::ostringstream out;
    out << "%%{init: {'flowchart': {'curve': 'basis'}}}%%\n";
    out << "graph LR\n";
    out << "\n";

    // Build set of nodes that participate in edges — these must be rendered.
    std::set<std::string> edge_nodes;
    for (const auto& e : exp.dataflow_edges) {
        if (!e.from.empty()) edge_nodes.insert(e.from);
        if (!e.to.empty()) edge_nodes.insert(e.to);
    }

    // Types that are internal plumbing or raw attribute objects.
    // ELEM is only skipped if it has no edges; with edges it becomes a Query node.
    auto is_infrastructure = [&](const BwExportedObject& obj) {
        if (obj.type == "DTPA" || obj.type == "TRFN" || obj.type == "IOBJ") {
            return true;
        }
        if (obj.type == "ELEM") {
            // Show as a query only when it participates in the dataflow graph.
            return edge_nodes.find(obj.name) == edge_nodes.end();
        }
        return false;
    };

    // Collect dataflow-relevant objects by category.
    // CUBE/HCPR → InfoCubes, MPRO/VRRC → MultiProviders,
    // RSDS → Sources, ADSO/DSO → Staging, QUERY/ELEM(with edges) → Queries.
    std::vector<const BwExportedObject*> sources, staging, cubes, mpros, queries;
    for (const auto& obj : exp.objects) {
        if (is_infrastructure(obj)) continue;
        if (obj.type == "RSDS") {
            sources.push_back(&obj);
        } else if (obj.type == "ADSO" || obj.type == "DSO") {
            staging.push_back(&obj);
        } else if (obj.type == "CUBE" || obj.type == "HCPR") {
            cubes.push_back(&obj);
        } else if (obj.type == "MPRO" || obj.type == "VRRC") {
            mpros.push_back(&obj);
        } else if (obj.type == "QUERY" || obj.type == "ELEM") {
            queries.push_back(&obj);
        }
        // All other types are silently omitted from the diagram.
    }

    // Helper: sanitize name for Mermaid node id
    auto node_id = [](const std::string& name) -> std::string {
        std::string id = name;
        for (char& c : id) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        }
        return id;
    };

    // Helper: truncate description for labels
    auto label = [](const std::string& name,
                    const std::string& desc) -> std::string {
        std::string text = name;
        if (!desc.empty() && desc != name) {
            text += "<br/>" + desc.substr(0, 40);
        }
        // Escape internal double-quotes; wrap in quotes for Mermaid
        std::string escaped;
        escaped.reserve(text.size() + 2);
        for (char c : text) {
            if (c == '"') escaped += "#quot;";
            else escaped += c;
        }
        return "\"" + escaped + "\"";
    };

    auto emit_subgraph = [&](const std::string& title,
                              const std::vector<const BwExportedObject*>& objs) {
        if (objs.empty()) return;
        out << "  subgraph " << title << "\n";
        for (const auto* obj : objs) {
            out << "    " << node_id(obj->name) << "["
                << label(obj->name, obj->description) << "]\n";
        }
        out << "  end\n\n";
    };

    emit_subgraph("Sources", sources);
    // Use the infoarea name as the Staging subgraph title so it's visible in the diagram.
    std::string staging_title = staging.empty() ? "" : ("Staging[" + exp.infoarea + "]");
    if (!staging.empty()) {
        out << "  subgraph " << staging_title << "\n";
        for (const auto* obj : staging) {
            out << "    " << node_id(obj->name) << "["
                << label(obj->name, obj->description) << "]\n";
        }
        out << "  end\n\n";
    }
    emit_subgraph("InfoCubes", cubes);
    emit_subgraph("MultiProviders", mpros);
    emit_subgraph("Queries", queries);

    // Optional InfoObjects subgraph: collect unique IOBJs referenced by visible queries.
    if (mopts.iobj_edges) {
        // Gather IOBJs used by query nodes that are in the diagram.
        std::set<std::string> visible_queries;
        for (const auto* q : queries) visible_queries.insert(q->name);

        // iobj_name → description (empty for now — IOBJ metadata not fetched)
        std::map<std::string, std::string> iobj_map;
        // role abbreviations for edge labels
        auto role_label = [](const std::string& role) -> std::string {
            if (role == "dimension") return "dim";
            if (role == "filter")    return "filter";
            if (role == "variable")  return "var";
            if (role == "key_figure") return "kf";
            return role;
        };

        // Collect all referenced IOBJs from visible queries.
        for (const auto& obj : exp.objects) {
            if (!visible_queries.count(obj.name)) continue;
            for (const auto& r : obj.iobj_refs) {
                iobj_map.emplace(r.name, "");
            }
        }

        if (!iobj_map.empty()) {
            out << "  subgraph InfoObjects\n";
            for (const auto& [iobj_name, desc] : iobj_map) {
                out << "    " << node_id(iobj_name) << "["
                    << label(iobj_name, desc) << "]\n";
            }
            out << "  end\n\n";

            // Emit query → iobj edges with role label.
            for (const auto& obj : exp.objects) {
                if (!visible_queries.count(obj.name)) continue;
                for (const auto& r : obj.iobj_refs) {
                    out << "  " << node_id(obj.name)
                        << " -->|" << role_label(r.role) << "| "
                        << node_id(r.name) << "\n";
                }
            }
        }
    }

    // Dataflow edges (provider → query).
    out << "\n";
    if (!exp.dataflow_edges.empty()) {
        for (const auto& edge : exp.dataflow_edges) {
            out << "  " << node_id(edge.from)
                << " --> " << node_id(edge.to) << "\n";
        }
    } else {
        // Fallback: DTP objects with source/target resolved.
        for (const auto& obj : exp.objects) {
            if (obj.type != "DTPA") continue;
            if (obj.dtp_source_name.empty() || obj.dtp_target_name.empty()) continue;
            out << "  " << node_id(obj.dtp_source_name)
                << " -->|" << obj.name << "| "
                << node_id(obj.dtp_target_name) << "\n";
        }
    }

    return out.str();
}

}  // namespace erpl_adt
