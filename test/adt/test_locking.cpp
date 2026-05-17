#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/locking.hpp>
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
// LockObject
// ===========================================================================

TEST_CASE("LockObject: parses lock response", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_EXAMPLE").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = LockObject(mock, uri);
    REQUIRE(result.IsOk());

    CHECK(result.Value().handle.Value() == "lock_handle_abc123");
    CHECK(result.Value().transport_number == "NPLK900001");
    CHECK(result.Value().transport_owner == "DEVELOPER");
    CHECK(result.Value().transport_text == "Test transport");
}

TEST_CASE("LockObject: sends POST with LOCK action", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = LockObject(mock, uri);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("_action=LOCK") != std::string::npos);
    CHECK(call.path.find("accessMode=MODIFY") != std::string::npos);
}

TEST_CASE("LockObject: 409 conflict returns LockConflict error", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_LOCKED").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    auto result = LockObject(mock, uri);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 409);
    CHECK(result.Error().category == ErrorCategory::LockConflict);
}

TEST_CASE("LockObject: HTTP error propagated", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_ERR").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"Post", "", std::nullopt, "timeout", std::nullopt}));

    auto result = LockObject(mock, uri);
    REQUIRE(result.IsErr());
}

TEST_CASE("LockObject: 400 session not found adds actionable hint", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_ERR").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {400, {}, "<html><body>Session not found</body></html>"}));

    auto result = LockObject(mock, uri);
    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status == 400);
    REQUIRE(result.Error().hint.has_value());
    CHECK(result.Error().hint->find("--session-file") != std::string::npos);
}

// ===========================================================================
// UnlockObject
// ===========================================================================

TEST_CASE("UnlockObject: sends POST with UNLOCK action", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("my_handle").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto result = UnlockObject(mock, uri, handle);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& call = mock.PostCalls()[0];
    CHECK(call.path.find("_action=UNLOCK") != std::string::npos);
    CHECK(call.path.find("lockHandle=my_handle") != std::string::npos);
}

TEST_CASE("UnlockObject: accepts 204 No Content", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    auto handle = LockHandle::Create("h").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = UnlockObject(mock, uri, handle);
    REQUIRE(result.IsOk());
}

// ===========================================================================
// LockGuard — RAII
// ===========================================================================

TEST_CASE("LockGuard: acquire enables stateful and locks", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    // Unlock on destruction.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    {
        auto guard_result = LockGuard::Acquire(mock, uri);
        REQUIRE(guard_result.IsOk());
        auto guard = std::move(guard_result).Value();

        CHECK(guard.Handle().Value() == "lock_handle_abc123");
        CHECK(mock.IsStateful());
        CHECK(mock.PostCallCount() == 1); // lock call
    }

    // After scope exit: unlock called, stateful disabled.
    CHECK(mock.PostCallCount() == 2); // lock + unlock
    CHECK_FALSE(mock.IsStateful());

    auto& unlock_call = mock.PostCalls()[1];
    CHECK(unlock_call.path.find("_action=UNLOCK") != std::string::npos);
}

TEST_CASE("LockGuard: acquire failure disables stateful", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_LOCKED").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    auto guard_result = LockGuard::Acquire(mock, uri);
    REQUIRE(guard_result.IsErr());
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("LockGuard: move transfers ownership", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""})); // unlock

    auto guard_result = LockGuard::Acquire(mock, uri);
    REQUIRE(guard_result.IsOk());
    auto guard1 = std::move(guard_result).Value();

    // Move to guard2 — guard1 should not unlock.
    auto guard2 = std::move(guard1);
    CHECK(guard2.Handle().Value() == "lock_handle_abc123");
}

TEST_CASE("LockGuard: move chain issues exactly one unlock", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));   // lock
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));    // unlock (exactly once)

    {
        auto r = LockGuard::Acquire(mock, uri);
        REQUIRE(r.IsOk());
        auto g1 = std::move(r).Value();   // owns lock
        auto g2 = std::move(g1);          // g1 moved-from, g2 owns
        auto g3 = std::move(g2);          // g2 moved-from, g3 owns
        // g1 and g2 destructors run here (moved-from, must NOT unlock)
    }
    // g3 destructor: exactly one unlock POST
    CHECK(mock.PostCallCount() == 2); // 1 lock + 1 unlock
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("LockGuard: acquire failure leaves zero unlocks", "[adt][locking]") {
    MockAdtSession mock;
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_LOCKED").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({409, {}, ""}));

    {
        auto r = LockGuard::Acquire(mock, uri);
        REQUIRE(r.IsErr());
        // No LockGuard was constructed — no unlock should be issued.
    }
    CHECK(mock.PostCallCount() == 1); // lock attempt only
    CHECK_FALSE(mock.IsStateful());
}

// Regression: when the unlock RPC fails, the server still considers the lock
// held. Clearing stateful mode would lie about session state — subsequent
// requests would lose the sap-contextid and the held lock would surface as
// a confusing 423 on the next write. Keep stateful mode set so the caller
// sees an honest state and can retry.
TEST_CASE("LockGuard: failed unlock keeps session stateful", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    // Unlock fails (network blip / session expired):
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "server error"}));

    {
        auto r = LockGuard::Acquire(mock, uri);
        REQUIRE(r.IsOk());
        REQUIRE(mock.IsStateful());
        auto guard = std::move(r).Value();
        // guard destructor runs here.
    }
    REQUIRE(mock.PostCallCount() == 2);  // lock + failed unlock
    // Unlock failed → session must remain stateful so caller sees real state.
    CHECK(mock.IsStateful());
}

TEST_CASE("LockGuard: successful unlock clears stateful mode", "[adt][locking]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/lock_response.xml");
    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST").Value();
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));  // unlock OK

    {
        auto r = LockGuard::Acquire(mock, uri);
        REQUIRE(r.IsOk());
        auto guard = std::move(r).Value();
    }
    CHECK(mock.PostCallCount() == 2);
    CHECK_FALSE(mock.IsStateful());
}
