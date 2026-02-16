#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_transport.hpp>
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
// BwTransportCheck
// ===========================================================================

TEST_CASE("BwTransportCheck: parses transport state", "[adt][bw][transport]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_transport.xml");
    HttpHeaders headers;
    headers["Writing-Enabled"] = "true";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, headers, xml}));

    auto result = BwTransportCheck(mock);
    REQUIRE(result.IsOk());

    const auto& tr = result.Value();
    CHECK(tr.writing_enabled);

    REQUIRE(tr.changeability.size() == 3);
    CHECK(tr.changeability[0].tlogo == "ADSO");
    CHECK(tr.changeability[0].transportable);
    CHECK(tr.changeability[0].changeable);

    REQUIRE(tr.requests.size() == 2);
    CHECK(tr.requests[0].number == "NPLK900001");
    CHECK(tr.requests[0].function_type == "K");
    CHECK(tr.requests[0].description == "BW Dev Request");
    REQUIRE(tr.requests[0].tasks.size() == 1);
    CHECK(tr.requests[0].tasks[0].number == "NPLK900002");
    CHECK(tr.requests[0].tasks[0].owner == "DEVELOPER");

    REQUIRE(tr.objects.size() == 1);
    CHECK(tr.objects[0].name == "ZSALES_DATA");
    CHECK(tr.objects[0].type == "ADSO");
    CHECK(tr.objects[0].lock_request == "NPLK900001");
}

TEST_CASE("BwTransportCheck: sends correct URL", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<bwCTO:transport/>"}));

    auto result = BwTransportCheck(mock);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find("/sap/bw/modeling/cto") != std::string::npos);
    CHECK(mock.GetCalls()[0].path.find("rddetails=all") != std::string::npos);
}

TEST_CASE("BwTransportCheck: own-only flag", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<bwCTO:transport/>"}));

    auto result = BwTransportCheck(mock, true);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("ownonly=true") != std::string::npos);
}

TEST_CASE("BwTransportCheck: advanced check flags are encoded", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<bwCTO:transport/>"}));

    BwTransportCheckOptions opts;
    opts.read_details = "objs";
    opts.read_properties = true;
    opts.own_only = true;
    opts.all_messages = true;
    auto result = BwTransportCheck(mock, opts);
    REQUIRE(result.IsOk());

    const auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("rddetails=objs") != std::string::npos);
    CHECK(path.find("rdprops=true") != std::string::npos);
    CHECK(path.find("ownonly=true") != std::string::npos);
    CHECK(path.find("allmsgs=true") != std::string::npos);
}

TEST_CASE("BwTransportCheck: sends CTO Accept header", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<bwCTO:transport/>"}));

    auto result = BwTransportCheck(mock);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") ==
          "application/vnd.sap.bw.modeling.cto-v1_1_0+xml");
}

TEST_CASE("BwTransportCheck: HTTP error propagated", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwTransportCheck(mock);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwTransportWrite
// ===========================================================================

TEST_CASE("BwTransportWrite: sends correct request", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwTransportWriteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.transport = "K900001";
    opts.package_name = "ZTEST";

    auto result = BwTransportWrite(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().success);

    REQUIRE(mock.PostCallCount() == 1);
    auto& post = mock.PostCalls()[0];
    CHECK(post.path.find("corrnum=K900001") != std::string::npos);
    CHECK(post.path.find("package=ZTEST") != std::string::npos);
    CHECK(post.body.find("ZSALES") != std::string::npos);
    CHECK(post.body.find("ADSO") != std::string::npos);
}

TEST_CASE("BwTransportWrite: simulate flag", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwTransportWriteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.transport = "K900001";
    opts.simulate = true;

    auto result = BwTransportWrite(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("simulate=true") != std::string::npos);
}

TEST_CASE("BwTransportWrite: allmsgs flag", "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwTransportWriteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.transport = "K900001";
    opts.all_messages = true;

    auto result = BwTransportWrite(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("allmsgs=true") != std::string::npos);
}

TEST_CASE("BwTransportWrite: context headers are forwarded",
          "[adt][bw][transport]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwTransportWriteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.transport = "K900001";
    opts.context_headers.foreign_objects = "ADSO:ZOTHER";
    opts.context_headers.foreign_package = "ZPKG";

    auto result = BwTransportWrite(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& headers = mock.PostCalls()[0].headers;
    CHECK(headers.at("Transport-Lock-Holder") == "K900001");
    CHECK(headers.at("Foreign-Objects") == "ADSO:ZOTHER");
    CHECK(headers.at("Foreign-Package") == "ZPKG");
}

TEST_CASE("BwTransportWrite: empty transport returns error", "[adt][bw][transport]") {
    MockAdtSession mock;
    BwTransportWriteOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";

    auto result = BwTransportWrite(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::TransportError);
}
