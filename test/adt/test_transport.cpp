#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/transport.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <vector>
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

// ListTransports does two GETs: (1) search-configurations, (2) transport tree.
// This helper enqueues both responses on the mock in that order.
void EnqueueListTransportsResponses(MockAdtSession& mock,
                                    const std::string& configs_xml,
                                    const std::string& transports_xml) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, configs_xml}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, transports_xml}));
}


// A real request answer (transport_show.xml, A4HK900156) renumbered and
// without its task: what GetTransport reads before a release.
std::string RequestWithoutTasks(const std::string& number) {
    auto xml = LoadFixture("transport/transport_show.xml");
    const auto start = xml.find("<tm:task ");
    const auto end = xml.find("</tm:task>", start) + std::string("</tm:task>").size();
    xml.erase(start, end - start);
    for (std::size_t pos; (pos = xml.find("A4HK900156")) != std::string::npos;) {
        xml.replace(pos, 10, number);
    }
    return xml;
}

void EnqueueRequestWithoutTasks(MockAdtSession& mock, const std::string& number) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, RequestWithoutTasks(number)}));
}

} // anonymous namespace

// ===========================================================================
// ListTransports
// ===========================================================================

TEST_CASE("ListTransports: parses nested transport tree", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());

    auto& transports = result.Value();
    REQUIRE(transports.size() == 3);

    // Order is iteration order of the tree (modifiable first, then released).
    CHECK(transports[0].number == "NPLK900001");
    CHECK(transports[0].description == "Implement feature X");
    CHECK(transports[0].owner == "DEVELOPER");
    CHECK(transports[0].status == "modifiable");
    CHECK(transports[0].target == "NPL");

    CHECK(transports[1].number == "NPLK900003");
    CHECK(transports[1].status == "modifiable");

    CHECK(transports[2].number == "NPLK900002");
    CHECK(transports[2].status == "released");
}

TEST_CASE("ListTransports: queries configUri when configuration exists", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/searchconfiguration/configurations");
    CHECK(mock.GetCalls()[1].path.find("configUri=") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("targets=true") != std::string::npos);
}

TEST_CASE("ListTransports: filters by owner when using configUri", "[adt][transport]") {
    MockAdtSession mock;
    // Real response has DEVELOPER as owner. We pass a different user.
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "ADMIN");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("ListTransports: falls back to user=USER when no configuration exists",
          "[adt][transport]") {
    MockAdtSession mock;
    EnqueueListTransportsResponses(
        mock,
        LoadFixture("transport/search_configurations_empty.xml"),
        LoadFixture("transport/transport_list.xml"));

    auto result = ListTransports(mock, "ADMIN");
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 2);
    CHECK(mock.GetCalls()[1].path.find("user=ADMIN") != std::string::npos);
    CHECK(mock.GetCalls()[1].path.find("configUri=") == std::string::npos);
    // No owner filter applied in fallback mode — all 3 entries pass through.
    CHECK(result.Value().size() == 3);
}

TEST_CASE("ListTransports: tolerates failing search-config lookup", "[adt][transport]") {
    MockAdtSession mock;
    // Search-config call fails — fall back to legacy ?user= query.
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_list.xml")}));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsOk());
    CHECK(mock.GetCalls()[1].path.find("user=DEVELOPER") != std::string::npos);
}

TEST_CASE("ListTransports: HTTP error on tree fetch propagated", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/search_configurations.xml")}));
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "timeout", std::nullopt}));

    auto result = ListTransports(mock, "DEVELOPER");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// CreateTransport
// ===========================================================================

TEST_CASE("CreateTransport: returns transport number", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900005\n"}));

    auto result = CreateTransport(mock, "My new transport", "ZTEST_PKG");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "NPLK900005");
}

TEST_CASE("CreateTransport: extracts number from URI path", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "/sap/bc/adt/cts/transports/NPLK900010"}));

    auto result = CreateTransport(mock, "Another transport", "ZDEV");
    REQUIRE(result.IsOk());
    CHECK(result.Value() == "NPLK900010");
}

TEST_CASE("CreateTransport: sends correct body", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900001"}));

    auto result = CreateTransport(mock, "Test transport", "ZPKG");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path == "/sap/bc/adt/cts/transports");
    CHECK(call.body.find("ZPKG") != std::string::npos);
    CHECK(call.body.find("Test transport") != std::string::npos);
}

TEST_CASE("CreateTransport: escapes XML in body fields", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({201, {}, "NPLK900001"}));

    auto result = CreateTransport(mock, "Fix <bug> & \"quote\"", "Z&PKG");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("<DEVCLASS>Z&amp;PKG</DEVCLASS>") != std::string::npos);
    CHECK(body.find("<REQUEST_TEXT>Fix &lt;bug&gt; &amp; &quot;quote&quot;</REQUEST_TEXT>") != std::string::npos);
}

TEST_CASE("CreateTransport: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = CreateTransport(mock, "Fail", "ZPKG");
    REQUIRE(result.IsErr());
}

// ===========================================================================
// ReleaseTransport
// ===========================================================================

TEST_CASE("ReleaseTransport: sends POST to release endpoint", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "NPLK900001");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/NPLK900001/newreleasejobs");
}

TEST_CASE("ReleaseTransport: accepts 204", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "NPLK900002");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900002");
    REQUIRE(result.IsOk());
}

TEST_CASE("ReleaseTransport: unexpected status returns error", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "NPLK900001");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 409);
}

TEST_CASE("ReleaseTransport: HTTP error propagated", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "NPLK900001");
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "connection refused", std::nullopt}));

    auto result = ReleaseTransport(mock, "NPLK900001");
    REQUIRE(result.IsErr());
}


// Releasing a transport that does not exist answered HTTP 200 with an empty
// body, and the CLI reported "Released transport: X" — a claim about a
// one-way operation that never happened.
TEST_CASE("ReleaseTransport: a missing transport is not reported as released",
          "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "not found"}));

    auto result = ReleaseTransport(mock, "ZZZK900099");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(mock.PostCallCount() == 0);
}

// A release answers HTTP 200 whether it worked or not; the outcome is the
// release report inside (chkrun:checkReport, status + messages). Reading only
// the status code, `transport release` said "Released" for a request SAP had
// refused (#72; all three answers captured from A4H).
TEST_CASE("ReleaseTransport: a released task is success", "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "A4HK900227");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_released.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900227");
    REQUIRE(result.IsOk());
}

TEST_CASE("ReleaseTransport: a refused release is a transport error with SAP's reason",
          "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "A4HK900226");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_not_released.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900226");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::TransportError);
    CHECK(result.Error().ExitCode() == 9);
    CHECK(result.Error().message.find("Referencing task A4HK900227 not yet released") !=
          std::string::npos);
}

TEST_CASE("ReleaseTransport: an export failure is not reported as released",
          "[adt][transport]") {
    MockAdtSession mock;
    EnqueueRequestWithoutTasks(mock, "A4HK900226");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_pre_export_error.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900226");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::TransportError);
    CHECK(result.Error().message.find("Error in pre-export methods") != std::string::npos);
}

// Crew review F6: only an empty body (older releases) or a "released" report
// is a release. An answer that cannot be read is no proof of one - the step
// cannot be undone, so say so and point to `transport show`.
TEST_CASE("ReleaseTransport: an unreadable release answer is not reported as released",
          "[adt][transport]") {
    for (const char* body : {"<html><body>Gateway timeout</body></html>",
                             "<tm:root xmlns:tm=\"http://www.sap.com/cts/adt/tm\"/>"}) {
        INFO(body);
        MockAdtSession mock;
        EnqueueRequestWithoutTasks(mock, "A4HK900226");
        mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, body}));

        auto result = ReleaseTransport(mock, "A4HK900226");
        REQUIRE(result.IsErr());
        CHECK(result.Error().category == ErrorCategory::TransportError);
        CHECK(result.Error().message.find("transport show A4HK900226") != std::string::npos);
    }
}

// A request is released with its tasks: SAP refuses a request whose task is
// still open ("Referencing task ... not yet released"). The logon user's own
// open tasks are released first; another user's open task stops the release
// before anything is released - releasing cannot be undone, and only the
// task's owner may release it.
TEST_CASE("ReleaseTransport: own open tasks are released first, then the request",
          "[adt][transport]") {
    MockAdtSession mock;
    mock.SetLogonUserName("developer");  // compared case-insensitively
    // Captured from A4H: request A4HK900156 with its open task A4HK900157.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_released.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_released.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900156");
    REQUIRE(result.IsOk());
    CHECK(result.Value().released_tasks == std::vector<std::string>{"A4HK900157"});
    REQUIRE(mock.PostCallCount() == 2);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/A4HK900157/newreleasejobs");
    CHECK(mock.PostCalls()[1].path ==
          "/sap/bc/adt/cts/transportrequests/A4HK900156/newreleasejobs");
}

TEST_CASE("ReleaseTransport: another user's open task stops it before anything is released",
          "[adt][transport]") {
    MockAdtSession mock;
    mock.SetLogonUserName("DEVELOPER");
    auto xml = LoadFixture("transport/transport_show.xml");
    const auto task = xml.find("<tm:task ");
    const auto owner = xml.find("tm:owner=\"DEVELOPER\"", task);
    REQUIRE(owner != std::string::npos);
    xml.replace(owner, std::string("tm:owner=\"DEVELOPER\"").size(), "tm:owner=\"BWDEVELOPER\"");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = ReleaseTransport(mock, "A4HK900156");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::TransportError);
    CHECK(result.Error().message.find("A4HK900157") != std::string::npos);
    CHECK(result.Error().message.find("BWDEVELOPER") != std::string::npos);
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("ReleaseTransport: a task that does not release stops before the request",
          "[adt][transport]") {
    MockAdtSession mock;
    mock.SetLogonUserName("DEVELOPER");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_not_released.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900156");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::TransportError);
    CHECK(mock.PostCallCount() == 1);  // the request was not attempted
}

TEST_CASE("ReleaseTransport: a task number releases just that task", "[adt][transport]") {
    MockAdtSession mock;
    mock.SetLogonUserName("DEVELOPER");
    // Captured from A4H: asked for the task, SAP answers with it beside its request.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show_task.xml")}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/release_task_released.xml")}));

    auto result = ReleaseTransport(mock, "A4HK900157");
    REQUIRE(result.IsOk());
    CHECK(result.Value().released_tasks.empty());
    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path ==
          "/sap/bc/adt/cts/transportrequests/A4HK900157/newreleasejobs");
}

// ===========================================================================
// GetTransport — review a request before releasing it (#76)
// ===========================================================================

TEST_CASE("GetTransport: reads the request and its task", "[adt][transport]") {
    MockAdtSession mock;
    // Captured from A4H: a request from `transport create`, one task, no objects.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show.xml")}));

    auto result = GetTransport(mock, "A4HK900156");
    REQUIRE(result.IsOk());
    const auto& tr = result.Value();
    CHECK(tr.number == "A4HK900156");
    CHECK(tr.description == "erpl-adt transport show probe");
    CHECK(tr.owner == "DEVELOPER");
    CHECK(tr.status == "modifiable");
    CHECK(tr.type == "K");
    CHECK(tr.objects.empty());

    REQUIRE(tr.tasks.size() == 1);
    CHECK(tr.tasks[0].number == "A4HK900157");
    CHECK(tr.tasks[0].owner == "DEVELOPER");
    CHECK(tr.tasks[0].status == "modifiable");
    CHECK(tr.tasks[0].type == "Development/Correction");
    CHECK(tr.tasks[0].objects.empty());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/cts/transportrequests/A4HK900156");
    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.adt.transportorganizer.v1+xml");
}

TEST_CASE("GetTransport: lists the object entries of request and task",
          "[adt][transport]") {
    // Captured from A4H: a released request after test_41 (#72). The request
    // carries the release comment (CORR RELE) and the objects merged into it
    // on release; tm:all_objects repeats them as an aggregate and is not read.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show_objects.xml")}));

    auto result = GetTransport(mock, "A4HK900260");
    REQUIRE(result.IsOk());
    const auto& tr = result.Value();
    CHECK(tr.status == "released");

    REQUIRE(tr.objects.size() == 9);
    CHECK(tr.objects[0].pgmid == "CORR");
    CHECK(tr.objects[0].type == "RELE");
    CHECK(tr.objects[0].description == "Comment Entry: Released");
    CHECK(tr.objects[1].pgmid == "R3TR");
    CHECK(tr.objects[1].type == "DOMA");
    CHECK(tr.objects[1].name == "ZERPT_D_SO_STATUS");
    CHECK(tr.objects[1].description == "Domain");

    REQUIRE(tr.tasks.size() == 1);
    CHECK(tr.tasks[0].number == "A4HK900261");
    CHECK(tr.tasks[0].status == "released");
    const auto& objs = tr.tasks[0].objects;
    REQUIRE(objs.size() == 8);
    CHECK(objs[3].type == "PROG");
    CHECK(objs[3].name == "ZERPT_PO_REPORT");
}

TEST_CASE("GetTransport: an entry locked on an open request says so",
          "[adt][transport]") {
    // Captured from A4H: a package created on a request (#72), still open.
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show_locked.xml")}));

    auto result = GetTransport(mock, "A4HK900220");
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().tasks.size() == 1);
    const auto& objs = result.Value().tasks[0].objects;
    REQUIRE(objs.size() == 1);
    CHECK(objs[0].pgmid == "R3TR");
    CHECK(objs[0].type == "DEVC");
    CHECK(objs[0].name == "ZERPL_TR");
    CHECK(objs[0].lock_status == "X");
}

TEST_CASE("GetTransport: a missing request is NotFound", "[adt][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {404, {}, "<exc:exception xmlns:exc=\"http://www.sap.com/abapxml/types/communicationframework\">"
                  "<message lang=\"EN\">Resource Transport Request/Task A4HK999999 does not exist.</message>"
                  "</exc:exception>"}));

    auto result = GetTransport(mock, "A4HK999999");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
    CHECK(result.Error().ExitCode() == 2);
}

TEST_CASE("GetTransport: a malformed number is refused before any request",
          "[adt][transport]") {
    MockAdtSession mock;
    auto result = GetTransport(mock, "../../oo/classes/x");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Transport ID") != std::string::npos);
    CHECK(mock.GetCallCount() == 0);
}

TEST_CASE("GetTransport: a task number answers with the task", "[adt][transport]") {
    MockAdtSession mock;
    // Captured from A4H for the task: tm:root holds the parent request and,
    // beside it rather than inside, the task (tm:object_type="T").
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("transport/transport_show_task.xml")}));

    auto result = GetTransport(mock, "A4HK900157");
    REQUIRE(result.IsOk());
    const auto& tr = result.Value();
    CHECK(tr.number == "A4HK900157");
    CHECK(tr.owner == "DEVELOPER");
    CHECK(tr.status == "modifiable");
    CHECK(tr.type == "Development/Correction");
    CHECK(tr.tasks.empty());
}
