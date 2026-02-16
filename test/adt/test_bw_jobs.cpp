#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_jobs.hpp>
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
// BwListJobs / BwGetJobResult
// ===========================================================================

TEST_CASE("BwListJobs: parses list response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    const std::string xml =
        "<jobs>"
        "<job guid=\"GUID1\" status=\"R\" jobType=\"ACT\" description=\"Running\"/>"
        "<job guid=\"GUID2\" status=\"S\" jobType=\"ACT\" description=\"Done\"/>"
        "</jobs>";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwListJobs(mock);
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);
    CHECK(result.Value()[0].guid == "GUID1");
    CHECK(result.Value()[0].status == "R");
    CHECK(result.Value()[1].guid == "GUID2");
    CHECK(result.Value()[1].description == "Done");
}

TEST_CASE("BwListJobs: sends collection URL and Accept header", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<jobs/>"}));

    auto result = BwListJobs(mock);
    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.jobs+xml");
}

TEST_CASE("BwGetJobResult: parses result resource", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<job status=\"S\" jobType=\"ACT\" description=\"Done\"/>"}));

    auto result = BwGetJobResult(mock, "GUID123");
    REQUIRE(result.IsOk());
    CHECK(result.Value().guid == "GUID123");
    CHECK(result.Value().status == "S");
    CHECK(result.Value().job_type == "ACT");
}

TEST_CASE("BwGetJobResult: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobResult(mock, "");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetJobStatus
// ===========================================================================

TEST_CASE("BwGetJobStatus: parses status response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_job_status.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetJobStatus(mock, "ABC12345678901234567890");
    REQUIRE(result.IsOk());

    const auto& st = result.Value();
    CHECK(st.guid == "ABC12345678901234567890");
    CHECK(st.status == "S");
    CHECK(st.job_type == "TLOGO_ACTIVATION");
    CHECK(st.description == "Activation completed");
}

TEST_CASE("BwGetJobStatus: sends correct URL and Accept header", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<status status=\"R\"/>"}));

    auto result = BwGetJobStatus(mock, "GUID123");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/status");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.jobs.job.status+xml");
}

TEST_CASE("BwGetJobStatus: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobStatus(mock, "");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("empty") != std::string::npos);
}

TEST_CASE("BwGetJobStatus: HTTP error propagated", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwGetJobStatus(mock, "GUID123");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetJobProgress
// ===========================================================================

TEST_CASE("BwGetJobProgress: parses progress response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_job_progress.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetJobProgress(mock, "GUID123");
    REQUIRE(result.IsOk());

    const auto& pr = result.Value();
    CHECK(pr.guid == "GUID123");
    CHECK(pr.percentage == 75);
    CHECK(pr.status == "R");
    CHECK(pr.description == "Activating objects...");
}

TEST_CASE("BwGetJobProgress: sends correct URL", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<progress percentage=\"50\"/>"}));

    auto result = BwGetJobProgress(mock, "GUID456");
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs/GUID456/progress");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.jobs.job.progress+xml");
}

TEST_CASE("BwGetJobProgress: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobProgress(mock, "");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetJobSteps
// ===========================================================================

TEST_CASE("BwGetJobSteps: parses steps response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_job_steps.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetJobSteps(mock, "GUID123");
    REQUIRE(result.IsOk());

    const auto& steps = result.Value();
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].name == "VALIDATE");
    CHECK(steps[0].status == "S");
    CHECK(steps[0].description == "Validation");
    CHECK(steps[1].name == "ACTIVATE");
    CHECK(steps[1].status == "R");
    CHECK(steps[2].name == "GENERATE");
    CHECK(steps[2].status == "N");
}

TEST_CASE("BwGetJobSteps: sends correct URL", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<steps/>"}));

    auto result = BwGetJobSteps(mock, "GUID789");
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs/GUID789/steps");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.jobs.steps+xml");
}

TEST_CASE("BwGetJobSteps: empty steps returns empty vector", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<steps/>"}));

    auto result = BwGetJobSteps(mock, "GUID123");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwGetJobSteps: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobSteps(mock, "");
    REQUIRE(result.IsErr());
}

TEST_CASE("BwGetJobStep: parses single step response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<step name=\"ACTIVATE\" status=\"S\" description=\"Done\"/>"}));

    auto result = BwGetJobStep(mock, "GUID123", "ACTIVATE");
    REQUIRE(result.IsOk());
    CHECK(result.Value().name == "ACTIVATE");
    CHECK(result.Value().status == "S");
    CHECK(result.Value().description == "Done");
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/steps/ACTIVATE");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.jobs.step+xml");
}

TEST_CASE("BwGetJobStep: empty step returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobStep(mock, "GUID123", "");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetJobMessages
// ===========================================================================

TEST_CASE("BwGetJobMessages: parses messages response", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_job_messages.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetJobMessages(mock, "GUID123");
    REQUIRE(result.IsOk());

    const auto& msgs = result.Value();
    REQUIRE(msgs.size() == 3);
    CHECK(msgs[0].severity == "I");
    CHECK(msgs[0].text == "Activation started for ZSALES_DATA");
    CHECK(msgs[0].object_name == "ZSALES_DATA");
    CHECK(msgs[1].severity == "W");
    CHECK(msgs[1].text.find("aggregation rule") != std::string::npos);
    CHECK(msgs[2].severity == "S");
}

TEST_CASE("BwGetJobMessages: sends correct URL", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<messages/>"}));

    auto result = BwGetJobMessages(mock, "GUID123");
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/messages");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap-bw-modeling.balmessages+xml");
}

TEST_CASE("BwGetJobMessages: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwGetJobMessages(mock, "");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwCancelJob
// ===========================================================================

TEST_CASE("BwCancelJob: sends POST to interrupt endpoint", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = BwCancelJob(mock, "GUID123");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/interrupt");
    CHECK(mock.PostCalls()[0].content_type ==
          "application/vnd.sap-bw-modeling.jobs.job.interrupt+xml");
}

TEST_CASE("BwCancelJob: 204 is success", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwCancelJob(mock, "GUID123");
    REQUIRE(result.IsOk());
}

TEST_CASE("BwCancelJob: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwCancelJob(mock, "");
    REQUIRE(result.IsErr());
}

TEST_CASE("BwCancelJob: HTTP error propagated", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwCancelJob(mock, "GUID123");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwRestartJob
// ===========================================================================

TEST_CASE("BwRestartJob: sends POST to restart endpoint", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = BwRestartJob(mock, "GUID123");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/restart");
}

TEST_CASE("BwRestartJob: 204 is success", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwRestartJob(mock, "GUID123");
    REQUIRE(result.IsOk());
}

TEST_CASE("BwRestartJob: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwRestartJob(mock, "");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("empty") != std::string::npos);
}

TEST_CASE("BwRestartJob: HTTP error propagated", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwRestartJob(mock, "GUID123");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwCleanupJob
// ===========================================================================

TEST_CASE("BwCleanupJob: sends DELETE to cleanup endpoint", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = BwCleanupJob(mock, "GUID123");
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    CHECK(mock.DeleteCalls()[0].path == "/sap/bw/modeling/jobs/GUID123/cleanup");
}

TEST_CASE("BwCleanupJob: 204 is success", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwCleanupJob(mock, "GUID123");
    REQUIRE(result.IsOk());
}

TEST_CASE("BwCleanupJob: empty GUID returns error", "[adt][bw][jobs]") {
    MockAdtSession mock;
    auto result = BwCleanupJob(mock, "");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("empty") != std::string::npos);
}

TEST_CASE("BwCleanupJob: HTTP error propagated", "[adt][bw][jobs]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwCleanupJob(mock, "GUID123");
    REQUIRE(result.IsErr());
}
