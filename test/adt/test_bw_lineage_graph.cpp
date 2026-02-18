#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

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

TEST_CASE("BwBuildLineageGraph: builds canonical graph with field mappings",
          "[adt][bw][lineage][graph]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_trfn.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_xref.xml")}));

    BwLineageGraphOptions options;
    options.dtp_name = "ZDTP_SALES";
    options.trfn_name = "ZTRFN_SALES";
    options.max_xref = 5;

    auto result = BwBuildLineageGraph(mock, options);
    REQUIRE(result.IsOk());

    const auto& graph = result.Value();
    CHECK(graph.schema_version == "1.0");
    CHECK(graph.root_type == "DTPA");
    CHECK(graph.root_name == "ZDTP_SALES");
    CHECK_FALSE(graph.nodes.empty());
    CHECK_FALSE(graph.edges.empty());
    CHECK(graph.warnings.empty());
    CHECK(graph.provenance.size() >= 3);

    bool found_dtp_node = false;
    bool found_field_mapping = false;
    bool found_xref_edge = false;
    for (const auto& n : graph.nodes) {
        if (n.type == "DTPA" && n.name == "ZDTP_SALES") {
            found_dtp_node = true;
            break;
        }
    }
    for (const auto& e : graph.edges) {
        if (e.type == "field_mapping") {
            found_field_mapping = true;
        }
        if (e.type == "xref") {
            found_xref_edge = true;
        }
    }
    CHECK(found_dtp_node);
    CHECK(found_field_mapping);
    CHECK(found_xref_edge);
}

TEST_CASE("BwBuildLineageGraph: xref failure yields partial graph warning",
          "[adt][bw][lineage][graph]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_trfn.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/repo/is/xref", 500, "boom", std::nullopt,
              ErrorCategory::Internal}));

    BwLineageGraphOptions options;
    options.dtp_name = "ZDTP_SALES";
    options.trfn_name = "ZTRFN_SALES";

    auto result = BwBuildLineageGraph(mock, options);
    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().warnings.empty());
}

TEST_CASE("BwBuildLineageGraph: maps multi-source and constant transformation rules",
          "[adt][bw][lineage][graph]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp_complex.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_trfn_complex.xml")}));

    BwLineageGraphOptions options;
    options.dtp_name = "ZDTP_COMPLEX";
    options.trfn_name = "ZTRFN_COMPLEX";
    options.include_xref = false;

    auto result = BwBuildLineageGraph(mock, options);
    REQUIRE(result.IsOk());

    const auto& graph = result.Value();
    bool found_multi_source_edge = false;
    bool found_constant_derivation = false;
    bool found_rsds_origin_edge = false;
    for (const auto& e : graph.edges) {
        if (e.type == "field_mapping" &&
            e.from == "field:RSDS:ZRSDS_ERP:WAERS" &&
            e.to == "field:ADSO:ZADSO_STAGE:AMOUNT_LOC") {
            found_multi_source_edge = true;
        }
        if (e.type == "field_derivation" &&
            e.to == "field:ADSO:ZADSO_STAGE:FIXED_FLAG") {
            found_constant_derivation = true;
        }
        if (e.type == "field_origin" &&
            e.from == "field:RSDS:ZRSDS_ERP:MATNR") {
            found_rsds_origin_edge = true;
        }
    }
    CHECK(found_multi_source_edge);
    CHECK(found_constant_derivation);
    CHECK(found_rsds_origin_edge);
}

TEST_CASE("BwBuildLineageGraph: empty dtp_name is validation error",
          "[adt][bw][lineage][graph]") {
    MockAdtSession mock;
    BwLineageGraphOptions options;

    auto result = BwBuildLineageGraph(mock, options);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("dtp_name") != std::string::npos);
}
