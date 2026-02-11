#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/testing.hpp>
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
// RunTests
// ===========================================================================

TEST_CASE("RunTests: all passing", "[adt][testing]") {
    MockAdtSession mock;
    auto xml = LoadFixture("testing/test_pass.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = RunTests(mock, "/sap/bc/adt/oo/classes/zcl_test");
    REQUIRE(result.IsOk());

    auto& run = result.Value();
    CHECK(run.AllPassed());
    CHECK(run.TotalMethods() == 2);
    CHECK(run.TotalFailed() == 0);

    REQUIRE(run.classes.size() == 1);
    CHECK(run.classes[0].name == "LTC_TEST");
    CHECK(run.classes[0].risk_level == "harmless");
    CHECK(run.classes[0].duration_category == "short");

    REQUIRE(run.classes[0].methods.size() == 2);
    CHECK(run.classes[0].methods[0].name == "test_add");
    CHECK(run.classes[0].methods[0].execution_time_ms == 5);
    CHECK(run.classes[0].methods[0].Passed());
    CHECK(run.classes[0].methods[1].name == "test_subtract");
    CHECK(run.classes[0].methods[1].Passed());
}

TEST_CASE("RunTests: with failures", "[adt][testing]") {
    MockAdtSession mock;
    auto xml = LoadFixture("testing/test_failures.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = RunTests(mock, "/sap/bc/adt/oo/classes/zcl_test");
    REQUIRE(result.IsOk());

    auto& run = result.Value();
    CHECK_FALSE(run.AllPassed());
    CHECK(run.TotalMethods() == 3);
    CHECK(run.TotalFailed() == 2);

    REQUIRE(run.classes.size() == 2);

    // First class: LTC_MATH — 1 pass, 1 fail
    auto& math = run.classes[0];
    CHECK(math.name == "LTC_MATH");
    CHECK(math.FailedCount() == 1);
    CHECK(math.methods[0].Passed());      // test_add
    CHECK_FALSE(math.methods[1].Passed()); // test_divide

    auto& alert = math.methods[1].alerts[0];
    CHECK(alert.kind == "failedAssertion");
    CHECK(alert.severity == "critical");
    CHECK(alert.title == "Assertion failed");
    CHECK(alert.detail == "Expected 5 but got 0");

    // Second class: LTC_STRING — 1 fail
    auto& str = run.classes[1];
    CHECK(str.name == "LTC_STRING");
    CHECK(str.FailedCount() == 1);
}

TEST_CASE("RunTests: sends POST to testruns endpoint", "[adt][testing]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<aunit:runResult xmlns:aunit=\"http://www.sap.com/adt/aunit\"/>"}));

    auto result = RunTests(mock, "/sap/bc/adt/oo/classes/zcl_test");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path == "/sap/bc/adt/abapunit/testruns");
    CHECK(call.body.find("zcl_test") != std::string::npos);
}

TEST_CASE("RunTests: HTTP error propagated", "[adt][testing]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = RunTests(mock, "/sap/bc/adt/oo/classes/zcl_test");
    REQUIRE(result.IsErr());
}

TEST_CASE("RunTests: unexpected status returns error", "[adt][testing]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, ""}));

    auto result = RunTests(mock, "/sap/bc/adt/oo/classes/zcl_test");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}
