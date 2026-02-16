#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/workflow/lock_workflow.hpp>

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

TEST_CASE("DeleteObjectWithAutoLock: lock/delete/unlock flow succeeds", "[workflow][lock]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, LoadFixture("object/lock_response.xml")}));
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto uri = ObjectUri::Create("/sap/bc/adt/oo/classes/zcl_test").Value();
    auto result = DeleteObjectWithAutoLock(mock, uri, std::nullopt);

    REQUIRE(result.IsOk());
    CHECK(mock.IsStateful() == false);
    CHECK(mock.PostCallCount() == 2);
    CHECK(mock.DeleteCallCount() == 1);
}

TEST_CASE("WriteSourceWithAutoLock: derives object URI and writes source", "[workflow][lock]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, LoadFixture("object/lock_response.xml")}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = WriteSourceWithAutoLock(
        mock,
        "/sap/bc/adt/oo/classes/zcl_test/source/main",
        "CLASS zcl_test DEFINITION.",
        std::nullopt);

    REQUIRE(result.IsOk());
    CHECK(result.Value() == "/sap/bc/adt/oo/classes/zcl_test");
    CHECK(mock.IsStateful() == false);
    CHECK(mock.PostCallCount() == 2);
    CHECK(mock.PutCallCount() == 1);
}

TEST_CASE("WriteSourceWithAutoLock: invalid source URI returns validation error", "[workflow][lock]") {
    MockAdtSession mock;
    auto result = WriteSourceWithAutoLock(
        mock,
        "/sap/bc/adt/oo/classes/zcl_test",
        "CLASS zcl_test DEFINITION.",
        std::nullopt);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("Cannot derive object URI") != std::string::npos);
}

TEST_CASE("WriteSourceWithAutoLock: write failure still unlocks", "[workflow][lock]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, LoadFixture("object/lock_response.xml")}));
    mock.EnqueuePut(Result<HttpResponse, Error>::Err(Error{
        "Put", "/sap/bc/adt/oo/classes/zcl_test/source/main",
        std::nullopt, "write failed", std::nullopt}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = WriteSourceWithAutoLock(
        mock,
        "/sap/bc/adt/oo/classes/zcl_test/source/main",
        "CLASS zcl_test DEFINITION.",
        std::nullopt);

    REQUIRE(result.IsErr());
    CHECK(mock.PostCallCount() == 2);
    CHECK(mock.IsStateful() == false);
}
