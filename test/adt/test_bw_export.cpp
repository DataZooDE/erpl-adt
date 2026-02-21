#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_export.hpp>
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

// ---------------------------------------------------------------------------
// BwExportInfoarea tests
// ---------------------------------------------------------------------------

TEST_CASE("BwExportInfoarea: ADSO fields collected via nodes + adso_detail",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GetNodes for AREA 0D_NW_DEMO
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_area_nodes.xml")}));
    // ADSO detail
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));
    // DTP detail
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    // DTP lineage: needs dtp, rsds, trfn reads (BwBuildLineageGraph)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_trfn.xml")}));
    // xref disabled in batch export — no additional GETs needed

    BwExportOptions opts;
    opts.infoarea_name = "0D_NW_DEMO";
    opts.include_lineage = true;
    opts.include_queries = false;

    auto result = BwExportInfoarea(mock, opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    CHECK(exp.infoarea == "0D_NW_DEMO");
    CHECK(exp.schema_version == "1.0");
    CHECK(exp.contract == "bw.infoarea.export");
    CHECK_FALSE(exp.exported_at.empty());

    // Should have at least the ADSO object
    bool found_adso = false;
    for (const auto& obj : exp.objects) {
        if (obj.type == "ADSO" && obj.name == "ZADSO_SALES") {
            found_adso = true;
            CHECK_FALSE(obj.fields.empty());
        }
    }
    CHECK(found_adso);
}

TEST_CASE("BwExportInfoarea: types_filter skips detail reads",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GetNodes for the BFS pass
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_area_nodes.xml")}));
    // Search supplement pass (search is on by default)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_search.xml")}));

    BwExportOptions opts;
    opts.infoarea_name = "0D_NW_DEMO";
    opts.types_filter = {"QUERY"};  // No QUERY nodes in fixture or search — yields empty objects

    auto result = BwExportInfoarea(mock, opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    CHECK(exp.objects.empty());
}

TEST_CASE("BwExportInfoarea: search supplement adds IOBJ not in BFS tree",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // Phase 1: GetNodes for AREA — returns ADSO + DTPA, but only IOBJ passes filter
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_area_nodes.xml")}));
    // Phase 2: Search supplement — returns ADSO (filtered) + IOBJ (added) + ADSO (filtered)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_search.xml")}));
    // BwReadObject for the IOBJ 0MATERIAL
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_iobj.xml")}));

    BwExportOptions opts;
    opts.infoarea_name = "0D_NW_DEMO";
    opts.types_filter = {"IOBJ"};
    opts.include_lineage = false;
    opts.include_queries = false;

    auto result = BwExportInfoarea(mock, opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    // IOBJ 0MATERIAL must be present (from search supplement)
    bool found_iobj = false;
    for (const auto& obj : exp.objects) {
        if (obj.type == "IOBJ" && obj.name == "0MATERIAL") {
            found_iobj = true;
        }
    }
    CHECK(found_iobj);
    CHECK(exp.objects.size() == 1);
}

TEST_CASE("BwExportInfoarea: TRFN read 404 yields warning, not error",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // Nodes fixture contains ADSO + DTPA — we test a scenario where nodes has a TRFN
    // We use bw_nodes.xml which has TRFN + DTPA
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_nodes.xml")}));
    // TRFN read → 404 (partial failure)
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/trfn/ZTRFN_SALES/a", 404,
              "Object not found", std::nullopt, ErrorCategory::NotFound}));
    // DTPA read
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    // Lineage reads (DTP, RSDS, TRFN)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dtp.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_rsds.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_trfn.xml")}));

    BwExportOptions opts;
    opts.infoarea_name = "0D_NW_DEMO";
    opts.include_lineage = true;
    opts.include_queries = false;

    auto result = BwExportInfoarea(mock, opts);
    // Result must still be Ok (partial failure)
    REQUIRE(result.IsOk());
    // Warning must be recorded
    CHECK_FALSE(result.Value().warnings.empty());
    bool found_trfn_warn = false;
    for (const auto& w : result.Value().warnings) {
        if (w.find("TRFN") != std::string::npos ||
            w.find("ZTRFN_SALES") != std::string::npos) {
            found_trfn_warn = true;
        }
    }
    CHECK(found_trfn_warn);
}

TEST_CASE("BwExportInfoarea: empty infoarea_name is validation error",
          "[adt][bw][export]") {
    MockAdtSession mock;
    BwExportOptions opts;
    // opts.infoarea_name left empty

    auto result = BwExportInfoarea(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("infoarea_name") != std::string::npos);
}

// ---------------------------------------------------------------------------
// BwExportQuery tests
// ---------------------------------------------------------------------------

TEST_CASE("BwExportQuery: happy path - provider is ADSO",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GET 1: query XML (BwReadQueryComponent)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));
    // GET 2: ADSO detail for provider ZCP_SALES
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZQ_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    CHECK(exp.contract == "bw.query.export");
    REQUIRE(exp.objects.size() == 2);
    CHECK(exp.objects[0].type == "ELEM");
    CHECK(exp.objects[0].name == "ZQ_SALES");
    CHECK(exp.objects[1].type == "ADSO");
    CHECK(exp.objects[1].name == "ZCP_SALES");
    CHECK(exp.dataflow_edges.size() == 1);
}

TEST_CASE("BwExportQuery: provider fallback - ADSO read fails, type is CUBE",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GET 1: query XML succeeds
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));
    // GET 2: ADSO detail fails (provider is a classic HCPR, not ADSO)
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/adso/ZCP_SALES/a", 404,
              "Not found", std::nullopt, ErrorCategory::NotFound}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZQ_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    REQUIRE(exp.objects.size() == 2);
    CHECK(exp.objects[0].type == "ELEM");
    CHECK(exp.objects[1].type == "CUBE");
    CHECK(exp.objects[1].name == "ZCP_SALES");
    // Edge still present: provider → query
    CHECK(exp.dataflow_edges.size() == 1);
}

TEST_CASE("BwExportQuery: iobj_refs harvested from query components",
          "[adt][bw][export]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZQ_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& query_obj = result.Value().objects[0];
    // ZQ_SALES has RKF/CKF members → key_figure refs, and ZVAR_FISCYEAR with
    // uppercase VARIABLE type → variable ref (case-insensitive detection).
    CHECK_FALSE(query_obj.iobj_refs.empty());
    bool found_key_figure = false;
    bool found_variable = false;
    for (const auto& ref : query_obj.iobj_refs) {
        if (ref.role == "key_figure") found_key_figure = true;
        if (ref.role == "variable") found_variable = true;
    }
    CHECK(found_key_figure);
    CHECK(found_variable);  // uppercase VARIABLE type must be detected
}

TEST_CASE("BwExportQuery: BwReadQueryComponent failure propagates as Err",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // Query read fails (e.g. object does not exist)
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/query/ZNONEXISTENT/a", 404,
              "Object not found", std::nullopt, ErrorCategory::NotFound}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZNONEXISTENT", opts);
    REQUIRE(result.IsErr());
}

// ---------------------------------------------------------------------------
// BwExportCube tests
// ---------------------------------------------------------------------------

TEST_CASE("BwExportCube: happy path - provider detail is ADSO",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GET 1: ADSO detail
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportCube(mock, "ZADSO_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    CHECK(exp.contract == "bw.cube.export");
    CHECK(exp.infoarea == "ZADSO_SALES");
    REQUIRE(exp.objects.size() == 1);
    CHECK(exp.objects[0].type == "ADSO");
    CHECK(exp.objects[0].name == "ZADSO_SALES");
    CHECK_FALSE(exp.objects[0].fields.empty());
}

TEST_CASE("BwExportCube: ADSO read fails, type falls back to CUBE stub",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // ADSO detail fails → classic InfoCube/HCPR fallback
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/adso/ZCP_SALES/a", 404,
              "Not found", std::nullopt, ErrorCategory::NotFound}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportCube(mock, "ZCP_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& exp = result.Value();
    REQUIRE(exp.objects.size() == 1);
    CHECK(exp.objects[0].type == "CUBE");
    CHECK(exp.objects[0].name == "ZCP_SALES");
    // Fields are empty for a stub
    CHECK(exp.objects[0].fields.empty());
}

// ---------------------------------------------------------------------------
// BwRenderExportCatalogJson tests
// ---------------------------------------------------------------------------

TEST_CASE("BwRenderExportCatalogJson: contract, objects and dataflow present",
          "[adt][bw][export]") {
    BwInfoareaExport exp;
    exp.infoarea = "TEST_AREA";
    exp.exported_at = "2026-01-01T00:00:00Z";

    BwExportedObject obj;
    obj.name = "ZADSO_TEST";
    obj.type = "ADSO";
    obj.description = "Test ADSO";
    BwExportedField f;
    f.name = "MATERIAL";
    f.data_type = "CHAR";
    f.key = true;
    obj.fields.push_back(f);
    exp.objects.push_back(std::move(obj));

    BwLineageNode ln;
    ln.id = "obj:ADSO:ZADSO_TEST";
    ln.type = "ADSO";
    ln.name = "ZADSO_TEST";
    exp.dataflow_nodes.push_back(ln);

    auto json_str = BwRenderExportCatalogJson(exp);
    CHECK_FALSE(json_str.empty());
    CHECK(json_str.find("\"contract\"") != std::string::npos);
    CHECK(json_str.find("bw.infoarea.export") != std::string::npos);
    CHECK(json_str.find("\"objects\"") != std::string::npos);
    CHECK(json_str.find("\"dataflow\"") != std::string::npos);
    CHECK(json_str.find("ZADSO_TEST") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Bug regression: BwGetNodes provenance recorded after call (q9n)
// ---------------------------------------------------------------------------

TEST_CASE("BwExportInfoarea: GetNodes failure records provenance as error",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // GetNodes fails (e.g. network error)
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bw/modeling/repo/infoproviderstructure/AREA/0D_NW_DEMO",
              503, "Service unavailable", std::nullopt, ErrorCategory::Internal}));

    BwExportOptions opts;
    opts.infoarea_name = "0D_NW_DEMO";
    opts.include_lineage = false;
    opts.include_queries = false;

    auto result = BwExportInfoarea(mock, opts);
    REQUIRE(result.IsOk());  // partial failure — not a hard error

    const auto& exp = result.Value();
    // Warning must be recorded
    CHECK_FALSE(exp.warnings.empty());
    // Provenance must record "error", not "ok"
    bool found_error_prov = false;
    for (const auto& p : exp.provenance) {
        if (p.operation == "BwGetNodes" && p.status == "error") {
            found_error_prov = true;
        }
    }
    CHECK(found_error_prov);
    // Must NOT have a "ok" provenance entry for BwGetNodes when call failed
    for (const auto& p : exp.provenance) {
        if (p.operation == "BwGetNodes") {
            CHECK(p.status == "error");
        }
    }
}

// ---------------------------------------------------------------------------
// 9dp: include_iobj_refs decoupled from include_elem_provider_edges
// ---------------------------------------------------------------------------

TEST_CASE("BwExportOptions: include_iobj_refs alone triggers CollectOrphanElemEdges without adding edges",
          "[adt][bw][export]") {
    // When include_iobj_refs=true but include_elem_provider_edges=false,
    // CollectOrphanElemEdges is called but must NOT add any provider edges.
    // The query result has no ELEM objects, so no extra HTTP calls are made.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;  // provider edges suppressed
    opts.include_iobj_refs = true;             // iobj_refs harvesting enabled
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZQ_SALES", opts);
    REQUIRE(result.IsOk());

    // 3 HTTP calls: query read + adso read + CollectOrphanElemEdges re-reads the
    // query object (type=ELEM/subtype=REP) to harvest its iobj_refs.
    // If include_iobj_refs were false (and include_elem_provider_edges false),
    // CollectOrphanElemEdges would not be called and GetCallCount() would be 2.
    CHECK(mock.GetCallCount() == 3);

    // Exactly 1 dataflow edge: the direct query-provider relationship recorded
    // before CollectOrphanElemEdges runs. CollectOrphanElemEdges must not add
    // a duplicate edge because (a) include_edges=false was passed and (b) the
    // query already has an incoming edge (skip guard).
    const auto& exp = result.Value();
    CHECK(exp.dataflow_edges.size() == 1);
}

// ---------------------------------------------------------------------------
// Bug regression: IobjRole case-insensitive VARIABLE detection (9it)
// ---------------------------------------------------------------------------

TEST_CASE("BwExportQuery: uppercase VARIABLE type maps to variable role",
          "[adt][bw][export]") {
    MockAdtSession mock;
    // The query fixture contains ZVAR_FISCYEAR with type="VARIABLE" (uppercase)
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_query.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_adso.xml")}));

    BwExportOptions opts;
    opts.include_xref_edges = false;
    opts.include_elem_provider_edges = false;
    opts.include_lineage = false;

    auto result = BwExportQuery(mock, "ZQ_SALES", opts);
    REQUIRE(result.IsOk());

    const auto& query_obj = result.Value().objects[0];
    bool found_variable = false;
    for (const auto& ref : query_obj.iobj_refs) {
        if (ref.role == "variable" && ref.name == "ZVAR_FISCYEAR") {
            found_variable = true;
        }
    }
    CHECK(found_variable);
}

// ---------------------------------------------------------------------------
// BwRenderExportMermaid tests
// ---------------------------------------------------------------------------

TEST_CASE("BwRenderExportMermaid: contains graph LR, infoarea name, ADSO node",
          "[adt][bw][export]") {
    BwInfoareaExport exp;
    exp.infoarea = "0D_NW_DEMO";

    BwExportedObject adso;
    adso.name = "ZADSO_SALES";
    adso.type = "ADSO";
    adso.description = "Sales DSO";
    exp.objects.push_back(adso);

    BwExportedObject rsds;
    rsds.name = "ZRSDS_ERP";
    rsds.type = "RSDS";
    exp.objects.push_back(rsds);

    BwExportedObject dtp;
    dtp.name = "ZDTP_SALES";
    dtp.type = "DTPA";
    dtp.dtp_source_name = "ZRSDS_ERP";
    dtp.dtp_source_type = "RSDS";
    dtp.dtp_target_name = "ZADSO_SALES";
    dtp.dtp_target_type = "ADSO";
    exp.objects.push_back(dtp);

    auto mmd = BwRenderExportMermaid(exp);
    CHECK_FALSE(mmd.empty());
    CHECK(mmd.find("graph LR") != std::string::npos);
    CHECK(mmd.find("0D_NW_DEMO") != std::string::npos);
    CHECK(mmd.find("ZADSO_SALES") != std::string::npos);
    // DTP edge
    CHECK(mmd.find("-->") != std::string::npos);
    CHECK(mmd.find("ZDTP_SALES") != std::string::npos);
}
