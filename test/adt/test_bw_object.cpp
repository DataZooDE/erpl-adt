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
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/adso/ZSALES/m");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/vnd.sap.bw.modeling.adso-v1_2_0+xml");
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

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/rsds/ZSRC/ECLCLNT100/a");
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

TEST_CASE("BwLockObject: sends correct URL", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<LOCK_HANDLE>H1</LOCK_HANDLE>"}));

    auto result = BwLockObject(mock, "ADSO", "ZSALES");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/adso/ZSALES?action=lock");
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

    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/adso/ZSALES?action=unlock");
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
    CHECK(put.path.find("/sap/bw/modeling/adso/ZSALES") != std::string::npos);
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
    CHECK(path.find("/sap/bw/modeling/adso/ZSALES") != std::string::npos);
    CHECK(path.find("lockHandle=H1") != std::string::npos);
    CHECK(path.find("corrNr=K900001") != std::string::npos);
}

TEST_CASE("BwDeleteObject: 204 is success", "[adt][bw][object]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwDeleteObject(mock, "ADSO", "ZSALES", "H1", "");
    REQUIRE(result.IsOk());
}
