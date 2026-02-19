#include <erpl_adt/adt/bw_export.hpp>

#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/bw_nodes.hpp>
#include <erpl_adt/adt/bw_object.hpp>
#include <erpl_adt/adt/bw_query.hpp>
#include <erpl_adt/adt/bw_rsds.hpp>

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
std::string BwRenderExportMermaid(const BwInfoareaExport& exp) {
    std::ostringstream out;
    out << "graph LR\n";
    out << "\n";

    // Collect objects by category
    std::vector<const BwExportedObject*> sources, staging, queries, others;
    for (const auto& obj : exp.objects) {
        if (obj.type == "RSDS") {
            sources.push_back(&obj);
        } else if (obj.type == "ADSO") {
            staging.push_back(&obj);
        } else if (obj.type == "QUERY") {
            queries.push_back(&obj);
        } else {
            others.push_back(&obj);
        }
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
        if (desc.empty() || desc == name) return name;
        // Use name<br/>desc (Mermaid HTML labels require quotes)
        std::string d = desc.substr(0, 40);
        return name + "\\n" + d;
    };

    if (!sources.empty()) {
        out << "  subgraph Sources\n";
        for (const auto* obj : sources) {
            out << "    " << node_id(obj->name) << "["
                << label(obj->name, obj->description) << "]\n";
        }
        out << "  end\n\n";
    }

    if (!staging.empty()) {
        out << "  subgraph Staging[" << exp.infoarea << "]\n";
        for (const auto* obj : staging) {
            out << "    " << node_id(obj->name) << "["
                << label(obj->name, obj->description) << "]\n";
        }
        out << "  end\n\n";
    }

    if (!queries.empty()) {
        out << "  subgraph Queries\n";
        for (const auto* obj : queries) {
            out << "    " << node_id(obj->name) << "["
                << label(obj->name, obj->description) << "]\n";
        }
        out << "  end\n\n";
    }

    // DTP edges
    for (const auto& obj : exp.objects) {
        if (obj.type != "DTPA") continue;
        if (obj.dtp_source_name.empty() || obj.dtp_target_name.empty()) continue;
        out << "  " << node_id(obj.dtp_source_name)
            << " -->|DTP: " << obj.name << "| "
            << node_id(obj.dtp_target_name) << "\n";
    }

    // Other objects not in subgraphs
    for (const auto* obj : others) {
        if (obj->type == "DTPA" || obj->type == "TRFN") continue;
        out << "  " << node_id(obj->name) << "["
            << label(obj->name, obj->description) << "]\n";
    }

    return out.str();
}

}  // namespace erpl_adt
