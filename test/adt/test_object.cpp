#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/object.hpp>
#include <erpl_adt/adt/locking.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.rfind('/');
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.rfind('/'));
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
// GetObjectStructure
// ===========================================================================

TEST_CASE("GetObjectStructure: parses class metadata", "[adt][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/class_metadata.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_EXAMPLE").Value();
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = GetObjectStructure(mock, uri);
    REQUIRE(result.IsOk());

    auto& structure = result.Value();
    CHECK(structure.info.name == "ZCL_EXAMPLE");
    CHECK(structure.info.type == "CLAS/OC");
    CHECK(structure.info.description == "Example class");
    CHECK(structure.info.source_uri == "source/main");
    CHECK(structure.info.version == "active");
    CHECK(structure.info.language == "EN");
    CHECK(structure.info.responsible == "DEVELOPER");
    CHECK(structure.info.changed_by == "DEVELOPER");
    CHECK(structure.info.changed_at == "2026-01-15T10:30:00Z");
    CHECK(structure.info.created_at == "2026-01-01T08:00:00Z");

    REQUIRE(structure.includes.size() == 2);
    CHECK(structure.includes[0].include_type == "main");
    CHECK(structure.includes[0].source_uri == "source/main");
    CHECK(structure.includes[1].include_type == "definitions");
    CHECK(structure.includes[1].source_uri == "includes/definitions");
}

TEST_CASE("GetObjectStructure: sends GET to correct URI", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<class:abapClass xmlns:adtcore=\"http://www.sap.com/adt/core\" "
                  "xmlns:class=\"http://www.sap.com/adt/oo/classes\" "
                  "adtcore:name=\"ZCL_TEST\" adtcore:type=\"CLAS/OC\"/>"}));

    auto result = GetObjectStructure(mock, uri);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/oo/classes/ZCL_TEST");
}

TEST_CASE("GetObjectStructure: HTTP error propagated", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_MISSING").Value();
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/oo/classes/ZCL_MISSING",
              std::nullopt, "connection refused", std::nullopt}));

    auto result = GetObjectStructure(mock, uri);
    REQUIRE(result.IsErr());
}

TEST_CASE("GetObjectStructure: 404 returns error", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_NOTFOUND").Value();
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));

    auto result = GetObjectStructure(mock, uri);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
}

TEST_CASE("GetObjectStructure: invalid XML returns error", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_BAD").Value();
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "not xml"}));

    auto result = GetObjectStructure(mock, uri);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// CreateObject
// ===========================================================================

TEST_CASE("CreateObject: creates class and returns URI from response", "[adt][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/create_class_response.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, xml}));

    CreateObjectParams params;
    params.object_type = "CLAS/OC";
    params.name = "ZCL_NEW_CLASS";
    params.package_name = "ZTEST_PKG";
    params.description = "A new test class";
    params.transport_number = "NPLK900001";

    auto result = CreateObject(mock, params);
    REQUIRE(result.IsOk());
    CHECK(result.Value().Value() == "/sap/bc/adt/oo/classes/zcl_new_class");

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("/sap/bc/adt/oo/classes") != std::string::npos);
    CHECK(call.path.find("corrNr=NPLK900001") != std::string::npos);
}

TEST_CASE("CreateObject: sends correct XML body", "[adt][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {201, {},
         "<class:abapClass xmlns:adtcore=\"http://www.sap.com/adt/core\" "
         "adtcore:name=\"ZCL_TEST\" adtcore:type=\"CLAS/OC\" "
         "adtcore:uri=\"/sap/bc/adt/oo/classes/zcl_test\"/>"}));

    CreateObjectParams params;
    params.object_type = "CLAS/OC";
    params.name = "ZCL_TEST";
    params.package_name = "ZTEST";
    params.description = "Test class";

    auto result = CreateObject(mock, params);
    REQUIRE(result.IsOk());

    auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("adtcore:name=\"ZCL_TEST\"") != std::string::npos);
    CHECK(body.find("adtcore:description=\"Test class\"") != std::string::npos);
    CHECK(body.find("adtcore:packageRef") != std::string::npos);
    CHECK(body.find("adtcore:name=\"ZTEST\"") != std::string::npos);
}

TEST_CASE("CreateObject: unknown type returns error", "[adt][object]") {
    MockAdtSession mock;

    CreateObjectParams params;
    params.object_type = "UNKNOWN/XX";
    params.name = "ZFOO";
    params.package_name = "ZTEST";
    params.description = "Bad type";

    auto result = CreateObject(mock, params);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Unknown object type") != std::string::npos);
}

TEST_CASE("CreateObject: HTTP error propagated", "[adt][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    CreateObjectParams params;
    params.object_type = "CLAS/OC";
    params.name = "ZCL_FAIL";
    params.package_name = "ZTEST";
    params.description = "Fail";

    auto result = CreateObject(mock, params);
    REQUIRE(result.IsErr());
}

TEST_CASE("CreateObject: no transport in URL when not specified", "[adt][object]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {201, {},
         "<program:abapProgram xmlns:adtcore=\"http://www.sap.com/adt/core\" "
         "adtcore:name=\"ZTEST_PROG\" adtcore:type=\"PROG/P\" "
         "adtcore:uri=\"/sap/bc/adt/programs/programs/ztest_prog\"/>"}));

    CreateObjectParams params;
    params.object_type = "PROG/P";
    params.name = "ZTEST_PROG";
    params.package_name = "$TMP";
    params.description = "Test program";

    auto result = CreateObject(mock, params);
    REQUIRE(result.IsOk());

    auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("corrNr") == std::string::npos);
}

// ===========================================================================
// DeleteObject
// ===========================================================================

TEST_CASE("DeleteObject: sends DELETE with lock handle", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("my_lock_handle").Value();
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = DeleteObject(mock, uri, handle);
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    auto& call = mock.DeleteCalls()[0];
    CHECK(call.path.find("lockHandle=my_lock_handle") != std::string::npos);
}

TEST_CASE("DeleteObject: includes transport number when provided", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("handle123").Value();
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = DeleteObject(mock, uri, handle, "NPLK900001");
    REQUIRE(result.IsOk());

    auto& call = mock.DeleteCalls()[0];
    CHECK(call.path.find("corrNr=NPLK900001") != std::string::npos);
}

TEST_CASE("DeleteObject: HTTP error propagated", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueueDelete(Result<HttpResponse, Error>::Err(
        Error{"Delete", "", std::nullopt, "connection refused", std::nullopt}));

    auto result = DeleteObject(mock, uri, handle);
    REQUIRE(result.IsErr());
}

TEST_CASE("DeleteObject: unexpected status code returns error", "[adt][object]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = DeleteObject(mock, uri, handle);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}
