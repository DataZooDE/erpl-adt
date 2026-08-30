#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_activation.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// The BW activation contract, read off the backend rather than guessed at
// (issue #44). CL_RSO_RES_ACTIVATION deserializes the request body with
// cl_atom_feed_prov->get_feed(), so the payload is an Atom feed; each entry's
// rel="self" link names the object, and its content must be a
// {http://www.sap.com/bw/modeling}checkProperties element, which the
// RSO_RES_ST_BW_CHECKRUN transformation maps to the check-run parameters.
// The backend rejects a feed that does not carry exactly one entry, and it
// takes the mode from the URI path — /activation activates, /checkruns only
// checks — not from a query parameter.
//
// The previous payload (a bwActivation:objects document in the
// http://www.sap.com/bw/massact namespace, with mode=/simu= query
// parameters) was never accepted by any system: every call answered
// HTTP 500 "Request cannot be deserialized".
// ===========================================================================

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

BwActivateOptions MakeActivateOptions(const std::string& type,
                                      const std::string& name) {
    BwActivateOptions opts;
    BwActivationObject obj;
    obj.name = name;
    obj.type = type;
    obj.uri = "/sap/bw/modeling/" + type + "/" + name + "/m";
    opts.objects.push_back(std::move(obj));
    return opts;
}

}  // anonymous namespace

// ===========================================================================
// Request shape
// ===========================================================================

TEST_CASE("BwActivateObjects: posts an Atom feed with one entry",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZSALES"));
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("<atom:feed") != std::string::npos);
    CHECK(body.find("http://www.w3.org/2005/Atom") != std::string::npos);
    // Exactly one entry: the backend answers "not acceptable" otherwise.
    CHECK(body.find("<atom:entry>") != std::string::npos);
    CHECK(body.find("<atom:entry>") == body.rfind("<atom:entry>"));
}

TEST_CASE("BwActivateObjects: the entry names the object via a self link",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZSALES"));
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find(R"(rel="self")") != std::string::npos);
    // The type segment is lower-cased: BW only resolves the tlogo that way,
    // and an upper-case one makes the backend dump with HTTP 500.
    CHECK(body.find("/sap/bw/modeling/adso/ZSALES/m") != std::string::npos);
}

TEST_CASE("BwActivateObjects: lower-cases the type segment of a supplied URI",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    BwActivateOptions opts;
    BwActivationObject obj;
    obj.name = "ZSALES";
    obj.type = "ADSO";
    obj.uri = "/sap/bw/modeling/ADSO/ZSALES/m";
    opts.objects.push_back(std::move(obj));

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("/sap/bw/modeling/adso/ZSALES/m") != std::string::npos);
    CHECK(body.find("/sap/bw/modeling/ADSO/") == std::string::npos);
}

TEST_CASE("BwActivateObjects: the entry content is bwModel:checkProperties",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZSALES"));
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("checkProperties") != std::string::npos);
    CHECK(body.find("http://www.sap.com/bw/modeling") != std::string::npos);
    // version, modelContent and lockHandle are unconditional in the backend's
    // transformation — omitting one makes the whole request undeserializable.
    CHECK(body.find(R"(version=")") != std::string::npos);
    CHECK(body.find(R"(modelContent=")") != std::string::npos);
    CHECK(body.find(R"(lockHandle=")") != std::string::npos);
}

TEST_CASE("BwActivateObjects: sends the activation media type",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZSALES"));
    REQUIRE(result.IsOk());

    const auto& call = mock.PostCalls()[0];
    CHECK(call.content_type ==
          "application/vnd.sap.bw.modeling.activation-v1_0_0+xml");
    REQUIRE(call.headers.count("Accept") == 1);
    CHECK(!call.headers.at("Accept").empty());
}

// ===========================================================================
// Mode comes from the path, not a query parameter
// ===========================================================================

TEST_CASE("BwActivateObjects: activate posts to /activation",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Activate;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    const auto& path = mock.PostCalls()[0].path;
    CHECK(path.find("/sap/bw/modeling/activation") == 0);
    // The old mode=/simu= parameters were never read by the backend.
    CHECK(path.find("mode=") == std::string::npos);
    CHECK(path.find("simu=") == std::string::npos);
}

TEST_CASE("BwActivateObjects: validate posts to /checkruns",
          "[adt][bw][activation]") {
    // /checkruns runs the same checks and stops short of activating, which is
    // exactly what --validate means.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Validate;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].path.find("/sap/bw/modeling/checkruns") == 0);
}

TEST_CASE("BwActivateObjects: simulate checks without activating",
          "[adt][bw][activation]") {
    // The API has no simulate mode; a dry run is a check run.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Simulate;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].path.find("/sap/bw/modeling/checkruns") == 0);
}

TEST_CASE("BwActivateObjects: endpoint override is used", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.endpoint_override = "/sap/bw/modeling/activation/custom";
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].path.find("/sap/bw/modeling/activation/custom") == 0);
}

// ===========================================================================
// Per-object parameters travel in the body
// ===========================================================================

TEST_CASE("BwActivateObjects: transport becomes transportId in the body",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.transport = "K900001";
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].body.find(R"(transportId="K900001")") !=
          std::string::npos);
    // Not a query parameter — the old corrnum= was ignored.
    CHECK(mock.PostCalls()[0].path.find("corrnum") == std::string::npos);
}

TEST_CASE("BwActivateObjects: a lock handle is passed through",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.lock_handle = "H123";
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].body.find(R"(lockHandle="H123")") !=
          std::string::npos);
}

TEST_CASE("BwActivateObjects: escapes XML attribute values",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto opts = MakeActivateOptions("ADSO", "Z&SALES");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("Z&amp;SALES") != std::string::npos);
    CHECK(body.find("\"Z&SALES\"") == std::string::npos);
}

TEST_CASE("BwActivateObjects: several objects are one request each",
          "[adt][bw][activation]") {
    // The backend rejects a feed carrying more than one entry, so a
    // multi-object request is a sequence of single-object ones.
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    BwActivateOptions opts;
    for (const auto* name : {"ZONE", "ZTWO"}) {
        BwActivationObject obj;
        obj.name = name;
        obj.type = "ADSO";
        opts.objects.push_back(std::move(obj));
    }

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    REQUIRE(mock.PostCallCount() == 2);
    // The entry identifies the object by URI, which BW spells lower-case.
    CHECK(mock.PostCalls()[0].body.find("/adso/zone") != std::string::npos);
    CHECK(mock.PostCalls()[1].body.find("/adso/ztwo") != std::string::npos);
}

TEST_CASE("BwActivateObjects: builds the object URI when none is given",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    BwActivateOptions opts;
    BwActivationObject obj;
    obj.name = "ZSALES";
    obj.type = "ADSO";
    opts.objects.push_back(std::move(obj));

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(mock.PostCalls()[0].body.find("/sap/bw/modeling/adso/zsales") !=
          std::string::npos);
}

// ===========================================================================
// Response parsing — against the real feed a live SAP_BW 758 system returned
// ===========================================================================

TEST_CASE("BwActivateObjects: parses check results from the response feed",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_result_real.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZTSTK02"));
    REQUIRE(result.IsOk());

    const auto& value = result.Value();
    // The fixture carries warnings and errors, so the activation did not
    // succeed — and the caller needs the messages to know why.
    CHECK(!value.success);
    REQUIRE(!value.messages.empty());

    bool saw_error = false;
    bool saw_warning = false;
    bool saw_key_message = false;
    for (const auto& msg : value.messages) {
        if (msg.severity == "E") saw_error = true;
        if (msg.severity == "W") saw_warning = true;
        if (msg.text.find("Key definition missing") != std::string::npos) {
            saw_key_message = true;
        }
        CHECK(msg.object_name == "ZTSTK02");
    }
    CHECK(saw_error);
    CHECK(saw_warning);
    CHECK(saw_key_message);
}

TEST_CASE("BwActivateObjects: a feed without errors is a success",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZCLEAN"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
}

TEST_CASE("BwActivateObjects: one failing object fails the whole run",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_clean.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_activation_result_real.xml")}));

    BwActivateOptions opts;
    for (const auto* name : {"ZCLEAN", "ZTSTK02"}) {
        BwActivationObject obj;
        obj.name = name;
        obj.type = "ADSO";
        opts.objects.push_back(std::move(obj));
    }

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(!result.Value().success);
}

// ===========================================================================
// Errors
// ===========================================================================

TEST_CASE("BwActivateObjects: empty objects returns error", "[adt][bw][activation]") {
    MockAdtSession mock;
    BwActivateOptions opts;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("BwActivateObjects: an HTTP failure is reported", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "boom"}));

    auto result = BwActivateObjects(mock, MakeActivateOptions("ADSO", "ZSALES"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value_or(0) == 500);
}
