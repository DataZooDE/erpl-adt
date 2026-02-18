#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_dataflow.hpp>
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

}  // namespace

TEST_CASE("BwReadDataFlow: parses topology nodes and connections", "[adt][bw][dataflow]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, LoadFixture("bw/bw_object_dmod.xml")}));

    auto result = BwReadDataFlow(mock, "ZDMOD_SALES");
    REQUIRE(result.IsOk());
    const auto& detail = result.Value();
    CHECK(detail.name == "ZDMOD_SALES");
    CHECK(detail.description == "Sales Data Flow");
    REQUIRE(detail.nodes.size() == 4);
    CHECK(detail.nodes[0].id == "N1");
    CHECK(detail.nodes[0].type == "RSDS");
    REQUIRE(detail.connections.size() == 3);
    CHECK(detail.connections[0].from == "N1");
    CHECK(detail.connections[0].to == "N2");
}

TEST_CASE("BwReadDataFlow: sends correct URL and Accept", "[adt][bw][dataflow]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<dmod:dataFlow xmlns:dmod=\"x\"/>"}));

    auto result = BwReadDataFlow(mock, "ZDMOD_SALES", "m");
    REQUIRE(result.IsOk());
    REQUIRE(mock.GetCallCount() == 1);
    const auto& call = mock.GetCalls()[0];
    CHECK(call.path.find("/sap/bw/modeling/dmod/zdmod_sales/m") != std::string::npos);
    CHECK(call.headers.at("Accept") == "application/vnd.sap.bw.modeling.dmod-v1_0_0+xml");
}
