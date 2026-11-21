#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/activation.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::vector<InactiveObject> SampleObjects() {
    return {
        {"CLAS", "ZCL_TEST", "/sap/bc/adt/oo/classes/ZCL_TEST"},
        {"INTF", "ZIF_TEST", "/sap/bc/adt/oo/interfaces/ZIF_TEST"},
    };
}

} // namespace

// ===========================================================================
// GetInactiveObjects
// ===========================================================================

TEST_CASE("GetInactiveObjects: returns parsed objects on 200", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<inactive-xml/>"}));
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(SampleObjects()));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().size() == 2);
    CHECK(result.Value()[0].type == "CLAS");
    CHECK(result.Value()[0].name == "ZCL_TEST");
    CHECK(result.Value()[1].type == "INTF");

    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/activation/inactive");
}

TEST_CASE("GetInactiveObjects: propagates HTTP error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "connection failed", std::nullopt}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "connection failed");
}

TEST_CASE("GetInactiveObjects: returns error on non-200", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {401, {}, "Unauthorized"}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 401);
}

TEST_CASE("GetInactiveObjects: returns empty vector when none inactive", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<empty/>"}));
    codec.SetParseInactiveObjectsResponse(
        Result<std::vector<InactiveObject>, Error>::Ok(std::vector<InactiveObject>{}));

    auto result = GetInactiveObjects(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

// ===========================================================================
// ActivateAll
// ===========================================================================

TEST_CASE("ActivateAll: returns zero-count result for empty object list", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    auto result = ActivateAll(session, codec, {});

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 0);
    CHECK(result.Value().activated == 0);
    CHECK(result.Value().failed == 0);
    // No HTTP calls should be made.
    CHECK(session.PostCallCount() == 0);
    CHECK(session.CsrfCallCount() == 0);
}

TEST_CASE("ActivateAll: handles sync 200 response", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<activation-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<result/>"}));

    ActivationResult expected{2, 2, 0, {}};
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(expected));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 2);
    CHECK(result.Value().activated == 2);
    CHECK(result.Value().failed == 0);
    CHECK(result.Value().error_messages.empty());

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path == "/sap/bc/adt/activation");
    CHECK(session.PostCalls()[0].headers.at("x-csrf-token") == "csrf-tok");
}

TEST_CASE("ActivateAll: handles async 202 with poll", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/activation/789"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<activation-result/>",
                   std::chrono::milliseconds{4000}}));

    ActivationResult expected{5, 4, 1, {"CLAS ZCL_BROKEN: syntax error"}};
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(expected));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 5);
    CHECK(result.Value().activated == 4);
    CHECK(result.Value().failed == 1);
    CHECK(result.Value().error_messages.size() == 1);

    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/activation/789");
}

TEST_CASE("ActivateAll: handles lowercase location header", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"location", "/poll/activation/lower"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<activation-result/>",
                   std::chrono::milliseconds{2500}}));
    codec.SetParseActivationResponse(
        Result<ActivationResult, Error>::Ok(ActivationResult{1, 1, 0, {}}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsOk());
    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/activation/lower");
}

TEST_CASE("ActivateAll: returns error when poll fails", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/123"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Failed, "", std::chrono::milliseconds{1000}}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "async activation operation failed");
}

TEST_CASE("ActivateAll: returns timeout error when poll stays running", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/activation/slow"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Running, "", std::chrono::milliseconds{5000}}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Timeout);
    CHECK(result.Error().message ==
          "async activation operation did not complete within timeout");
}

TEST_CASE("ActivateAll: propagates CSRF error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}

TEST_CASE("ActivateAll: propagates XML build error", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Err(
            Error{"BuildActivationXml", "", std::nullopt,
                  "xml build failed", std::nullopt}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "xml build failed");
}

TEST_CASE("ActivateAll: returns error on unexpected status", "[adt][activation]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildActivationXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {500, {}, "Error"}));

    auto result = ActivateAll(session, codec, SampleObjects());

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}

// ===========================================================================
// ActivateObject (new-style, no IXmlCodec)
// ===========================================================================

namespace {

// Sample activation response XML with no errors.
const char* kActivationSuccessXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:activationResultList xmlns:chkl="http://www.sap.com/adt/checklistresult">
</chkl:activationResultList>)";

// Real SAP Cloud format: chkl:messages IS the root, activationExecuted=true.
const char* kSapCloudActivationSuccessXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:messages xmlns:chkl="http://www.sap.com/abapxml/checklist">
  <chkl:properties checkExecuted="true" activationExecuted="true" generationExecuted="true"/>
</chkl:messages>)";

// Real SAP Cloud format: activation cancelled (preaudit errors).
const char* kSapCloudActivationCancelledXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:messages xmlns:chkl="http://www.sap.com/abapxml/checklist">
  <chkl:properties checkExecuted="true" activationExecuted="false" generationExecuted="false"/>
  <msg type="W" line="0" href="">
    <shortText><txt>Activation was cancelled.</txt></shortText>
  </msg>
  <msg type="E" line="1" href="/sap/bc/adt/oo/classes/zcl_test/source/main#start=10,1">
    <shortText><txt>Syntax error in method MAIN</txt></shortText>
  </msg>
</chkl:messages>)";

// Sample activation response XML with error messages.
const char* kActivationWithErrorsXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:activationResultList xmlns:chkl="http://www.sap.com/adt/checklistresult">
  <chkl:messages>
    <msg type="E">
      <shortText>
        <txt>Syntax error in class ZCL_BROKEN</txt>
      </shortText>
    </msg>
    <msg type="W">
      <shortText>
        <txt>Unused variable X</txt>
      </shortText>
    </msg>
  </chkl:messages>
</chkl:activationResultList>)";

} // namespace

TEST_CASE("ActivateObject: sync 200 success with empty response", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-123")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";
    params.type = "CLAS/OC";
    params.name = "ZCL_TEST";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed == 0);
    CHECK(result.Value().error_messages.empty());

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path ==
          "/sap/bc/adt/activation?method=activate&preauditRequested=true");
    CHECK(session.PostCalls()[0].content_type ==
          "application/vnd.sap.adt.activation.v1+xml");
    CHECK(session.PostCalls()[0].headers.at("x-csrf-token") == "csrf-123");

    // Verify the request XML contains the object reference.
    auto& body = session.PostCalls()[0].body;
    CHECK(body.find("adtcore:uri=\"/sap/bc/adt/oo/classes/ZCL_TEST\"") != std::string::npos);
    CHECK(body.find("adtcore:type=\"CLAS/OC\"") != std::string::npos);
    CHECK(body.find("adtcore:name=\"ZCL_TEST\"") != std::string::npos);
}

TEST_CASE("ActivateObject: sync 200 with error messages", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationWithErrorsXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_BROKEN";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().total == 2);
    CHECK(result.Value().failed == 1);
    CHECK(result.Value().activated == 1);
    CHECK(result.Value().error_messages.size() == 2);
    CHECK(result.Value().error_messages[0] == "Syntax error in class ZCL_BROKEN");
    CHECK(result.Value().error_messages[1] == "Unused variable X");
}

// #82: asked to activate the main program of a function group recorded on a
// request, SAP does not activate - it answers HTTP 200 with a pre-audit list
// (root ioc:inactiveObjects) naming the request's objects, the list Eclipse
// shows in its "activate these too?" dialog. The list is the same whether
// anything is inactive or not (captured on A4H for ZERPL_PA890 in both
// states), so the inactive objects decide: read as plain messages it was
// "activated 0, failed 0", exit 0, with the main program still inactive.
namespace {
std::string Fixture(const std::string& name) {
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of("/\\"));
    const auto root = here.substr(0, here.find_last_of("/\\"));
    std::ifstream in(root + "/testdata/" + name);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ActivateObjectParams MainProgramOfPa890() {
    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/functions/groups/zerpl_pa890/source/main";
    params.type = "FUGR/I";
    params.name = "SAPLZERPL_PA890";
    params.parent_uri = "/sap/bc/adt/functions/groups/zerpl_pa890";
    return params;
}
}  // namespace

TEST_CASE("ActivateObject: a pre-audit list with inactive companions is not an activation",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_preaudit_main_inactive.xml")}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("inactive_objects_group_inactive.xml")}));

    auto result = ActivateObject(session, MainProgramOfPa890());

    REQUIRE(result.IsOk());
    const auto& act = result.Value();
    CHECK_FALSE(act.activation_executed);
    CHECK(act.failed == 2);  // the group and its main program
    REQUIRE(act.error_messages.size() == 1);
    const auto& msg = act.error_messages[0];
    CHECK(msg.find("FUGR/F ZERPL_PA890") != std::string::npos);
    CHECK(msg.find("FUGR/I SAPLZERPL_PA890") != std::string::npos);
    // How: one call with everything that is inactive, by URI.
    CHECK(msg.find("erpl-adt activate ") != std::string::npos);
    CHECK(msg.find("/sap/bc/adt/functions/groups/zerpl_pa890/source/main") !=
          std::string::npos);
    CHECK(msg.find("/sap/bc/adt/functions/groups/zerpl_pa890 ") != std::string::npos);
    // The request is where they are recorded, not something to activate.
    CHECK(msg.find("A4HK900308") == std::string::npos);
    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/activation/inactiveobjects");
}

TEST_CASE("ActivateObject: a pre-audit list with nothing inactive is nothing to activate",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_preaudit_nothing_inactive.xml")}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("inactive_objects_none.xml")}));

    auto result = ActivateObject(session, MainProgramOfPa890());

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed == 0);
    CHECK(result.Value().total == 0);
    CHECK(result.Value().error_messages.empty());
    // The verdict came from the inactive objects - 22 of them, none ours -
    // not from the pre-audit list alone (crew F5).
    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/activation/inactiveobjects");
}

TEST_CASE("ActivateObject: a pre-audit list after an asynchronous activation is resolved too",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/activation/1"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, Fixture("activation_preaudit_main_inactive.xml"),
                   std::chrono::milliseconds{100}}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("inactive_objects_group_inactive.xml")}));

    auto result = ActivateObject(session, MainProgramOfPa890());

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed == 2);
    REQUIRE(session.PollCallCount() == 1);
    REQUIRE(session.GetCallCount() == 1);
}

TEST_CASE("ActivateObject: async 202 with poll success", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/activation/abc"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, kActivationSuccessXml,
                   std::chrono::milliseconds{2000}}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed == 0);

    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/activation/abc");
}

TEST_CASE("ActivateObject: async 202 poll failure returns error", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/xyz"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Failed, "", std::chrono::milliseconds{500}}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "async activation operation failed");
    CHECK(result.Error().category == ErrorCategory::ActivationError);
}

TEST_CASE("ActivateObject: HTTP error propagated", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {500, {}, "Internal Server Error"}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}

TEST_CASE("ActivateObject: CSRF error propagated", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}

TEST_CASE("ActivateObject: empty URI returns error", "[adt][activation]") {
    MockAdtSession session;

    ActivateObjectParams params;  // uri is empty

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "URI is required for activation");
}

TEST_CASE("ActivateObject: optional type and name omitted from XML", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/ZCL_TEST";
    // type and name intentionally left empty

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());

    // Verify type/name attributes are not in the request XML.
    auto& body = session.PostCalls()[0].body;
    CHECK(body.find("adtcore:uri=\"/sap/bc/adt/oo/classes/ZCL_TEST\"") != std::string::npos);
    CHECK(body.find("adtcore:type=") == std::string::npos);
    CHECK(body.find("adtcore:name=") == std::string::npos);
}

TEST_CASE("ActivateObject: parent_uri included in XML when set", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri        = "/sap/bc/adt/ddic/ddl/sources/znw_ord_items_v";
    params.type       = "DDLS/DF";
    params.name       = "ZNW_ORD_ITEMS_V";
    params.parent_uri = "/sap/bc/adt/packages/znorthwind";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());

    auto& body = session.PostCalls()[0].body;
    CHECK(body.find("adtcore:parentUri=\"/sap/bc/adt/packages/znorthwind\"") != std::string::npos);
}

TEST_CASE("ActivateObject: parent_uri omitted from XML when empty", "[adt][activation]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri  = "/sap/bc/adt/oo/classes/ZCL_TEST";
    params.type = "CLAS/OC";
    params.name = "ZCL_TEST";
    // parent_uri intentionally left empty

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());

    auto& body = session.PostCalls()[0].body;
    CHECK(body.find("adtcore:parentUri=") == std::string::npos);
}

// ===========================================================================
// ParseActivationResultXml — real SAP Cloud response format
// (chkl:messages as root, with chkl:properties activationExecuted attribute)
// ===========================================================================

TEST_CASE("ActivateObject: SAP Cloud format - activationExecuted=true sets flag, no errors",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kSapCloudActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/zcl_test";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().activation_executed == true);
    CHECK(result.Value().failed == 0);
    CHECK(result.Value().error_messages.empty());
}

TEST_CASE("ActivateObject: SAP Cloud format - activationExecuted=false with error messages",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kSapCloudActivationCancelledXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/zcl_test";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().activation_executed == false);
    CHECK(result.Value().failed == 1);   // 1 "E" message
    CHECK(result.Value().total == 2);    // 1 "W" + 1 "E"
    CHECK(result.Value().error_messages.size() == 2);
    CHECK(result.Value().error_messages[0] == "Activation was cancelled.");
    CHECK(result.Value().error_messages[1] == "Syntax error in method MAIN");
}

TEST_CASE("ActivateObject: legacy activationResultList format - activation_executed stays false",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, kActivationSuccessXml}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/zcl_test";

    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    // Legacy format has no chkl:properties — activation_executed stays false.
    CHECK(result.Value().activation_executed == false);
    CHECK(result.Value().failed == 0);
}

// ===========================================================================
// ActivationParamsFromObjectUri — type/name for source write --activate
// ===========================================================================

TEST_CASE("ActivationParamsFromObjectUri: function module is FUGR/FF with the module name",
          "[adt][activation][uri]") {
    auto p = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/zfg/fmodules/z_fm");
    CHECK(p.uri == "/sap/bc/adt/functions/groups/zfg/fmodules/z_fm");
    CHECK(p.type == "FUGR/FF");
    CHECK(p.name == "Z_FM");
    CHECK(p.parent_uri == "/sap/bc/adt/functions/groups/zfg");
}

TEST_CASE("ActivationParamsFromObjectUri: function group include is FUGR/I",
          "[adt][activation][uri]") {
    auto p = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/zfg/includes/lzfgtop");
    CHECK(p.type == "FUGR/I");
    CHECK(p.name == "LZFGTOP");
    CHECK(p.parent_uri == "/sap/bc/adt/functions/groups/zfg");
}

TEST_CASE("ActivationParamsFromObjectUri: function group itself stays FUGR/F",
          "[adt][activation][uri]") {
    auto p = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/zfg");
    CHECK(p.type == "FUGR/F");
    CHECK(p.name == "ZFG");
    CHECK(p.parent_uri.empty());
    // Its main program (.../source/main) is not the group: see the SAPL test.
}

TEST_CASE("ActivationParamsFromObjectUri: namespaced names are decoded",
          "[adt][activation][uri]") {
    auto p = ActivationParamsFromObjectUri(
        "/sap/bc/adt/functions/groups/%2fdmo%2ffg/fmodules/%2fdmo%2ffm");
    CHECK(p.type == "FUGR/FF");
    CHECK(p.name == "/DMO/FM");
    CHECK(p.parent_uri == "/sap/bc/adt/functions/groups/%2fdmo%2ffg");
}

TEST_CASE("ActivationParamsFromObjectUri: other object types", "[adt][activation][uri]") {
    CHECK(ActivationParamsFromObjectUri("/sap/bc/adt/oo/classes/zcl_x").type == "CLAS/OC");
    CHECK(ActivationParamsFromObjectUri("/sap/bc/adt/oo/classes/zcl_x").name == "ZCL_X");
    CHECK(ActivationParamsFromObjectUri("/sap/bc/adt/programs/includes/zincl").type == "PROG/I");
    CHECK(ActivationParamsFromObjectUri("/sap/bc/adt/ddic/ddl/sources/zi_x").type == "DDLS/DF");
    auto unknown = ActivationParamsFromObjectUri("/sap/bc/adt/unknown/thing/x");
    CHECK(unknown.uri == "/sap/bc/adt/unknown/thing/x");
    CHECK(unknown.type.empty());
    CHECK(unknown.name.empty());
}

// #82: a function group's main program is its own activation object - FUGR/I
// named SAPL<fg> at <group>/source/main, as ADT lists it. Mapped to the group
// (FUGR/F), SAP answered activationExecuted="true" and left it inactive.
TEST_CASE("ActivationParamsFromObjectUri: a function group's main program is FUGR/I SAPL<fg>",
          "[adt][activation]") {
    auto p = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/zerpl_fx_fg/source/main");
    CHECK(p.type == "FUGR/I");
    CHECK(p.name == "SAPLZERPL_FX_FG");
    CHECK(p.uri == "/sap/bc/adt/functions/groups/zerpl_fx_fg/source/main");
    CHECK(p.parent_uri == "/sap/bc/adt/functions/groups/zerpl_fx_fg");

    auto ns = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/%2facme%2ffg/source/main");
    CHECK(ns.type == "FUGR/I");
    CHECK(ns.name == "/ACME/SAPLFG");

    // The group itself stays the group.
    auto group = ActivationParamsFromObjectUri("/sap/bc/adt/functions/groups/zerpl_fx_fg");
    CHECK(group.type == "FUGR/F");
    CHECK(group.name == "ZERPL_FX_FG");
}

// Crew review F1: the pre-audit list names the group and its main program
// with the same label ("FUGR/F ZERPL_PA890"); deduplicated by label, the main
// program's URI was lost, and activating the group with only the main program
// inactive read as "nothing to activate". Companions are keyed by URI, and the
// message names what is inactive as the inactive-objects list names it.
TEST_CASE("ActivateObject: activating the group finds its inactive main program",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_preaudit_main_inactive.xml")}));
    // Only the main program is inactive: the group entry is dropped from the
    // captured list, which is how A4H answers once the group header is active.
    auto inactive = Fixture("inactive_objects_group_inactive.xml");
    const auto group_entry_start = inactive.find(
        "<ioc:entry><ioc:object ioc:user=\"DEVELOPER\" ioc:deleted=\"false\">"
        "<ioc:ref adtcore:uri=\"/sap/bc/adt/functions/groups/zerpl_pa890\"");
    REQUIRE(group_entry_start != std::string::npos);
    const auto group_entry_end = inactive.find("</ioc:entry>", group_entry_start) +
                                 std::string("</ioc:entry>").size();
    inactive.erase(group_entry_start, group_entry_end - group_entry_start);
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, inactive}));

    ActivateObjectParams group;
    group.uri = "/sap/bc/adt/functions/groups/zerpl_pa890";
    group.type = "FUGR/F";
    group.name = "ZERPL_PA890";
    auto result = ActivateObject(session, group);

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed == 1);
    REQUIRE(result.Value().error_messages.size() == 1);
    const auto& msg = result.Value().error_messages[0];
    CHECK(msg.find("FUGR/I SAPLZERPL_PA890") != std::string::npos);
    CHECK(msg.find("/sap/bc/adt/functions/groups/zerpl_pa890/source/main") !=
          std::string::npos);
}

// Crew review F5: an inactive-objects answer that cannot be read is no proof
// that nothing is inactive.
TEST_CASE("ActivateObject: an unreadable inactive-objects list is not success",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_preaudit_nothing_inactive.xml")}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<html>gateway</html>"}));

    auto result = ActivateObject(session, MainProgramOfPa890());

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed > 0);
    REQUIRE(result.Value().error_messages.size() == 1);
    CHECK(result.Value().error_messages[0].find("could not read the inactive objects") !=
          std::string::npos);
}

// Crew review F3: SAP's own activation errors next to an inactiveObjects
// element stay the messages; only a pre-audit list as the root is a pre-audit.
TEST_CASE("ActivateObject: activation errors next to inactive objects are kept",
          "[adt][activation]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {},
         "<chkl:messages xmlns:chkl=\"http://www.sap.com/abapxml/checklist\">"
         "<chkl:properties checkExecuted=\"true\" activationExecuted=\"false\"/>"
         "<msg type=\"E\"><shortText><txt>Field \"LV_X\" is unknown.</txt></shortText></msg>"
         "<ioc:inactiveObjects xmlns:ioc=\"http://www.sap.com/abapxml/inactiveCtsObjects\">"
         "<ioc:entry><ioc:object><ioc:ref xmlns:adtcore=\"http://www.sap.com/adt/core\" "
         "adtcore:uri=\"/sap/bc/adt/oo/classes/zcl_x\" adtcore:type=\"CLAS/OC\" "
         "adtcore:name=\"ZCL_X\"/></ioc:object></ioc:entry></ioc:inactiveObjects>"
         "</chkl:messages>"}));

    ActivateObjectParams params;
    params.uri = "/sap/bc/adt/oo/classes/zcl_x";
    auto result = ActivateObject(session, params);

    REQUIRE(result.IsOk());
    CHECK(result.Value().failed >= 1);
    REQUIRE_FALSE(result.Value().error_messages.empty());
    CHECK(result.Value().error_messages[0] == "Field \"LV_X\" is unknown.");
    CHECK(session.GetCallCount() == 0);  // not a pre-audit: no inactive-list lookup
}

// #84: the outcome rule lives on the result type, so CLI (exit 5) and MCP
// (passed) read it from one place.
TEST_CASE("ActivationResult::Failed: any failed object", "[adt][activation]") {
    ActivationResult act;
    CHECK_FALSE(act.Failed());
    act.activated = 1;
    CHECK_FALSE(act.Failed());
    act.failed = 1;
    CHECK(act.Failed());
}

// ---------------------------------------------------------------------------
// #90: each activation message says which object, include and line it is
// about - read off the message's href (the objDescr is free text).
// ---------------------------------------------------------------------------

TEST_CASE("ActivateObjects: an error in a test include names class, include and line",
          "[adt][activation][positions]") {
    // Captured on A4H: two classes activated together, the second one's unit
    // test misspells the class under test.
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_failed_testclasses.xml")}));
    ActivateObjectParams a = ActivationParamsFromObjectUri("/sap/bc/adt/oo/classes/zcl_erpl_ord_11111");
    ActivateObjectParams b = ActivationParamsFromObjectUri("/sap/bc/adt/oo/classes/zcl_erpl_prc_11111");
    auto result = ActivateObjects(session, {a, b});
    REQUIRE(result.IsOk());
    const auto& act = result.Value();
    REQUIRE(act.messages.size() == 2);
    CHECK(act.error_messages.size() == 2);  // the text list stays

    const auto& cancelled = act.messages[0];
    CHECK(cancelled.type == "W");
    CHECK(cancelled.text == "Activation was cancelled.");
    CHECK_FALSE(cancelled.object.has_value());
    CHECK_FALSE(cancelled.line.has_value());

    const auto& error = act.messages[1];
    CHECK(error.type == "E");
    CHECK(error.text == "Type \"ZCL_ERPL_PRC_11111X\" is unknown.");
    CHECK(error.object == "ZCL_ERPL_PRC_11111");
    CHECK(error.include == "testclasses");
    CHECK(error.line == 9);
    CHECK(error.column == 23);
    CHECK(error.uri == "/sap/bc/adt/oo/classes/zcl_erpl_prc_11111/includes/testclasses");
    CHECK(error.description.find("Class ZCL_ERPL_PRC_11111") == 0);
}

TEST_CASE("ActivateObject: an error in the main source is include main", "[adt][activation][positions]") {
    MockAdtSession session;
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, Fixture("activation_failed_response.xml")}));
    auto result = ActivateObject(session,
                                 ActivationParamsFromObjectUri("/sap/bc/adt/oo/classes/ztest_prb_17726"));
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().messages.size() == 2);
    const auto& error = result.Value().messages[1];
    CHECK(error.object == "ZTEST_PRB_17726");
    CHECK(error.include == "main");
    CHECK(error.line == 7);  // from #start=7,16 - the line="1" attribute is not the line
    CHECK(error.column == 16);
}

TEST_CASE("ActivationMessageFrom: href shapes of other object types", "[adt][activation][positions]") {
    SECTION("a program") {
        auto m = ActivationMessageFrom("E", "x", "/sap/bc/adt/programs/programs/zrep/source/main#start=12,3", "");
        CHECK(m.object == "ZREP");
        CHECK(m.include == "main");
        CHECK(m.line == 12);
        CHECK(m.column == 3);
    }
    SECTION("a function module") {
        auto m = ActivationMessageFrom(
            "E", "x", "/sap/bc/adt/functions/groups/zfg/fmodules/z_read/source/main#start=4,1", "");
        CHECK(m.object == "Z_READ");
        CHECK(m.include == "main");
    }
    SECTION("a namespaced class") {
        auto m = ActivationMessageFrom(
            "E", "x", "/sap/bc/adt/oo/classes/%2fdmo%2fcl_flight/includes/definitions#start=2,5", "");
        CHECK(m.object == "/DMO/CL_FLIGHT");
        CHECK(m.include == "definitions");
    }
    SECTION("the object itself, no position") {
        auto m = ActivationMessageFrom("W", "x", "/sap/bc/adt/oo/classes/zcl_test_flight", "Class ZCL_TEST_FLIGHT");
        CHECK(m.object == "ZCL_TEST_FLIGHT");
        CHECK_FALSE(m.include.has_value());
        CHECK_FALSE(m.line.has_value());
    }
    SECTION("no href") {
        auto m = ActivationMessageFrom("E", "x", "", "");
        CHECK_FALSE(m.object.has_value());
        CHECK_FALSE(m.uri.has_value());
    }
}
