#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_query.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.find_last_of("/\\");
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("BwReadQueryComponent: parses query-family references", "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));

    auto result = BwReadQueryComponent(mock, "QUERY", "ZQ_SALES");
    REQUIRE(result.IsOk());
    const auto& detail = result.Value();
    CHECK(detail.name == "ZQ_SALES");
    CHECK(detail.component_type == "QUERY");
    CHECK(detail.description == "Sales Query");
    CHECK(detail.info_provider == "ZCP_SALES");
    CHECK(detail.info_provider_type == "HCPR");
    REQUIRE(detail.references.size() == 5);
    CHECK(detail.references[0].name == "ZVAR_FISCYEAR");
    CHECK(detail.references[0].type == "VARIABLE");
}

TEST_CASE("BwReadQueryComponent: uses query endpoint for variable-like types", "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));

    auto result = BwReadQueryComponent(mock, "VARIABLE", "ZVAR_FISCYEAR");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    const auto& call = mock.GetCalls()[0];
    CHECK(call.path.find("/sap/bw/modeling/query/zvar_fiscyear/a") != std::string::npos);
    CHECK(call.headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.variable-v1_10_0+xml");
}

TEST_CASE("BwReadQueryComponent: 415 retries downgraded and generic Accept types",
          "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({415, {}, "unsupported"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({415, {}, "unsupported"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));

    auto result = BwReadQueryComponent(mock, "VARIABLE", "ZVAR_FISCYEAR");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 3);
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.variable-v1_10_0+xml");
    CHECK(mock.GetCalls()[1].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.variable-v1_9_0+xml");
    CHECK(mock.GetCalls()[2].headers.at("Accept") == "application/xml");
}

TEST_CASE("BwReadQueryComponent: 415 after all fallbacks includes attempted Accept hint",
          "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({415, {}, "unsupported"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({415, {}, "unsupported"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({415, {}, "unsupported"}));

    auto result = BwReadQueryComponent(mock, "FILTER", "ZFILTER_REGION");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 415);
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("application/vnd.sap.bw.modeling.filter-v1_9_0+xml") !=
          std::string::npos);
    CHECK(result.Error().hint->find("application/vnd.sap.bw.modeling.filter-v1_8_0+xml") !=
          std::string::npos);
    CHECK(result.Error().hint->find("application/xml") != std::string::npos);
}

TEST_CASE("BwReadQueryComponent: invalid type returns validation error", "[adt][bw][query]") {
    MockAdtSession mock;
    auto result = BwReadQueryComponent(mock, "ADSO", "ZQ_SALES");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Unsupported query component type") != std::string::npos);
}

TEST_CASE("BwReadQueryComponent: parses SAP Qry:queryResource structure", "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/live_query_0D_FC_NW_C01_Q0007.xml")}));

    auto result = BwReadQueryComponent(mock, "QUERY", "0D_FC_NW_C01_Q0007");
    REQUIRE(result.IsOk());
    const auto& detail = result.Value();
    CHECK(detail.name == "0D_FC_NW_C01_Q0007");
    CHECK(detail.description == "Monthly Sales by Product Group (Dyn. Date)");
    CHECK(detail.info_provider == "0D_NW_C01");
    CHECK(detail.references.empty() == false);

    bool found_variable = false;
    bool found_dimension = false;
    for (const auto& ref : detail.references) {
        if (ref.type == "VARIABLE" && ref.name == "0D_NW_ACTCMON") {
            found_variable = true;
        }
        if (ref.type == "DIMENSION" && ref.name == "0D_NW_PROD__0D_NW_PRDGP") {
            found_dimension = true;
        }
    }
    CHECK(found_variable);
    CHECK(found_dimension);
}

TEST_CASE("BwBuildQueryGraph: emits normalized nodes/edges contract", "[adt][bw][query]") {
    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.component_type = "QUERY";
    detail.description = "Sales Query";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";
    detail.attributes["foo"] = "bar";
    detail.references.push_back(BwQueryComponentRef{
        "ZVAR_FISCYEAR", "VARIABLE", "subcomponent", {{"xsi:type", "Qry:Variable"}}});
    detail.references.push_back(BwQueryComponentRef{
        "0CALMONTH", "DIMENSION", "columns", {}});

    const auto graph = BwBuildQueryGraph(detail);
    CHECK(graph.schema_version == "1.0");
    CHECK(graph.root_node_id == "Q_QUERY_ZQ_SALES");
    REQUIRE(graph.nodes.size() == 3);
    REQUIRE(graph.edges.size() == 2);
    CHECK(graph.nodes[0].id == graph.root_node_id);
    CHECK(graph.nodes[0].type == "QUERY");
    CHECK(graph.nodes[0].name == "ZQ_SALES");
    CHECK(graph.nodes[0].label.find("Sales Query") != std::string::npos);
    CHECK(graph.edges[0].from == graph.root_node_id);
    CHECK(graph.edges[0].to == "R1");
    CHECK(graph.edges[0].type == "depends_on");
    CHECK(graph.edges[0].role == "subcomponent");
}

TEST_CASE("BwRenderQueryGraphMermaid: deterministic order with escaping and subgraphs",
          "[adt][bw][query]") {
    BwQueryGraph graph;
    graph.root_node_id = "Q_QUERY_ZQ_SALES";
    graph.nodes = {
        {"R2", "DIMENSION", "0CALMONTH", "columns", "DIMENSION: 0CALMONTH", {}},
        {"Q_QUERY_ZQ_SALES", "QUERY", "ZQ_SALES", "root", "ZQ_SALES\\nSales \"Query\"", {}},
        {"R1", "VARIABLE", "ZVAR_FISCYEAR", "subcomponent", "VARIABLE: ZVAR_FISCYEAR", {}},
    };
    graph.edges = {
        {"E2", "Q_QUERY_ZQ_SALES", "R2", "depends_on", "columns", {}},
        {"E1", "Q_QUERY_ZQ_SALES", "R1", "depends_on", "subcomponent", {}},
    };

    BwQueryMermaidOptions options;
    options.layout = "compact";
    const auto mermaid = BwRenderQueryGraphMermaid(graph, options);
    CHECK(mermaid.rfind("graph TD", 0) == 0);
    CHECK(mermaid.find("subgraph Query") != std::string::npos);
    CHECK(mermaid.find("subgraph References") != std::string::npos);
    CHECK(mermaid.find("Sales \\\"Query\\\"") != std::string::npos);
    CHECK(mermaid.find("E1") == std::string::npos);  // Edge IDs are not rendered.

    const auto p_r1 = mermaid.find("R1[");
    const auto p_r2 = mermaid.find("R2[");
    REQUIRE(p_r1 != std::string::npos);
    REQUIRE(p_r2 != std::string::npos);
    CHECK(p_r1 < p_r2);
}

TEST_CASE("BwRenderQueryGraphMermaid: detailed layout emits role subgraphs and LR direction",
          "[adt][bw][query]") {
    BwQueryGraph graph;
    graph.root_node_id = "N_QUERY_ZQ_SALES";
    graph.nodes = {
        {"N_QUERY_ZQ_SALES", "QUERY", "ZQ_SALES", "root", "ZQ_SALES", {}},
        {"N_DIM_0CALMONTH", "DIMENSION", "0CALMONTH", "columns", "DIMENSION: 0CALMONTH", {}},
        {"N_VAR_ZVAR_FY", "VARIABLE", "ZVAR_FY", "subcomponent", "VARIABLE: ZVAR_FY", {}},
        {"N_FIL_ZF", "FILTER", "ZF", "filter", "FILTER: ZF", {}},
    };
    graph.edges = {
        {"E1", "N_QUERY_ZQ_SALES", "N_DIM_0CALMONTH", "depends_on", "columns", {}},
        {"E2", "N_QUERY_ZQ_SALES", "N_VAR_ZVAR_FY", "depends_on", "subcomponent", {}},
        {"E3", "N_VAR_ZVAR_FY", "N_FIL_ZF", "depends_on", "filter", {}},
    };

    BwQueryMermaidOptions options;
    options.layout = "detailed";
    options.direction = "LR";
    const auto mermaid = BwRenderQueryGraphMermaid(graph, options);

    CHECK(mermaid.rfind("graph LR", 0) == 0);
    CHECK(mermaid.find("subgraph Columns") != std::string::npos);
    CHECK(mermaid.find("subgraph Filters") != std::string::npos);
    CHECK(mermaid.find("subgraph Subcomponents") != std::string::npos);
    CHECK(mermaid.find("classDef query") != std::string::npos);
    CHECK(mermaid.find("class N_QUERY_ZQ_SALES query;") != std::string::npos);
}

TEST_CASE("BwReduceQueryGraph: summarizes high-degree role nodes with stable summary ID",
          "[adt][bw][query]") {
    BwQueryGraph graph;
    graph.root_node_id = "N_QUERY_ZQ_SALES";
    graph.nodes = {
        {"N_QUERY_ZQ_SALES", "QUERY", "ZQ_SALES", "root", "ZQ_SALES", {}},
        {"N_FILTER_A", "FILTER", "A", "filter", "FILTER: A", {}},
        {"N_FILTER_B", "FILTER", "B", "filter", "FILTER: B", {}},
        {"N_FILTER_C", "FILTER", "C", "filter", "FILTER: C", {}},
        {"N_COL_1", "DIMENSION", "0CALMONTH", "columns", "DIMENSION: 0CALMONTH", {}},
    };
    graph.edges = {
        {"E1", "N_QUERY_ZQ_SALES", "N_FILTER_A", "depends_on", "filter", {}},
        {"E2", "N_QUERY_ZQ_SALES", "N_FILTER_B", "depends_on", "filter", {}},
        {"E3", "N_QUERY_ZQ_SALES", "N_FILTER_C", "depends_on", "filter", {}},
        {"E4", "N_QUERY_ZQ_SALES", "N_COL_1", "depends_on", "columns", {}},
    };

    BwQueryGraphReduceOptions options;
    options.focus_role = "filter";
    options.max_nodes_per_role = 1;
    const auto reduced = BwReduceQueryGraph(graph, options);

    const auto& reduced_graph = reduced.first;
    const auto& reduction = reduced.second;
    CHECK(reduction.applied);
    CHECK(reduction.focus_role.has_value());
    CHECK(*reduction.focus_role == "filter");
    CHECK(reduction.max_nodes_per_role == 1);
    REQUIRE(reduction.summaries.size() == 1);
    CHECK(reduction.summaries[0].summary_node_id == "S_FILTER_MORE");
    REQUIRE(reduction.summaries[0].omitted_node_ids.size() == 2);
    REQUIRE(reduction.summaries[0].kept_node_ids.size() == 1);

    bool has_summary_node = false;
    bool has_omitted_node = false;
    for (const auto& node : reduced_graph.nodes) {
        if (node.id == "S_FILTER_MORE") {
            has_summary_node = true;
            CHECK(node.attributes.at("synthetic") == "true");
        }
        if (node.id == "N_FILTER_B" || node.id == "N_FILTER_C") {
            has_omitted_node = true;
        }
    }
    CHECK(has_summary_node);
    CHECK_FALSE(has_omitted_node);
}

TEST_CASE("BwMergeQueryAndLineageGraphs: composes upstream nodes and bridge edge",
          "[adt][bw][query]") {
    BwQueryGraph query_graph;
    query_graph.root_node_id = "N_QUERY_ZQ_SALES";
    query_graph.nodes = {
        {"N_QUERY_ZQ_SALES", "QUERY", "ZQ_SALES", "root", "ZQ_SALES", {}},
    };

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.component_type = "QUERY";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    BwLineageGraph lineage;
    lineage.root_type = "DTPA";
    lineage.root_name = "DTP_ZSALES";
    lineage.nodes = {
        {"DTP", "DTPA", "DTP_ZSALES", "root", "/dtp", "A", {}},
        {"TRFN", "TRFN", "TRFN_ZSALES", "transformation", "/trfn", "A", {}},
    };
    lineage.edges = {
        {"LE1", "DTP", "TRFN", "dtp_to_trfn", {}},
    };
    lineage.warnings.push_back("partial xref");

    const auto merged = BwMergeQueryAndLineageGraphs(query_graph, detail, lineage);
    CHECK(merged.provenance.empty() == false);
    CHECK(merged.provenance.back() == "bw.lineage.compose");
    CHECK(merged.warnings.empty() == false);
    CHECK(merged.warnings.back().find("upstream lineage:") != std::string::npos);

    bool has_provider = false;
    bool has_bridge = false;
    bool has_upstream_edge = false;
    for (const auto& n : merged.nodes) {
        if (n.id == "N_PROVIDER_ZCP_SALES") has_provider = true;
    }
    for (const auto& e : merged.edges) {
        if (e.type == "upstream_bridge") has_bridge = true;
        if (e.type == "upstream_lineage") has_upstream_edge = true;
    }
    CHECK(has_provider);
    CHECK(has_bridge);
    CHECK(has_upstream_edge);
}

TEST_CASE("BwMergeQueryAndLineageGraphs: keeps lineage provenance and dedupes repeated branch nodes",
          "[adt][bw][query]") {
    BwQueryGraph query_graph;
    query_graph.root_node_id = "N_QUERY_ZQ_SALES";
    query_graph.nodes = {
        {"N_QUERY_ZQ_SALES", "QUERY", "ZQ_SALES", "root", "ZQ_SALES", {}},
    };

    BwQueryComponentDetail detail;
    detail.name = "ZQ_SALES";
    detail.component_type = "QUERY";
    detail.info_provider = "ZCP_SALES";
    detail.info_provider_type = "HCPR";

    BwLineageGraph lineage;
    lineage.root_type = "DTPA";
    lineage.root_name = "DTP_ZSALES";
    lineage.nodes = {
        {"DTP", "DTPA", "DTP_ZSALES", "root", "/dtp", "A", {}},
        {"TRFN", "TRFN", "TRFN_ZSALES", "transformation", "/trfn", "A", {}},
    };
    lineage.edges = {
        {"LE1", "DTP", "TRFN", "dtp_to_trfn", {}},
    };
    lineage.provenance = {
        {"BwReadDtpDetail", "/sap/bw/modeling/dtpa/DTP_ZSALES/a", "ok"},
        {"BwReadTransformation", "/sap/bw/modeling/trfn/TRFN_ZSALES/a", "ok"},
    };

    auto merged = BwMergeQueryAndLineageGraphs(query_graph, detail, lineage);
    const auto node_count_after_first = merged.nodes.size();
    const auto edge_count_after_first = merged.edges.size();

    merged = BwMergeQueryAndLineageGraphs(merged, detail, lineage);
    CHECK(merged.nodes.size() == node_count_after_first);
    CHECK(merged.edges.size() == edge_count_after_first);

    bool has_lineage_provenance = false;
    for (const auto& p : merged.provenance) {
        if (p.find("lineage:BwReadDtpDetail:ok:/sap/bw/modeling/dtpa/DTP_ZSALES/a") !=
            std::string::npos) {
            has_lineage_provenance = true;
            break;
        }
    }
    CHECK(has_lineage_provenance);
}

TEST_CASE("BwAnalyzeQueryGraph: reports fanout and summary ergonomics flags",
          "[adt][bw][query]") {
    BwQueryGraph graph;
    graph.root_node_id = "ROOT";
    graph.nodes.push_back({"ROOT", "QUERY", "ZQ", "root", "ZQ", {}});
    graph.nodes.push_back({"S_FILTER_MORE", "SUMMARY", "+5", "filter", "SUMMARY", {}});
    for (int i = 0; i < 21; ++i) {
        const auto id = "N" + std::to_string(i);
        graph.nodes.push_back({id, "FILTER", id, "filter", id, {}});
        graph.edges.push_back({"E" + std::to_string(i), "ROOT", id, "depends_on", "filter", {}});
    }

    const auto metrics = BwAnalyzeQueryGraph(graph);
    CHECK(metrics.node_count == graph.nodes.size());
    CHECK(metrics.edge_count == graph.edges.size());
    CHECK(metrics.max_out_degree == 21);
    CHECK(metrics.summary_node_count == 1);
    CHECK_FALSE(metrics.high_fanout_node_ids.empty());
    CHECK(std::find(metrics.ergonomics_flags.begin(), metrics.ergonomics_flags.end(),
                    "high_fanout") != metrics.ergonomics_flags.end());
    CHECK(std::find(metrics.ergonomics_flags.begin(), metrics.ergonomics_flags.end(),
                    "summary_nodes_present") != metrics.ergonomics_flags.end());
}

TEST_CASE("BwAssembleQueryGraph: recursively resolves query-family references with dedupe",
          "[adt][bw][query]") {
    MockAdtSession mock;
    const std::string root_xml =
        "<?xml version=\"1.0\"?>"
        "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\""
        " name=\"ZQ_SALES\" description=\"Sales Query\">"
        "  <components>"
        "    <member name=\"ZVAR_FISCYEAR\" type=\"VARIABLE\" role=\"SELECTION\"/>"
        "    <member name=\"ZRKF_MARGIN\" type=\"RKF\" role=\"COLUMN\"/>"
        "  </components>"
        "</query:query>";
    const std::string variable_xml =
        "<?xml version=\"1.0\"?>"
        "<query:variable xmlns:query=\"http://www.sap.com/bw/modeling/query\""
        " name=\"ZVAR_FISCYEAR\" description=\"Fiscal Year Variable\">"
        "  <components>"
        "    <member name=\"ZFILTER_REGION\" type=\"FILTER\" role=\"FILTER\"/>"
        "    <member name=\"ZRKF_MARGIN\" type=\"RKF\" role=\"USAGE\"/>"
        "  </components>"
        "</query:variable>";
    const std::string rkf_xml =
        "<?xml version=\"1.0\"?>"
        "<query:rkf xmlns:query=\"http://www.sap.com/bw/modeling/query\""
        " name=\"ZRKF_MARGIN\" description=\"Margin\"/>";
    const std::string filter_xml =
        "<?xml version=\"1.0\"?>"
        "<query:filter xmlns:query=\"http://www.sap.com/bw/modeling/query\""
        " name=\"ZFILTER_REGION\" description=\"Region\"/>";

    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, root_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, rkf_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, variable_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, filter_xml}));

    auto result = BwAssembleQueryGraph(mock, "query", "ZQ_SALES");
    REQUIRE(result.IsOk());
    const auto& graph = result.Value();

    CHECK(graph.root_node_id == "N_QUERY_ZQ_SALES");
    REQUIRE(graph.nodes.size() == 4);
    REQUIRE(graph.edges.size() == 4);
    CHECK(graph.warnings.empty());
}

TEST_CASE("BwAssembleQueryGraph: partial subcomponent failures become warnings",
          "[adt][bw][query]") {
    MockAdtSession mock;
    const std::string root_xml =
        "<?xml version=\"1.0\"?>"
        "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\""
        " name=\"ZQ_SALES\">"
        "  <components>"
        "    <member name=\"ZVAR_MISSING\" type=\"VARIABLE\" role=\"SELECTION\"/>"
        "  </components>"
        "</query:query>";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, root_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "<error/>"}));

    auto result = BwAssembleQueryGraph(mock, "query", "ZQ_SALES");
    REQUIRE(result.IsOk());
    const auto& graph = result.Value();

    REQUIRE(graph.nodes.size() == 2);
    REQUIRE(graph.edges.size() == 1);
    REQUIRE_FALSE(graph.warnings.empty());
    CHECK(graph.warnings[0].find("Failed to resolve VARIABLE ZVAR_MISSING") != std::string::npos);
}

TEST_CASE("BwAssembleQueryGraph: root not found propagates NotFound error",
          "[adt][bw][query]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "<error/>"}));

    auto result = BwAssembleQueryGraph(mock, "query", "ZQ_DOES_NOT_EXIST");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(result.Error().message.find("not found") != std::string::npos);
}
