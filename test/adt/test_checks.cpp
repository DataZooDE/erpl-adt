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
// RunAtcCheck
// ===========================================================================

TEST_CASE("RunAtcCheck: full workflow with findings", "[adt][checks]") {
    MockAdtSession mock;

    // Step 1: Create worklist → returns ID.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_001"}));
    // Step 2: Create run → OK.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    // Step 3: Get worklist results.
    auto xml = LoadFixture("checks/atc_worklist.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest_pkg", "FUNCTIONAL_DB_ADDITION");
    REQUIRE(result.IsOk());

    auto& atc = result.Value();
    CHECK(atc.worklist_id == "wl_001");
    REQUIRE(atc.findings.size() == 3);

    CHECK(atc.findings[0].priority == 1);
    CHECK(atc.findings[0].message == "SELECT statement inside LOOP detected");
    CHECK(atc.findings[0].check_title == "Functional DB Check");

    CHECK(atc.findings[1].priority == 2);
    CHECK(atc.findings[2].priority == 3);

    CHECK(atc.ErrorCount() == 1);
    CHECK(atc.WarningCount() == 1);
}

TEST_CASE("RunAtcCheck: clean results", "[adt][checks]") {
    MockAdtSession mock;
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
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_test"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<worklist xmlns=\"http://www.sap.com/adt/atc\"><objects/></worklist>"}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest", "MY_VARIANT");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 2);
    CHECK(mock.PostCalls()[0].path.find("checkVariant=MY_VARIANT") != std::string::npos);
    CHECK(mock.PostCalls()[1].path.find("worklistId=wl_test") != std::string::npos);

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find("atc/worklists/wl_test") != std::string::npos);
}

TEST_CASE("RunAtcCheck: worklist creation failure propagated", "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest");
    REQUIRE(result.IsErr());
}

TEST_CASE("RunAtcCheck: run creation failure propagated", "[adt][checks]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_test"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = RunAtcCheck(mock, "/sap/bc/adt/packages/ztest");
    REQUIRE(result.IsErr());
}
