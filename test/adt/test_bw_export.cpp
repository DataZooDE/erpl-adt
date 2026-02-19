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
    // Only GetNodes — no detail reads since filter won't match
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_area_nodes.xml")}));
    // Search supplement returns objects, but types_filter excludes them too
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
