#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/screen_workflow.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

const char* kLockResponseXml =
    R"(<?xml version="1.0" encoding="UTF-8"?>
<asx:abap xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <LOCK_HANDLE>lock_handle_abc123</LOCK_HANDLE>
      <CORRNR></CORRNR>
      <CORRUSER></CORRUSER>
      <CORRTEXT></CORRTEXT>
    </DATA>
  </asx:values>
</asx:abap>)";

const char* kActivationSuccessXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:activationResultList xmlns:chkl="http://www.sap.com/adt/checklistresult">
</chkl:activationResultList>)";

// Enqueues on `mock`: helper class exists (GET 200) + write + activate.
// Enqueues on `classrun_mock` (a *separate* session, matching the real CLI
// wiring — see workflow/screen_workflow.hpp): RunClass's own EnsureObjectExists
// GET + the classrun POST returning `console_output`.
void EnqueueHelperExistsWriteActivate(MockAdtSession& mock) {
    // EnsureHelperClass: GET helper class -> exists.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    // WriteSourceWithAutoLock: lock, write, unlock.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockResponseXml}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    // ActivateObject: CSRF fetch + POST.
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-1")));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kActivationSuccessXml}));
}

void EnqueueRunClass(MockAdtSession& classrun_mock, const std::string& console_output) {
    // RunClass: EnsureObjectExists GET + classrun POST.
    classrun_mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    classrun_mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, console_output}));
}

void EnqueueCleanupDelete(MockAdtSession& mock) {
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockResponseXml}));
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));
}

ScreenWorkflowTarget MakeTarget() {
    ScreenWorkflowTarget target;
    target.program = "ZJR66_HOST";
    target.screen = "9001";
    target.package = "$TMP";
    target.helper_class = "ZJR66_HOST_SCRHLP";
    target.keep_helper = false;
    return target;
}

}  // namespace

TEST_CASE("RunScreenCreateWorkflow: RC=0 succeeds and cleans up the helper class",
          "[workflow][screen]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueHelperExistsWriteActivate(mock);
    EnqueueRunClass(classrun_mock, "RC=0\n");
    EnqueueCleanupDelete(mock);

    ScreenCreateWorkflowParams params;
    params.target = MakeTarget();
    params.description = "Disposable test screen";
    params.fields = {
        ScreenFieldSpec{"HEADERNAME", "Header Name", 5, 3, 30},
        ScreenFieldSpec{"VALUE", "Value", 7, 3, 30},
    };

    auto result = RunScreenCreateWorkflow(mock, classrun_mock, params);

    REQUIRE(result.IsOk());
    CHECK(mock.DeleteCallCount() == 1);  // helper class cleaned up

    // The generated source actually reached the backend with both field names.
    REQUIRE(mock.PutCalls().size() == 1);
    CHECK(mock.PutCalls()[0].body.find("'HEADERNAME'") != std::string::npos);
    CHECK(mock.PutCalls()[0].body.find("'VALUE'") != std::string::npos);
}

TEST_CASE("RunScreenCreateWorkflow: a non-zero ABAP return code is an Err, never success",
          "[workflow][screen]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueHelperExistsWriteActivate(mock);
    // Captured live: HEADER-NEXTSCREEN='0' is illegal on this system.
    EnqueueRunClass(classrun_mock, "RC=6\nMSG=XI/E/111:DYNP_HEADER|NEXTSCREEN|0|\n");
    EnqueueCleanupDelete(mock);  // cleanup still runs on the failure path

    ScreenCreateWorkflowParams params;
    params.target = MakeTarget();
    params.fields = {ScreenFieldSpec{"HEADERNAME", "Header Name", 5, 3, 30}};

    auto result = RunScreenCreateWorkflow(mock, classrun_mock, params);

    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::ActivationError);
    CHECK(mock.DeleteCallCount() == 1);  // still cleaned up despite the failure
}

TEST_CASE("RunScreenCreateWorkflow: --keep-helper skips deleting the helper class",
          "[workflow][screen]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueHelperExistsWriteActivate(mock);
    EnqueueRunClass(classrun_mock, "RC=0\n");
    // No cleanup enqueued — keep_helper must skip it entirely.

    ScreenCreateWorkflowParams params;
    params.target = MakeTarget();
    params.target.keep_helper = true;
    params.fields = {ScreenFieldSpec{"HEADERNAME", "Header Name", 5, 3, 30}};

    auto result = RunScreenCreateWorkflow(mock, classrun_mock, params);

    REQUIRE(result.IsOk());
    CHECK(mock.DeleteCallCount() == 0);
}

TEST_CASE("RunScreenReadWorkflow: parses HEADERNAME and VALUE fields, "
          "dropping the synthetic OK-code field",
          "[workflow][screen]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueHelperExistsWriteActivate(mock);
    std::string console_output =
        "RC=0\n"
        "HEADER=ZJR66_HOST|9001|024|083\n"
        "FIELD=HEADERNAME|005|003|030|X|\n"
        "FIELD=VALUE|007|003|030|X|\n"
        "FIELD=|000|000|020|X|\n";
    EnqueueRunClass(classrun_mock, console_output);
    EnqueueCleanupDelete(mock);

    auto result = RunScreenReadWorkflow(mock, classrun_mock, MakeTarget());

    REQUIRE(result.IsOk());
    const auto& screen = result.Value();
    CHECK(screen.program == "ZJR66_HOST");
    REQUIRE(screen.fields.size() == 2);
    CHECK(screen.fields[0].name == "HEADERNAME");
    CHECK(screen.fields[1].name == "VALUE");
}

TEST_CASE("RunScreenDeleteWorkflow: RC=0 succeeds", "[workflow][screen]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueHelperExistsWriteActivate(mock);
    EnqueueRunClass(classrun_mock, "RC=0\n");
    EnqueueCleanupDelete(mock);

    auto result = RunScreenDeleteWorkflow(mock, classrun_mock, MakeTarget());

    REQUIRE(result.IsOk());
}
