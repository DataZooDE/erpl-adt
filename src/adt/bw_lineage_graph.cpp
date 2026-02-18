#include <erpl_adt/adt/bw_lineage_graph.hpp>

#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_rsds.hpp>
#include <erpl_adt/adt/bw_search.hpp>
#include <erpl_adt/adt/bw_xref.hpp>

#include <set>
#include <string>

namespace erpl_adt {

namespace {

std::string ObjectNodeId(const std::string& type, const std::string& name) {
    return "obj:" + type + ":" + name;
}

std::string FieldNodeId(const std::string& object_type, const std::string& object_name,
                        const std::string& field_name) {
    return "field:" + object_type + ":" + object_name + ":" + field_name;
}

void AddNode(BwLineageGraph& graph, std::set<std::string>& seen,
             BwLineageNode node) {
    if (node.id.empty() || seen.count(node.id) > 0) {
        return;
    }
    seen.insert(node.id);
    graph.nodes.push_back(std::move(node));
}

void AddEdge(BwLineageGraph& graph, std::set<std::string>& seen,
             BwLineageEdge edge) {
    if (edge.id.empty() || seen.count(edge.id) > 0) {
        return;
    }
    seen.insert(edge.id);
    graph.edges.push_back(std::move(edge));
}

std::string ResolveTransformationName(IAdtSession& session,
                                      const BwLineageGraphOptions& options,
                                      const BwDtpDetail& dtp,
                                      BwLineageGraph& graph) {
    if (options.trfn_name.has_value() && !options.trfn_name->empty()) {
        return *options.trfn_name;
    }

    BwSearchOptions search;
    search.query = "*";
    search.max_results = 20;
    search.object_type = "TRFN";
    if (!dtp.target_name.empty() && !dtp.target_type.empty()) {
        search.depends_on_name = dtp.target_name;
        search.depends_on_type = dtp.target_type;
    }

    auto result = BwSearchObjects(session, search);
    if (result.IsErr()) {
        graph.warnings.push_back(
            "TRFN lookup via BW search failed; continuing with partial lineage");
        graph.provenance.push_back(
            {"BwSearchObjects",
             "/sap/bw/modeling/repo/is/bwsearch?objectType=TRFN",
             "partial"});
        return "";
    }

    graph.provenance.push_back(
        {"BwSearchObjects",
         "/sap/bw/modeling/repo/is/bwsearch?objectType=TRFN",
         "ok"});
    if (result.Value().empty()) {
        graph.warnings.push_back(
            "No TRFN discovered for DTP target; continuing with partial lineage");
        return "";
    }
    return result.Value().front().name;
}

}  // namespace

Result<BwLineageGraph, Error> BwBuildLineageGraph(
    IAdtSession& session,
    const BwLineageGraphOptions& options) {
    if (options.dtp_name.empty()) {
        return Result<BwLineageGraph, Error>::Err(Error{
            "BwBuildLineageGraph",
            "",
            std::nullopt,
            "dtp_name must not be empty",
            std::nullopt,
            ErrorCategory::Internal});
    }

    BwLineageGraph graph;
    graph.root_name = options.dtp_name;

    std::set<std::string> node_ids;
    std::set<std::string> edge_ids;

    auto dtp_result = BwReadDtpDetail(session, options.dtp_name, options.version);
    if (dtp_result.IsErr()) {
        return Result<BwLineageGraph, Error>::Err(std::move(dtp_result).Error());
    }
    graph.provenance.push_back(
        {"BwReadDtpDetail",
         "/sap/bw/modeling/dtpa/" + options.dtp_name + "/" + options.version,
         "ok"});
    const auto& dtp = dtp_result.Value();

    const auto dtp_node_id = ObjectNodeId("DTPA", dtp.name);
    AddNode(graph, node_ids, BwLineageNode{
                             dtp_node_id,
                             "DTPA",
                             dtp.name,
                             "dtp",
                             "",
                             options.version,
                             {{"description", dtp.description}}});

    const auto src_node_id = ObjectNodeId(dtp.source_type, dtp.source_name);
    AddNode(graph, node_ids, BwLineageNode{
                             src_node_id,
                             dtp.source_type,
                             dtp.source_name,
                             "source_object",
                             "",
                             options.version,
                             {{"source_system", dtp.source_system}}});

    const auto tgt_node_id = ObjectNodeId(dtp.target_type, dtp.target_name);
    AddNode(graph, node_ids, BwLineageNode{
                             tgt_node_id,
                             dtp.target_type,
                             dtp.target_name,
                             "target_object",
                             "",
                             options.version,
                             {}});

    AddEdge(graph, edge_ids, BwLineageEdge{
                             "edge:dtp_source",
                             src_node_id,
                             dtp_node_id,
                             "dtp_source",
                             {}});
    AddEdge(graph, edge_ids, BwLineageEdge{
                             "edge:dtp_target",
                             dtp_node_id,
                             tgt_node_id,
                             "dtp_target",
                             {}});

    std::set<std::string> rsds_field_names;
    if (dtp.source_type == "RSDS" && !dtp.source_name.empty() &&
        !dtp.source_system.empty()) {
        auto rsds_result = BwReadRsdsDetail(session, dtp.source_name,
                                            dtp.source_system, options.version);
        if (rsds_result.IsErr()) {
            graph.warnings.push_back(
                "RSDS read failed; source field-level lineage is partial");
            graph.provenance.push_back(
                {"BwReadRsdsDetail",
                 "/sap/bw/modeling/rsds/" + dtp.source_name + "/" + dtp.source_system + "/" + options.version,
                 "partial"});
        } else {
            graph.provenance.push_back(
                {"BwReadRsdsDetail",
                 "/sap/bw/modeling/rsds/" + dtp.source_name + "/" + dtp.source_system + "/" + options.version,
                 "ok"});
            for (const auto& f : rsds_result.Value().fields) {
                const auto id = FieldNodeId("RSDS", dtp.source_name, f.name);
                AddNode(graph, node_ids, BwLineageNode{
                                         id,
                                         "RSDS_FIELD",
                                         f.name,
                                         "rsds_field",
                                         "",
                                         options.version,
                                         {{"data_type", f.data_type},
                                          {"key", f.key ? "true" : "false"},
                                          {"segment_id", f.segment_id}}});
                rsds_field_names.insert(f.name);
                AddEdge(graph, edge_ids, BwLineageEdge{
                                         "edge:rsds_field:" + f.name,
                                         src_node_id,
                                         id,
                                         "contains_field",
                                         {}});
            }
        }
    }

    const auto trfn_name = ResolveTransformationName(session, options, dtp, graph);
    if (!trfn_name.empty()) {
        auto trfn_result = BwReadTransformation(session, trfn_name, options.version);
        if (trfn_result.IsErr()) {
            graph.warnings.push_back(
                "TRFN read failed; continuing with DTP-only lineage");
            graph.provenance.push_back(
                {"BwReadTransformation",
                 "/sap/bw/modeling/trfn/" + trfn_name + "/" + options.version,
                 "partial"});
        } else {
            graph.provenance.push_back(
                {"BwReadTransformation",
                 "/sap/bw/modeling/trfn/" + trfn_name + "/" + options.version,
                 "ok"});
            const auto& trfn = trfn_result.Value();
            const auto trfn_node_id = ObjectNodeId("TRFN", trfn.name);
            AddNode(graph, node_ids, BwLineageNode{
                                     trfn_node_id,
                                     "TRFN",
                                     trfn.name,
                                     "transformation",
                                     "",
                                     options.version,
                                     {{"description", trfn.description}}});

            AddEdge(graph, edge_ids, BwLineageEdge{
                                     "edge:trfn_source",
                                     src_node_id,
                                     trfn_node_id,
                                     "trfn_source",
                                     {}});
            AddEdge(graph, edge_ids, BwLineageEdge{
                                     "edge:trfn_target",
                                     trfn_node_id,
                                     tgt_node_id,
                                     "trfn_target",
                                     {}});

            for (const auto& field : trfn.source_fields) {
                const auto id = FieldNodeId(trfn.source_type, trfn.source_name, field.name);
                AddNode(graph, node_ids, BwLineageNode{
                                         id,
                                         trfn.source_type + "_FIELD",
                                         field.name,
                                         "source_field",
                                         "",
                                         options.version,
                                         {{"field_type", field.type},
                                          {"aggregation", field.aggregation},
                                          {"key", field.key ? "true" : "false"}}});
                if (trfn.source_type == "RSDS" &&
                    rsds_field_names.count(field.name) > 0) {
                    const auto rsds_field_id = FieldNodeId("RSDS", trfn.source_name, field.name);
                    AddEdge(graph, edge_ids, BwLineageEdge{
                                             "edge:rsds_to_trfn_src:" + field.name,
                                             rsds_field_id,
                                             id,
                                             "field_origin",
                                             {}});
                }
            }
            for (const auto& field : trfn.target_fields) {
                const auto id = FieldNodeId(trfn.target_type, trfn.target_name, field.name);
                AddNode(graph, node_ids, BwLineageNode{
                                         id,
                                         trfn.target_type + "_FIELD",
                                         field.name,
                                         "target_field",
                                         "",
                                         options.version,
                                         {{"field_type", field.type},
                                          {"aggregation", field.aggregation},
                                          {"key", field.key ? "true" : "false"}}});
            }

            int mapping_idx = 0;
            for (const auto& rule : trfn.rules) {
                std::vector<std::string> source_fields = rule.source_fields;
                std::vector<std::string> target_fields = rule.target_fields;
                if (source_fields.empty() && !rule.source_field.empty()) {
                    source_fields.push_back(rule.source_field);
                }
                if (target_fields.empty() && !rule.target_field.empty()) {
                    target_fields.push_back(rule.target_field);
                }
                if (target_fields.empty()) {
                    continue;
                }
                if (source_fields.empty()) {
                    for (const auto& target_field : target_fields) {
                        ++mapping_idx;
                        const auto tgt_field_id =
                            FieldNodeId(trfn.target_type, trfn.target_name, target_field);
                        AddEdge(graph, edge_ids, BwLineageEdge{
                                                 "edge:field_derivation:" + std::to_string(mapping_idx),
                                                 trfn_node_id,
                                                 tgt_field_id,
                                                 "field_derivation",
                                                 {{"rule_type", rule.rule_type},
                                                  {"formula", rule.formula},
                                                  {"constant", rule.constant}}});
                    }
                    continue;
                }
                for (const auto& source_field : source_fields) {
                    for (const auto& target_field : target_fields) {
                        ++mapping_idx;
                        const auto src_field_id =
                            FieldNodeId(trfn.source_type, trfn.source_name, source_field);
                        const auto tgt_field_id =
                            FieldNodeId(trfn.target_type, trfn.target_name, target_field);
                        AddEdge(graph, edge_ids, BwLineageEdge{
                                                 "edge:field_mapping:" + std::to_string(mapping_idx),
                                                 src_field_id,
                                                 tgt_field_id,
                                                 "field_mapping",
                                                 {{"rule_type", rule.rule_type},
                                                  {"formula", rule.formula},
                                                  {"constant", rule.constant}}});
                    }
                }
            }
        }
    }

    if (options.include_xref && !dtp.target_type.empty() && !dtp.target_name.empty()) {
        BwXrefOptions xref;
        xref.object_type = dtp.target_type;
        xref.object_name = dtp.target_name;
        xref.object_version = "A";
        xref.max_results = options.max_xref;
        auto xref_result = BwGetXrefs(session, xref);
        if (xref_result.IsErr()) {
            graph.warnings.push_back(
                "XREF read failed; graph excludes downstream references");
            graph.provenance.push_back(
                {"BwGetXrefs",
                 "/sap/bw/modeling/repo/is/xref",
                 "partial"});
        } else {
            graph.provenance.push_back(
                {"BwGetXrefs",
                 "/sap/bw/modeling/repo/is/xref",
                 "ok"});
            int xref_idx = 0;
            for (const auto& item : xref_result.Value()) {
                const auto id = ObjectNodeId(item.type, item.name);
                AddNode(graph, node_ids, BwLineageNode{
                                         id,
                                         item.type,
                                         item.name,
                                         "xref_object",
                                         item.uri,
                                         item.version,
                                         {{"description", item.description}}});
                ++xref_idx;
                AddEdge(graph, edge_ids, BwLineageEdge{
                                         "edge:xref:" + std::to_string(xref_idx),
                                         tgt_node_id,
                                         id,
                                         "xref",
                                         {{"association_type", item.association_type},
                                          {"association_label", item.association_label}}});
            }
        }
    }

    return Result<BwLineageGraph, Error>::Ok(std::move(graph));
}

}  // namespace erpl_adt
