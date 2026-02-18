#pragma once

#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace erpl_adt {

struct BwQueryComponentRef {
    std::string name;
    std::string type;
    std::string role;
    std::map<std::string, std::string> attributes;
};

struct BwQueryComponentDetail {
    std::string name;
    std::string component_type;  // QUERY, VARIABLE, RKF, CKF, FILTER, STRUCTURE
    std::string description;
    std::string info_provider;
    std::string info_provider_type;
    std::map<std::string, std::string> attributes;
    std::vector<BwQueryComponentRef> references;
};

struct BwQueryGraphNode {
    std::string id;
    std::string type;
    std::string name;
    std::string role;
    std::string label;
    std::map<std::string, std::string> attributes;
};

struct BwQueryGraphEdge {
    std::string id;
    std::string from;
    std::string to;
    std::string type;
    std::string role;
    std::map<std::string, std::string> attributes;
};

struct BwQueryGraph {
    std::string schema_version = "1.0";
    std::string root_node_id;
    std::vector<BwQueryGraphNode> nodes;
    std::vector<BwQueryGraphEdge> edges;
    std::vector<std::string> warnings;
    std::vector<std::string> provenance;
};

struct BwQueryMermaidOptions {
    std::string direction = "TD";   // TD or LR
    std::string layout = "detailed"; // compact or detailed
};

struct BwQueryGraphReduceSummary {
    std::string summary_node_id;
    std::string role;
    std::vector<std::string> omitted_node_ids;
    std::vector<std::string> kept_node_ids;
};

struct BwQueryGraphReduceOptions {
    std::optional<std::string> focus_role;
    size_t max_nodes_per_role = 0;  // 0 disables reduction
};

struct BwQueryGraphReduction {
    bool applied = false;
    std::optional<std::string> focus_role;
    size_t max_nodes_per_role = 0;
    std::vector<BwQueryGraphReduceSummary> summaries;
};

struct BwQueryGraphMetrics {
    size_t node_count = 0;
    size_t edge_count = 0;
    size_t max_out_degree = 0;
    size_t summary_node_count = 0;
    std::vector<std::string> high_fanout_node_ids;
    std::vector<std::string> ergonomics_flags;
};

[[nodiscard]] Result<BwQueryComponentDetail, Error> BwReadQueryComponent(
    IAdtSession& session,
    const std::string& component_type,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

[[nodiscard]] BwQueryGraph BwBuildQueryGraph(
    const BwQueryComponentDetail& detail);

[[nodiscard]] std::string BwRenderQueryGraphMermaid(
    const BwQueryGraph& graph,
    const BwQueryMermaidOptions& options = {});

[[nodiscard]] std::pair<BwQueryGraph, BwQueryGraphReduction> BwReduceQueryGraph(
    const BwQueryGraph& graph,
    const BwQueryGraphReduceOptions& options);

[[nodiscard]] BwQueryGraph BwMergeQueryAndLineageGraphs(
    const BwQueryGraph& query_graph,
    const BwQueryComponentDetail& query_detail,
    const BwLineageGraph& lineage_graph);

[[nodiscard]] BwQueryGraphMetrics BwAnalyzeQueryGraph(const BwQueryGraph& graph);

[[nodiscard]] Result<BwQueryGraph, Error> BwAssembleQueryGraph(
    IAdtSession& session,
    const std::string& component_type,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

[[nodiscard]] Result<BwQueryGraph, Error> BwAssembleQueryGraph(
    IAdtSession& session,
    const BwQueryComponentDetail& root_detail,
    const std::string& version = "a");

}  // namespace erpl_adt
