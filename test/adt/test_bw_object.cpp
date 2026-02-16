#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_object.hpp>
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

} // anonymous namespace

// ===========================================================================
// BwReadObject
// ===========================================================================

TEST_CASE("BwReadObject: parses ADSO metadata", "[adt][bw][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_adso.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES_DATA";
    opts.version = "a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    CHECK(meta.name == "ZSALES_DATA");
    CHECK(meta.type == "ADSO");
    CHECK(meta.description == "Sales DataStore Object");
    CHECK(meta.package_name == "ZTEST");
    CHECK(meta.last_changed_by == "DEVELOPER");
    CHECK(meta.version == "a");
}

TEST_CASE("BwReadObject: sends correct path and Accept header", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.version = "m";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/adso/zsales/m");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/vnd.sap.bw.modeling.adso-v1_2_0+xml");
}

TEST_CASE("BwReadObject: uppercase names are lowercased in URL", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "CUBE";
    opts.object_name = "0TCT_C01";
    opts.version = "a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/cube/0tct_c01/a");
}

TEST_CASE("BwReadObject: source system adds path segment", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "RSDS";
    opts.object_name = "ZSRC";
    opts.version = "a";
    opts.source_system = "ECLCLNT100";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/rsds/zsrc/ECLCLNT100/a");
}

TEST_CASE("BwReadObject: raw mode returns XML directly", "[adt][bw][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_adso.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES_DATA";
    opts.raw = true;

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().raw_xml == xml);
}

TEST_CASE("BwReadObject: 404 returns NotFound error", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "NONEXIST";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("BwReadObject: 404 preserves SAP error detail", "[adt][bw][object]") {
    MockAdtSession mock;
    std::string sap_body =
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<exc:exception xmlns:exc="http://www.sap.com/abap/exception">)"
        R"(<exc:message>Version 'A' of DataStore object '0TCTHP24O' does not exist</exc:message>)"
        R"(</exc:exception>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, sap_body}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "0TCTHP24O";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    // BW-specific message is preserved
    CHECK(result.Error().message.find("BW object not found") != std::string::npos);
    // SAP detail is extracted into sap_error field
    REQUIRE(result.Error().sap_error.has_value());
    CHECK(result.Error().sap_error->find("does not exist") != std::string::npos);
}

TEST_CASE("BwReadObject: empty type returns error", "[adt][bw][object]") {
    MockAdtSession mock;
    BwReadOptions opts;
    opts.object_name = "ZSALES";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("type") != std::string::npos);
}

TEST_CASE("BwReadObject: empty name returns error", "[adt][bw][object]") {
    MockAdtSession mock;
    BwReadOptions opts;
    opts.object_type = "ADSO";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("name") != std::string::npos);
}

TEST_CASE("BwReadObject: uri overrides constructed path", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "ELEM";
    opts.object_name = "0D_FC_NW_C01_Q0007";
    opts.uri = "/sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a");
}

TEST_CASE("BwReadObject: uri with empty type/name still works", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.uri = "/sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a");
    // Falls back to application/xml when type is empty
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/xml");
}

TEST_CASE("BwReadObject: uri with type uses type for Accept header", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "ELEM";
    opts.uri = "/sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.elem+xml");
}

TEST_CASE("BwReadObject: parses IOBJ with tlogoProperties", "[adt][bw][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_iobj.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "IOBJ";
    opts.object_name = "0CALMONTH";
    opts.version = "a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    CHECK(meta.name == "0CALMONTH");
    CHECK(meta.type == "IOBJ");
    CHECK(meta.description == "Calendar Year/Month");
    CHECK(meta.sub_type == "iobj:TimeCharacteristic");
    CHECK(meta.short_description == "Cal. Year/Month");
    CHECK(meta.long_description == "Calendar Year/Month for reporting and analysis");

    // tlogoProperties
    CHECK(meta.responsible == "SAP");
    CHECK(meta.created_at == "2017-07-13T09:27:01Z");
    CHECK(meta.last_changed_by == "DDIC");
    CHECK(meta.last_changed_at == "2017-07-13T09:27:01Z");
    CHECK(meta.language == "EN");
    CHECK(meta.info_area == "NODESNOTCONNECTED");
    CHECK(meta.status == "active");
    CHECK(meta.content_state == "ACT");
    CHECK(meta.package_name == "NODESNOTCONNECTED");

    // Root attributes in properties
    CHECK(meta.properties.at("fieldName") == "CALMONTH");
    CHECK(meta.properties.at("conversionRoutine") == "PERI6");
    CHECK(meta.properties.at("outputLength") == "7");
    CHECK(meta.properties.at("dataType") == "NUMC");

    // Child element text in properties
    CHECK(meta.properties.at("infoObjectType") == "TIM");
}

TEST_CASE("BwReadObject: ADSO without tlogoProperties still works", "[adt][bw][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_object_adso.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES_DATA";
    opts.version = "a";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    // These should still work from root attributes
    CHECK(meta.description == "Sales DataStore Object");
    CHECK(meta.package_name == "ZTEST");
    CHECK(meta.last_changed_by == "DEVELOPER");

    // tlogoProperties fields should be empty (not present in this fixture)
    CHECK(meta.responsible.empty());
    CHECK(meta.info_area.empty());
    CHECK(meta.status.empty());
    CHECK(meta.content_state.empty());
    CHECK(meta.language.empty());
    CHECK(meta.sub_type.empty());
}

TEST_CASE("BwReadObject: inline XML with tlogoProperties", "[adt][bw][object]") {
    MockAdtSession mock;
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<obj name="ZTEST" description="Test Object">
  <tlogoProperties>
    <responsible>TESTUSER</responsible>
    <createdAt>2025-01-01</createdAt>
    <changedBy>ADMIN</changedBy>
    <changedAt>2025-06-15</changedAt>
    <language>DE</language>
    <infoArea>ZAREA</infoArea>
    <objectStatus>inactive</objectStatus>
    <contentState>MOD</contentState>
  </tlogoProperties>
</obj>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "TEST";
    opts.object_name = "ZTEST";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    // Non-namespaced tlogoProperties (plain element names)
    CHECK(meta.responsible == "TESTUSER");
    CHECK(meta.created_at == "2025-01-01");
    CHECK(meta.last_changed_by == "ADMIN");
    CHECK(meta.last_changed_at == "2025-06-15");
    CHECK(meta.language == "DE");
    CHECK(meta.info_area == "ZAREA");
    CHECK(meta.status == "inactive");
    CHECK(meta.content_state == "MOD");
}

TEST_CASE("BwReadObject: empty tlogoProperties is handled gracefully", "[adt][bw][object]") {
    MockAdtSession mock;
    std::string xml = R"(<obj name="ZMIN" description="Minimal">
  <tlogoProperties/>
</obj>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "TEST";
    opts.object_name = "ZMIN";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    CHECK(meta.description == "Minimal");
    CHECK(meta.responsible.empty());
    CHECK(meta.status.empty());
}

TEST_CASE("BwReadObject: namespace attributes are excluded from properties", "[adt][bw][object]") {
    MockAdtSession mock;
    std::string xml = R"(<obj xmlns:ns="http://example.com" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:type="ns:SubType" name="Z1" description="Test" customAttr="value42"/>)";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    BwReadOptions opts;
    opts.object_type = "TEST";
    opts.object_name = "Z1";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    const auto& meta = result.Value();
    CHECK(meta.sub_type == "ns:SubType");
    CHECK(meta.properties.count("customAttr") == 1);
    CHECK(meta.properties.at("customAttr") == "value42");
    // xmlns and xsi attributes should NOT be in properties
    CHECK(meta.properties.count("xmlns:ns") == 0);
    CHECK(meta.properties.count("xsi:type") == 0);
    // Already-extracted attrs should NOT be in properties
    CHECK(meta.properties.count("name") == 0);
    CHECK(meta.properties.count("description") == 0);
}

// ===========================================================================
// BwLockObject
// ===========================================================================

TEST_CASE("BwLockObject: parses lock response", "[adt][bw][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_lock.xml");
    HttpHeaders headers;
    headers["timestamp"] = "20260214120000";
    headers["Development-Class"] = "ZTEST";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, headers, xml}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES_DATA");
    REQUIRE(result.IsOk());

    const auto& lock = result.Value();
    CHECK(lock.lock_handle == "ABCD1234567890");
    CHECK(lock.transport_number == "NPLK900001");
    CHECK(lock.transport_owner == "DEVELOPER");
    CHECK(lock.transport_text == "BW Development");
    CHECK(lock.timestamp == "20260214120000");
    CHECK(lock.package_name == "ZTEST");
}

TEST_CASE("BwLockObject: lock options include parent query and context headers",
          "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<LOCK_HANDLE>H1</LOCK_HANDLE>"}));

    BwLockOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.parent_name = "PARENT";
    opts.parent_type = "HCPR";
    opts.context_headers.transport_lock_holder = "K900001";

    auto result = BwLockObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("action=lock") != std::string::npos);
    CHECK(call.path.find("parent_name=PARENT") != std::string::npos);
    CHECK(call.path.find("parent_type=HCPR") != std::string::npos);
    CHECK(call.headers.at("Transport-Lock-Holder") == "K900001");
}

TEST_CASE("BwLockObject: sends correct URL", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<LOCK_HANDLE>H1</LOCK_HANDLE>"}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/adso/zsales?action=lock");
}

TEST_CASE("BwLockObject: sends activity header for DELE", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<LOCK_HANDLE>H1</LOCK_HANDLE>"}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES", "DELE");
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].headers.count("activity_context") > 0);
    CHECK(mock.PostCalls()[0].headers.at("activity_context") == "DELE");
}

TEST_CASE("BwLockObject: 400 adds stateful session hint", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {400, {}, "<html><body>Session not found</body></html>"}));

    auto result = BwLockObject(mock, "IOBJ", "0CALDAY");
    REQUIRE(result.IsErr());
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("--session-file") != std::string::npos);
    CHECK(result.Error().hint->find("stateful") != std::string::npos);
}

TEST_CASE("BwLockObject: 409 returns LockConflict", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, "Locked"}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::LockConflict);
}

TEST_CASE("BwLockObject: 423 returns LockConflict", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({423, {}, "Locked"}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::LockConflict);
}

// ===========================================================================
// BwUnlockObject
// ===========================================================================

TEST_CASE("BwUnlockObject: success returns Ok", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = BwUnlockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/adso/zsales?action=unlock");
}

TEST_CASE("BwUnlockObject: 204 is also success", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwUnlockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsOk());
}

// ===========================================================================
// BwSaveObject
// ===========================================================================

TEST_CASE("BwSaveObject: sends correct URL and content", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwSaveOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.content = "<adso/>";
    opts.lock_handle = "H1";
    opts.transport = "K900001";
    opts.timestamp = "20260214120000";

    auto result = BwSaveObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PutCallCount() == 1);
    auto& put = mock.PutCalls()[0];
    CHECK(put.path.find("/sap/bw/modeling/adso/zsales") != std::string::npos);
    CHECK(put.path.find("lockHandle=H1") != std::string::npos);
    CHECK(put.path.find("corrNr=K900001") != std::string::npos);
    CHECK(put.path.find("timestamp=20260214120000") != std::string::npos);
    CHECK(put.body == "<adso/>");
    CHECK(put.content_type == "application/vnd.sap.bw.modeling.adso-v1_2_0+xml");
}

TEST_CASE("BwSaveObject: empty lock handle returns error", "[adt][bw][object]") {
    MockAdtSession mock;
    BwSaveOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.content = "<adso/>";

    auto result = BwSaveObject(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Lock handle") != std::string::npos);
}

TEST_CASE("BwSaveObject: injects transport and foreign context headers",
          "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwSaveOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.content = "<adso/>";
    opts.lock_handle = "H1";
    opts.transport = "K900001";
    opts.context_headers.foreign_objects = "ADSO:ZOTHER";
    opts.context_headers.foreign_object_locks = "LOCK123";

    auto result = BwSaveObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PutCallCount() == 1);
    const auto& headers = mock.PutCalls()[0].headers;
    CHECK(headers.at("Transport-Lock-Holder") == "K900001");
    CHECK(headers.at("Foreign-Objects") == "ADSO:ZOTHER");
    CHECK(headers.at("Foreign-Object-Locks") == "LOCK123");
}

// ===========================================================================
// BwDeleteObject
// ===========================================================================

TEST_CASE("BwDeleteObject: sends correct URL", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = BwDeleteObject(mock, "ADSO", "ZSALES", "H1", "K900001");
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    auto& path = mock.DeleteCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/adso/zsales") != std::string::npos);
    CHECK(path.find("lockHandle=H1") != std::string::npos);
    CHECK(path.find("corrNr=K900001") != std::string::npos);
}

TEST_CASE("BwDeleteObject: 204 is success", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwDeleteObject(mock, "ADSO", "ZSALES", "H1", "");
    REQUIRE(result.IsOk());
}

TEST_CASE("BwDeleteObject: options allow explicit context header override",
          "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwDeleteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.lock_handle = "H1";
    opts.transport = "K900001";
    opts.context_headers.transport_lock_holder = "K999999";
    opts.context_headers.foreign_package = "ZPKG";

    auto result = BwDeleteObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    const auto& headers = mock.DeleteCalls()[0].headers;
    CHECK(headers.at("Transport-Lock-Holder") == "K999999");
    CHECK(headers.at("Foreign-Package") == "ZPKG");
}

// ===========================================================================
// BwReadObject content_type override
// ===========================================================================

TEST_CASE("BwReadObject: content_type override is used as Accept header", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "IOBJ";
    opts.object_name = "0CALMONTH";
    opts.content_type = "application/vnd.sap-bw-modeling.iobj-v2_1_0+xml";

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.iobj-v2_1_0+xml");
}

TEST_CASE("BwReadObject: empty content_type falls back to default", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.content_type = "";  // Explicitly empty

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.adso-v1_2_0+xml");
}

TEST_CASE("BwReadObject: unset content_type falls back to default", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<root/>"}));

    BwReadOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    // content_type not set (nullopt)

    auto result = BwReadObject(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.adso-v1_2_0+xml");
}

// ===========================================================================
// BwSaveObject content_type override
// ===========================================================================

TEST_CASE("BwSaveObject: content_type override is used as Content-Type", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwSaveOptions opts;
    opts.object_type = "IOBJ";
    opts.object_name = "0CALMONTH";
    opts.content = "<iobj/>";
    opts.lock_handle = "H1";
    opts.content_type = "application/vnd.sap-bw-modeling.iobj-v2_1_0+xml";

    auto result = BwSaveObject(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PutCallCount() == 1);
    CHECK(mock.PutCalls()[0].content_type ==
          "application/vnd.sap-bw-modeling.iobj-v2_1_0+xml");
}

// ===========================================================================
// BwCreateObject
// ===========================================================================

TEST_CASE("BwCreateObject: sends create URL with options", "[adt][bw][object]") {
    MockAdtSession mock;
    HttpHeaders headers;
    headers["Location"] = "/sap/bw/modeling/adso/ZNEW_ADSO";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, headers, ""}));

    BwCreateOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZNEW_ADSO";
    opts.package_name = "ZPKG";
    opts.copy_from_name = "ZSOURCE";
    opts.copy_from_type = "ADSO";

    auto result = BwCreateObject(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().uri == "/sap/bw/modeling/adso/ZNEW_ADSO");
    CHECK(result.Value().http_status == 201);

    REQUIRE(mock.PostCallCount() == 1);
    const auto& path = mock.PostCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/adso/znew_adso") != std::string::npos);
    CHECK(path.find("package=ZPKG") != std::string::npos);
    CHECK(path.find("copyFromObjectName=ZSOURCE") != std::string::npos);
    CHECK(path.find("copyFromObjectType=ADSO") != std::string::npos);
}

TEST_CASE("BwCreateObject: non-success status returns error", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({400, {}, "bad"}));

    BwCreateOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZBAD";

    auto result = BwCreateObject(mock, opts);
    REQUIRE(result.IsErr());
}

TEST_CASE("BwLockObject: captures foreign object locks header", "[adt][bw][object]") {
    MockAdtSession mock;
    std::string xml = "<LOCK_HANDLE>H1</LOCK_HANDLE><CORRNR>K900001</CORRNR>";
    HttpHeaders headers;
    headers["Foreign-Object-Locks"] = "LOCKA,LOCKB";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, headers, xml}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsOk());
    CHECK(result.Value().foreign_object_locks == "LOCKA,LOCKB");
}
