#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_nodes.hpp>
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

BwNodesOptions MakeNodesOptions(const std::string& type, const std::string& name) {
    BwNodesOptions opts;
    opts.object_type = type;
    opts.object_name = name;
    return opts;
}

} // anonymous namespace

// ===========================================================================
// BwGetNodes — success cases
// ===========================================================================

TEST_CASE("BwGetNodes: parses node results", "[adt][bw][nodes]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_nodes.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "ZSALES_DATA"));
    REQUIRE(result.IsOk());

    const auto& items = result.Value();
    REQUIRE(items.size() == 2);
    CHECK(items[0].name == "ZTRFN_SALES");
    CHECK(items[0].type == "TRFN");
    CHECK(items[0].status == "ACT");
    CHECK(items[0].description == "Transformation ZTRFN_SALES");
    CHECK(items[1].name == "ZDTP_SALES");
    CHECK(items[1].type == "DTPA");
    CHECK(items[1].subtype == "STANDARD");
}

TEST_CASE("BwGetNodes: sends correct URL for infoprovider", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "ZSALES_DATA"));
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("/infoproviderstructure/ADSO/ZSALES_DATA") != std::string::npos);
}

TEST_CASE("BwGetNodes: sends correct URL for datasource", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwNodesOptions opts;
    opts.object_type = "RSDS";
    opts.object_name = "ZSOURCE";
    opts.datasource = true;
    auto result = BwGetNodes(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("/datasourcestructure/RSDS/ZSOURCE") != std::string::npos);
}

TEST_CASE("BwGetNodes: sends child filters", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwNodesOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES";
    opts.child_name = "ZTRFN";
    opts.child_type = "TRFN";
    auto result = BwGetNodes(mock, opts);
    REQUIRE(result.IsOk());

    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("childName=ZTRFN") != std::string::npos);
    CHECK(path.find("childType=TRFN") != std::string::npos);
}

TEST_CASE("BwGetNodes: sends Accept atom+xml header", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "TEST"));
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwGetNodes: missing type returns error", "[adt][bw][nodes]") {
    MockAdtSession mock;
    auto result = BwGetNodes(mock, MakeNodesOptions("", "NAME"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("type must not be empty") != std::string::npos);
}

TEST_CASE("BwGetNodes: missing name returns error", "[adt][bw][nodes]") {
    MockAdtSession mock;
    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", ""));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("name must not be empty") != std::string::npos);
}

TEST_CASE("BwGetNodes: empty feed returns empty vector", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "NONEXIST"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwGetNodes: HTTP error propagated", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Internal Error"}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}

TEST_CASE("BwGetNodes: connection error propagated", "[adt][bw][nodes]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/repo/infoproviderstructure/ADSO/TEST",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwGetNodes(mock, MakeNodesOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}
