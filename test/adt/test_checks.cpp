#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/checks.hpp>
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

// The variant is resolved before the run: from the ATC customizing when none
// is given, then checked to exist (GET /atc/checkvariants/{name}).
void EnqueueSystemVariant(MockAdtSession& mock) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("checks/atc_customizing.xml")}));
}

void EnqueueVariantExists(MockAdtSession& mock) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<chkv:checkvariant/>"}));
}

} // anonymous namespace

// ===========================================================================
// RunAtcCheck
// ===========================================================================

TEST_CASE("RunAtcCheck: full workflow with findings", "[adt][checks]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueVariantExists(mock);

    // Step 1: Create worklist -> returns ID.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_001"}));
    // Step 2: Create run -> OK.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    // Step 3: Get worklist results.
    auto xml = LoadFixture("checks/atc_worklist.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/ztest_prb_17726",
                              "ZABAP_CLOUD_DEVELOPMENT");
    REQUIRE(result.IsOk());

    // The fixture is a worklist captured from A4H (SAP_BASIS 816): every
    // element and attribute is namespace-prefixed (atcobject:object,
    // atcfinding:finding, atcfinding:priority, ...). A parser that looked for
    // unprefixed names read it as "no findings" — every live run was clean.
    auto& atc = result.Value();
    CHECK(atc.worklist_id == "wl_001");
    REQUIRE(atc.findings.size() == 5);

    CHECK(atc.findings[0].priority == 1);
    CHECK(atc.findings[0].check_title == "ABAP Language Version (Syntax)");
    CHECK(atc.findings[0].message_title ==
          "Syntax error in restricted language scope (checkpoints)");
    // There is no separate message attribute; the title is the message.
    CHECK(atc.findings[0].message == atc.findings[0].message_title);
    // uri is the source position an agent jumps to, not the finding id.
    CHECK(atc.findings[0].uri ==
          "/sap/bc/adt/oo/classes/ztest_prb_17726/source/main#start=8,0");

    CHECK(atc.findings[1].priority == 2);
    CHECK(atc.findings[1].message_title ==
          "The line contains a BREAK-POINT statement.");
    CHECK(atc.findings[2].priority == 2);
    CHECK(atc.findings[3].priority == 1);
    CHECK(atc.findings[3].check_title == "Usage of Released APIs");
    CHECK(atc.findings[4].priority == 3);
    CHECK(atc.findings[4].uri ==
          "/sap/bc/adt/oo/classes/ztest_prb_17726/source/main#start=10,0");

    CHECK(atc.ErrorCount() == 2);
    CHECK(atc.WarningCount() == 2);
}

TEST_CASE("RunAtcCheck: clean results", "[adt][checks]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueSystemVariant(mock);
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_002"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto xml = LoadFixture("checks/atc_worklist_clean.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_clean");
    REQUIRE(result.IsOk());
    CHECK(result.Value().findings.empty());
}

TEST_CASE("RunAtcCheck: sends correct endpoints", "[adt][checks]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_test"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<worklist xmlns=\"http://www.sap.com/adt/atc\"><objects/></worklist>"}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest", "MY_VARIANT");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 2);
    CHECK(mock.PostCalls()[0].path.find("checkVariant=MY_VARIANT") != std::string::npos);
    CHECK(mock.PostCalls()[1].path.find("worklistId=wl_test") != std::string::npos);

    REQUIRE(mock.GetCallCount() == 3);
    CHECK(mock.GetCalls()[1].path == "/sap/bc/adt/atc/checkvariants/MY_VARIANT");
    CHECK(mock.GetCalls()[2].path.find("atc/worklists/wl_test") != std::string::npos);
}

TEST_CASE("RunAtcCheck: worklist creation failure propagated", "[adt][checks]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueSystemVariant(mock);
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest");
    REQUIRE(result.IsErr());
}

TEST_CASE("RunAtcCheck: run creation failure propagated", "[adt][checks]") {
    MockAdtSession mock;
    // The call probes that the object exists before acting.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueSystemVariant(mock);
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_test"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest");
    REQUIRE(result.IsErr());
}


// ===========================================================================
// A run against an object that is not there used to answer with an empty
// finding list — indistinguishable from "clean", which is the reading that
// matters when an agent runs ATC after every edit and mistypes a URI.
// ===========================================================================

TEST_CASE("RunAtcCheck: a missing object is not reported as clean",
          "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "not found"}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zzz_nope");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    // Nothing was run: no worklist, no run.
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("RunAtcCheck: a transport failure is not reported as missing",
          "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/oo/classes/zcl_x", std::nullopt,
              "HTTP request failed", std::nullopt, ErrorCategory::Connection}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_x");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Connection);
}

// ===========================================================================
// The check variant. Without one, the CLI sent the literal "DEFAULT", which
// does not exist on A4H — SAP ran other checks without a word, and a
// misspelled --variant did the same.
// ===========================================================================

TEST_CASE("RunAtcCheck: without a variant runs the system check variant",
          "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    EnqueueSystemVariant(mock);  // captured from A4H: ZABAP_CLOUD_DEVELOPMENT
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_sys"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("checks/atc_worklist_clean.xml")}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_x");
    REQUIRE(result.IsOk());
    CHECK(result.Value().variant == "ZABAP_CLOUD_DEVELOPMENT");
    CHECK(mock.GetCalls()[1].path == "/sap/bc/adt/atc/customizing");
    CHECK(mock.PostCalls()[0].path.find("checkVariant=ZABAP_CLOUD_DEVELOPMENT") !=
          std::string::npos);
}

TEST_CASE("RunAtcCheck: customizing without a system variant falls back to DEFAULT",
          "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<atc:customizing xmlns:atc=\"http://www.sap.com/adt/atc\"><properties/></atc:customizing>"}));
    EnqueueVariantExists(mock);
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_def"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("checks/atc_worklist_clean.xml")}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_x");
    REQUIRE(result.IsOk());
    CHECK(result.Value().variant == "DEFAULT");
}

TEST_CASE("RunAtcCheck: an unknown variant is NotFound and nothing runs",
          "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    // Captured from A4H for GET /atc/checkvariants/NOPE_XYZ_123.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {404, {}, LoadFixture("checks/atc_checkvariant_not_found.xml")}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_x", "NOPE_XYZ_123");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(result.Error().message.find("NOPE_XYZ_123") != std::string::npos);
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("RunAtcCheck: a release without the variant resource still runs",
          "[adt][checks]") {
    // A 404 without SAP's resource-not-found payload means the endpoint is
    // not there (older releases), not that the variant is missing.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<obj/>"}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "<html>Not Found</html>"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_old"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("checks/atc_worklist_clean.xml")}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/oo/classes/zcl_x", "MY_VARIANT");
    REQUIRE(result.IsOk());
    CHECK(result.Value().variant == "MY_VARIANT");
}

// #84: a priority-1 finding is an error; 2 and 3 are not.
TEST_CASE("AtcResult::HasErrors: priority 1 only", "[adt][checks]") {
    AtcResult atc;
    CHECK_FALSE(atc.HasErrors());
    AtcFinding warning;
    warning.priority = 2;
    atc.findings.push_back(warning);
    CHECK_FALSE(atc.HasErrors());
    AtcFinding error;
    error.priority = 1;
    atc.findings.push_back(error);
    CHECK(atc.HasErrors());
}
