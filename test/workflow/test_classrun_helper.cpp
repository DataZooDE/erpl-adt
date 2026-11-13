#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/classrun_helper.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <cctype>
#include <set>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Ops in the shape of the DOMA part (src/abap/engine_doma.abap); the handler
// builds the real ones in src/workflow/abapgit_type_doma.cpp.
EngineOp DomaGetOp(const std::string& name, const std::string& language) {
    return {"doma_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}
EngineOp DomaDeleteOp(const std::string& name, const std::string& trkorr) {
    return {"doma_delete( name = " + AbapQuote(name) + " trkorr = " + AbapQuote(trkorr) + " ).",
            ""};
}

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

// ---------------------------------------------------------------------------
// Stale helper detection (cleanup after interrupted runs)
// ---------------------------------------------------------------------------

namespace {
ObjectInfo Helper(const std::string& name, const std::string& owner, const std::string& changed) {
    ObjectInfo info;
    info.name = name;
    info.type = "CLAS/OC";
    info.responsible = owner;
    info.changed_at = changed;
    return info;
}
std::chrono::system_clock::time_point At(const std::string& iso) {
    auto t = ParseAdtTimestamp(iso);
    REQUIRE(t.has_value());
    return *t;
}
}  // namespace

TEST_CASE("ParseAdtTimestamp: ADT UTC timestamps", "[workflow][classrun_helper]") {
    auto a = ParseAdtTimestamp("2026-11-08T11:57:46Z");
    auto b = ParseAdtTimestamp("2026-11-08T12:57:46Z");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(std::chrono::duration_cast<std::chrono::minutes>(*b - *a).count() == 60);
    CHECK(std::chrono::system_clock::to_time_t(*a) == 1794139066);
    CHECK_FALSE(ParseAdtTimestamp("").has_value());
    CHECK_FALSE(ParseAdtTimestamp("yesterday").has_value());
}

TEST_CASE("IsStaleHelper: only own, old, genuine helper classes", "[workflow][classrun_helper]") {
    const auto now = At("2026-11-08T14:00:00Z");
    const std::chrono::minutes age{60};
    CHECK(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD", "DEVELOPER", "2026-11-08T11:57:46Z"),
                        "DEVELOPER", now, age));
    // too young: may belong to a run in progress
    CHECK_FALSE(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD", "DEVELOPER", "2026-11-08T13:30:00Z"),
                              "DEVELOPER", now, age));
    // someone else's helper
    CHECK_FALSE(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD", "COLLEAGUE", "2026-11-08T11:00:00Z"),
                              "DEVELOPER", now, age));
    // owner comparison is case-insensitive
    CHECK(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD", "developer", "2026-11-08T11:00:00Z"),
                        "DEVELOPER", now, age));
    // names that merely look similar are never touched
    CHECK_FALSE(IsStaleHelper(Helper("ZCL_ERPLHLP_MYCLASS", "DEVELOPER", "2026-11-08T11:00:00Z"),
                              "DEVELOPER", now, age));
    CHECK_FALSE(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD1", "DEVELOPER", "2026-11-08T11:00:00Z"),
                              "DEVELOPER", now, age));
    // unknown change time: keep
    CHECK_FALSE(IsStaleHelper(Helper("ZCL_ERPLHLP_017E06DD", "DEVELOPER", ""), "DEVELOPER", now, age));
}
