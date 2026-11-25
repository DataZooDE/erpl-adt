#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/service_binding.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// The shape of a publish job answer (ABAP XML, asx:abap). Not captured from
// a system: modelled on what Eclipse ADT and abap-adt-api read from it.
const char* kPublishOk =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0"><asx:values><DATA>)"
    R"(<SEVERITY>OK</SEVERITY>)"
    R"(<SHORT_TEXT>Local Service Endpoint of service ZUI_ERPL_O2 is activated locally</SHORT_TEXT>)"
    R"(<LONG_TEXT/>)"
    R"(</DATA></asx:values></asx:abap>)";

const char* kPublishError =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0"><asx:values><DATA>)"
    R"(<SEVERITY>ERROR</SEVERITY>)"
    R"(<SHORT_TEXT>Service binding ZUI_ERPL_O2 is not active</SHORT_TEXT>)"
    R"(<LONG_TEXT>Activate the service binding before publishing it.</LONG_TEXT>)"
    R"(</DATA></asx:values></asx:abap>)";

void ExpectCsrfAndPost(MockAdtSession& mock, int status, const std::string& body) {
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("tok123"));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({status, {}, body}));
}

}  // namespace

// ===========================================================================
// Request shape
// ===========================================================================

TEST_CASE("PublishServiceBinding: V2 request path, headers and body", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishOk);

    auto result = PublishServiceBinding(mock, "ZUI_ERPL_O2");
    REQUIRE(result.IsOk());

    CHECK(mock.CsrfCallCount() == 1);
    REQUIRE(mock.PostCallCount() == 1);
    const auto& call = mock.PostCalls()[0];
    CHECK(call.path ==
          "/sap/bc/adt/businessservices/odatav2/publishjobs"
          "?servicename=ZUI_ERPL_O2&serviceversion=0001");
    CHECK(call.content_type == "application/xml");
    CHECK(call.headers.at("Accept") == "application/*");
    CHECK(call.headers.at("x-csrf-token") == "tok123");
    CHECK(call.body.find(R"(<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">)") !=
          std::string::npos);
    CHECK(call.body.find(R"(<adtcore:objectReference adtcore:name="ZUI_ERPL_O2"/>)") !=
          std::string::npos);
}

TEST_CASE("UnpublishServiceBinding: unpublishjobs path", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishOk);

    auto result = UnpublishServiceBinding(mock, "ZUI_ERPL_O2", "0002");
    REQUIRE(result.IsOk());
    CHECK(result.Value().action == ServicePublishAction::Unpublish);
    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/businessservices/odatav2/unpublishjobs"
          "?servicename=ZUI_ERPL_O2&serviceversion=0002");
}

TEST_CASE("PublishServiceBinding: V4 uses the odatav4 path", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishOk);
    auto result = PublishServiceBinding(mock, "ZUI_ERPL_O4", "0001", ODataVersion::V4);
    REQUIRE(result.IsOk());
    CHECK(result.Value().odata == ODataVersion::V4);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/businessservices/odatav4/publishjobs"
          "?servicename=ZUI_ERPL_O4&serviceversion=0001");

    MockAdtSession mock2;
    ExpectCsrfAndPost(mock2, 200, kPublishOk);
    auto un = UnpublishServiceBinding(mock2, "ZUI_ERPL_O4", "0001", ODataVersion::V4);
    REQUIRE(un.IsOk());
    CHECK(mock2.PostCalls()[0].path.rfind(
              "/sap/bc/adt/businessservices/odatav4/unpublishjobs?", 0) == 0);
}

TEST_CASE("PublishServiceBinding: namespaced name is URL-encoded in the query and escaped in the body",
          "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishOk);
    auto result = PublishServiceBinding(mock, "/dmo/ui_travel&o2");
    REQUIRE(result.IsOk());
    const auto& call = mock.PostCalls()[0];
    // Upper-cased: SAP keeps repository names upper-case.
    CHECK(call.path ==
          "/sap/bc/adt/businessservices/odatav2/publishjobs"
          "?servicename=%2FDMO%2FUI_TRAVEL%26O2&serviceversion=0001");
    CHECK(call.body.find(R"(adtcore:name="/DMO/UI_TRAVEL&amp;O2")") != std::string::npos);
    CHECK(result.Value().binding == "/DMO/UI_TRAVEL&O2");
}

TEST_CASE("PublishServiceBinding: empty name or version is refused before any request",
          "[adt][service_binding]") {
    MockAdtSession mock;
    auto no_name = PublishServiceBinding(mock, "");
    REQUIRE(no_name.IsErr());
    CHECK(no_name.Error().category == ErrorCategory::Validation);
    auto no_version = PublishServiceBinding(mock, "ZUI_ERPL_O2", "");
    REQUIRE(no_version.IsErr());
    CHECK(no_version.Error().category == ErrorCategory::Validation);
    CHECK(mock.PostCallCount() == 0);
    CHECK(mock.CsrfCallCount() == 0);
}

// ===========================================================================
// Answers
// ===========================================================================

TEST_CASE("PublishServiceBinding: OK answer", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishOk);
    auto result = PublishServiceBinding(mock, "zui_erpl_o2");
    REQUIRE(result.IsOk());
    const auto& r = result.Value();
    CHECK(r.Succeeded());
    CHECK(r.binding == "ZUI_ERPL_O2");
    CHECK(r.service_version == "0001");
    CHECK(r.odata == ODataVersion::V2);
    CHECK(r.action == ServicePublishAction::Publish);
    CHECK(r.severity == "OK");
    CHECK(r.short_text == "Local Service Endpoint of service ZUI_ERPL_O2 is activated locally");
    CHECK(r.long_text.empty());
}

TEST_CASE("PublishServiceBinding: ERROR severity is an Ok result that did not succeed",
          "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, kPublishError);
    auto result = PublishServiceBinding(mock, "ZUI_ERPL_O2");
    REQUIRE(result.IsOk());
    const auto& r = result.Value();
    CHECK_FALSE(r.Succeeded());
    CHECK(r.severity == "ERROR");
    CHECK(r.short_text == "Service binding ZUI_ERPL_O2 is not active");
    CHECK(r.long_text == "Activate the service binding before publishing it.");

    const auto j = ServicePublishResultToJson(r);
    CHECK(j["ok"] == false);
    CHECK(j["severity"] == "ERROR");
    CHECK(j["short_text"] == "Service binding ZUI_ERPL_O2 is not active");
    CHECK(j["long_text"] == "Activate the service binding before publishing it.");
    CHECK(j["binding"] == "ZUI_ERPL_O2");
    CHECK(j["odata"] == "v2");
    CHECK(j["action"] == "publish");
    CHECK(j["service_version"] == "0001");
}

TEST_CASE("ServicePublishResult: WARNING is not OK; ok is case-insensitive",
          "[adt][service_binding]") {
    ServicePublishResult r;
    r.severity = "WARNING";
    CHECK_FALSE(r.Succeeded());
    r.severity = "ok";
    CHECK(r.Succeeded());
    r.severity = "";
    CHECK_FALSE(r.Succeeded());
}

TEST_CASE("PublishServiceBinding: HTTP error carries SAP's message", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 400,
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<exc:exception xmlns:exc="http://www.sap.com/abapxml/types/communicationframework">)"
        R"(<namespace id="com.sap.adt"/><type id="ExceptionResourceNotFound"/>)"
        R"(<message lang="EN">Service ZUI_NOPE does not exist</message>)"
        R"(</exc:exception>)");
    auto result = PublishServiceBinding(mock, "ZUI_NOPE");
    REQUIRE(result.IsErr());
    const auto& e = result.Error();
    CHECK(e.http_status == 400);
    CHECK(e.operation == "PublishServiceBinding");
    CHECK(e.endpoint.find("/odatav2/publishjobs?servicename=ZUI_NOPE") != std::string::npos);
    CHECK(e.message.find("Service ZUI_NOPE does not exist") != std::string::npos);
}

TEST_CASE("PublishServiceBinding: 404 is NotFound", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 404, "");
    auto result = UnpublishServiceBinding(mock, "ZUI_ERPL_O2");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(result.Error().operation == "UnpublishServiceBinding");
}

TEST_CASE("PublishServiceBinding: transport and CSRF errors propagate", "[adt][service_binding]") {
    SECTION("CSRF fetch fails") {
        MockAdtSession mock;
        mock.EnqueueCsrfToken(Result<std::string, Error>::Err(
            Error{"FetchCsrfToken", "/sap/bc/adt/discovery", 403, "Forbidden", std::nullopt,
                  ErrorCategory::Authorization}));
        auto result = PublishServiceBinding(mock, "ZUI_ERPL_O2");
        REQUIRE(result.IsErr());
        CHECK(result.Error().category == ErrorCategory::Authorization);
        CHECK(mock.PostCallCount() == 0);
    }
    SECTION("connection refused") {
        MockAdtSession mock;
        mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("tok"));
        mock.EnqueuePost(Result<HttpResponse, Error>::Err(
            Error{"Post", "/sap/bc/adt/businessservices/odatav2/publishjobs", std::nullopt,
                  "connection refused", std::nullopt, ErrorCategory::Connection}));
        auto result = PublishServiceBinding(mock, "ZUI_ERPL_O2");
        REQUIRE(result.IsErr());
        CHECK(result.Error().category == ErrorCategory::Connection);
    }
}

TEST_CASE("PublishServiceBinding: 200 without a publish result is an error", "[adt][service_binding]") {
    MockAdtSession mock;
    ExpectCsrfAndPost(mock, 200, "<html><body>Logon</body></html>");
    auto result = PublishServiceBinding(mock, "ZUI_ERPL_O2");
    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "PublishServiceBinding");
    CHECK(result.Error().message.find("Unexpected publish job answer") != std::string::npos);
}

TEST_CASE("ParseServicePublishResponse: malformed answers", "[adt][service_binding]") {
    CHECK(ParseServicePublishResponse("").IsErr());
    CHECK(ParseServicePublishResponse("not xml <").IsErr());
    CHECK(ParseServicePublishResponse(
              R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml"><asx:values/></asx:abap>)")
              .IsErr());
    CHECK(ParseServicePublishResponse(
              R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml"><asx:values><DATA>)"
              R"(<SHORT_TEXT>x</SHORT_TEXT></DATA></asx:values></asx:abap>)")
              .IsErr());
    auto ok = ParseServicePublishResponse(
        R"(<asx:abap xmlns:asx="http://www.sap.com/abapxml"><asx:values><DATA>)"
        R"(<SEVERITY>OK</SEVERITY></DATA></asx:values></asx:abap>)");
    REQUIRE(ok.IsOk());
    CHECK(ok.Value().severity == "OK");
    CHECK(ok.Value().short_text.empty());
}

TEST_CASE("ParseODataVersion", "[adt][service_binding]") {
    CHECK(ParseODataVersion("v2").Value() == ODataVersion::V2);
    CHECK(ParseODataVersion("V2").Value() == ODataVersion::V2);
    CHECK(ParseODataVersion("v4").Value() == ODataVersion::V4);
    CHECK(ParseODataVersion("V4").Value() == ODataVersion::V4);
    auto bad = ParseODataVersion("v3");
    REQUIRE(bad.IsErr());
    CHECK(bad.Error().category == ErrorCategory::Validation);
    CHECK(bad.Error().message.find("v2 or v4") != std::string::npos);
    CHECK(ODataVersionName(ODataVersion::V2) == "v2");
    CHECK(ODataVersionName(ODataVersion::V4) == "v4");
}
