#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_transport_collect.hpp>
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

BwTransportCollectOptions MakeCollectOptions(const std::string& type,
                                              const std::string& name) {
    BwTransportCollectOptions opts;
    opts.object_type = type;
    opts.object_name = name;
    return opts;
}

} // anonymous namespace

// ===========================================================================
// BwTransportCollect — success cases
// ===========================================================================

TEST_CASE("BwTransportCollect: parses collect results", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_transport_collect.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", "ZSALES_DATA"));
    REQUIRE(result.IsOk());

    const auto& r = result.Value();
    REQUIRE(r.details.size() == 2);
    CHECK(r.details[0].name == "ZSALES_DATA");
    CHECK(r.details[0].type == "ADSO");
    CHECK(r.details[0].status == "ACT");
    CHECK(r.details[0].last_changed_by == "DEVELOPER");
    CHECK(r.details[1].name == "ZTRFN_SALES");
    CHECK(r.details[1].type == "TRFN");

    REQUIRE(r.dependencies.size() == 2);
    CHECK(r.dependencies[0].name == "0MATERIAL");
    CHECK(r.dependencies[0].type == "IOBJ");
    CHECK(r.dependencies[0].association_type == "002");
    CHECK(r.dependencies[0].associated_name == "ZSALES_DATA");
    CHECK(r.dependencies[1].name == "0CALDAY");

    REQUIRE(r.messages.size() == 1);
    CHECK(r.messages[0] == "Collection completed successfully");
}

TEST_CASE("BwTransportCollect: sends correct URL and body", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "<trCollect:objects xmlns:trCollect=\"http://www.sap.com/bw/trcollect\"/>"}));

    BwTransportCollectOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES_DATA";
    opts.mode = "001";
    opts.transport = "K900001";
    auto result = BwTransportCollect(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& post = mock.PostCalls()[0];
    CHECK(post.path.find("collect=true") != std::string::npos);
    CHECK(post.path.find("mode=001") != std::string::npos);
    CHECK(post.path.find("corrnum=K900001") != std::string::npos);
    CHECK(post.body.find("ZSALES_DATA") != std::string::npos);
    CHECK(post.body.find("ADSO") != std::string::npos);
    CHECK(post.content_type.find("cto") != std::string::npos);
}

TEST_CASE("BwTransportCollect: sends Accept header", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "<trCollect:objects xmlns:trCollect=\"http://www.sap.com/bw/trcollect\"/>"}));

    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", "TEST"));
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].headers.at("Accept") == "application/vnd.sap-bw-modeling.trcollect+xml");
}

TEST_CASE("BwTransportCollect: context headers are forwarded",
          "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<trCollect:objects xmlns:trCollect=\"http://www.sap.com/bw/trcollect\"/>"}));

    BwTransportCollectOptions opts = MakeCollectOptions("ADSO", "TEST");
    opts.transport = "K900001";
    opts.context_headers.transport_lock_holder = "K999999";
    opts.context_headers.foreign_correction_number = "K123456";

    auto result = BwTransportCollect(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    const auto& headers = mock.PostCalls()[0].headers;
    CHECK(headers.at("Transport-Lock-Holder") == "K999999");
    CHECK(headers.at("Foreign-Correction-Number") == "K123456");
}

TEST_CASE("BwTransportCollect: missing type returns error", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    auto result = BwTransportCollect(mock, MakeCollectOptions("", "NAME"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("type must not be empty") != std::string::npos);
}

TEST_CASE("BwTransportCollect: missing name returns error", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", ""));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("name must not be empty") != std::string::npos);
}

TEST_CASE("BwTransportCollect: empty response returns empty result", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", "NONEXIST"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().details.empty());
    CHECK(result.Value().dependencies.empty());
}

TEST_CASE("BwTransportCollect: HTTP error propagated", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "Internal Error"}));

    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}

TEST_CASE("BwTransportCollect: connection error propagated", "[adt][bw][transport][collect]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(Error{
        "Post", "/sap/bw/modeling/cto",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwTransportCollect(mock, MakeCollectOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}
