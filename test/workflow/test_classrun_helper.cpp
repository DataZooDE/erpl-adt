#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/classrun_helper.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <cctype>
#include <set>

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

const char* kActivationCancelledXml =
    R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:messages xmlns:chkl="http://www.sap.com/abapxml/checklist">
  <chkl:properties checkExecuted="true" activationExecuted="false" generationExecuted="false"/>
  <msg type="E" line="1" href="/sap/bc/adt/oo/classes/zcl_erplhlp_00000001/source/main#start=10,1">
    <shortText><txt>Field "ZZ" is unknown.</txt></shortText>
  </msg>
</chkl:messages>)";

void EnqueueExistsWrite(MockAdtSession& mock) {
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockResponseXml}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));
}

void EnqueueActivation(MockAdtSession& mock, const char* xml) {
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-1")));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
}

void EnqueueRun(MockAdtSession& classrun_mock, const std::string& output) {
    classrun_mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    classrun_mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, output}));
}

void EnqueueDelete(MockAdtSession& mock) {
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockResponseXml}));
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));
}

ClassrunHelperOptions Options() {
    ClassrunHelperOptions o;
    o.helper_class = "ZCL_ERPLHLP_00000001";
    return o;
}

}  // namespace

TEST_CASE("MakeHelperClassName: ZCL_ERPLHLP_ + 8 hex digits, unique per call",
          "[workflow][classrun_helper]") {
    std::set<std::string> names;
    for (int i = 0; i < 50; ++i) {
        auto name = MakeHelperClassName();
        REQUIRE(name.size() == 20);
        CHECK(name.rfind("ZCL_ERPLHLP_", 0) == 0);
        for (char c : name.substr(12)) {
            CHECK(std::isxdigit(static_cast<unsigned char>(c)));
            CHECK_FALSE(std::islower(static_cast<unsigned char>(c)));
        }
        names.insert(name);
    }
    CHECK(names.size() == 50);
}

TEST_CASE("RunClassrunHelper: refuses a transportable helper package",
          "[workflow][classrun_helper]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    auto options = Options();
    options.package = "ZDEMO";

    auto result = RunClassrunHelper(mock, classrun_mock, options, "CLASS ...");

    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("local package") != std::string::npos);
    CHECK(mock.GetCallCount() == 0);
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("RunClassrunHelper: a helper that fails activation is never run, but is deleted",
          "[workflow][classrun_helper]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueExistsWrite(mock);
    EnqueueActivation(mock, kActivationCancelledXml);
    EnqueueDelete(mock);

    auto result = RunClassrunHelper(mock, classrun_mock, Options(), "CLASS ...");

    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::ActivationError);
    CHECK(result.Error().message.find("Field \"ZZ\" is unknown.") != std::string::npos);
    CHECK(classrun_mock.PostCallCount() == 0);  // the stale active version must not run
    CHECK(mock.DeleteCallCount() == 1);
}

TEST_CASE("ClassrunEngineRunner: parses results and deletes its helper exactly once",
          "[workflow][classrun_helper]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueExistsWrite(mock);
    EnqueueActivation(mock, kActivationSuccessXml);
    EnqueueRun(classrun_mock, "@1 EXISTS 0\n@1 RC 0\n@END\n");
    // second run reuses the class: write + activate only (no existence check)
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockResponseXml}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    EnqueueActivation(mock, kActivationSuccessXml);
    EnqueueRun(classrun_mock, "@1 RC 0\n@2 RC 4\n@END\n");
    EnqueueDelete(mock);

    {
        ClassrunEngineRunner runner(mock, classrun_mock, Options());
        auto first = runner.Run({DomaGetOp("ZERPL_D_SO_STATUS", "E")});
        REQUIRE(first.IsOk());
        CHECK(first.Value()[0].exists == std::optional<bool>(false));

        auto second = runner.Run({DomaGetOp("ZA", "E"), DomaDeleteOp("ZB", "")});
        REQUIRE(second.IsOk());
        CHECK(second.Value()[1].rc == 4);
        CHECK(mock.DeleteCallCount() == 0);
    }
    CHECK(mock.DeleteCallCount() == 1);
}

TEST_CASE("ClassrunEngineRunner: keep_helper leaves the class in place",
          "[workflow][classrun_helper]") {
    MockAdtSession mock;
    MockAdtSession classrun_mock;
    EnqueueExistsWrite(mock);
    EnqueueActivation(mock, kActivationSuccessXml);
    EnqueueRun(classrun_mock, "@1 RC 0\n@END\n");
    auto options = Options();
    options.keep_helper = true;
    {
        ClassrunEngineRunner runner(mock, classrun_mock, options);
        REQUIRE(runner.Run({DomaGetOp("ZA", "E")}).IsOk());
    }
    CHECK(mock.DeleteCallCount() == 0);
}
