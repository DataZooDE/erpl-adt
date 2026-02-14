#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_xref.hpp>
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

BwXrefOptions MakeXrefOptions(const std::string& type, const std::string& name) {
    BwXrefOptions opts;
    opts.object_type = type;
    opts.object_name = name;
    return opts;
}

} // anonymous namespace

// ===========================================================================
// BwGetXrefs — success cases
// ===========================================================================

TEST_CASE("BwGetXrefs: parses xref results", "[adt][bw][xref]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_xref.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", "ZSALES_DATA"));
    REQUIRE(result.IsOk());

    const auto& items = result.Value();
    REQUIRE(items.size() == 3);
    CHECK(items[0].name == "ZTRFN_SALES");
    CHECK(items[0].type == "TRFN");
    CHECK(items[0].association_type == "001");
    CHECK(items[0].association_label == "Used by");
    CHECK(items[0].description == "Transformation for ZSALES_DATA");
    CHECK(items[1].name == "ZDTP_SALES");
    CHECK(items[1].type == "DTPA");
    CHECK(items[1].association_type == "003");
    CHECK(items[1].association_label == "Depends on");
    CHECK(items[2].name == "0MATERIAL");
    CHECK(items[2].type == "IOBJ");
    CHECK(items[2].association_type == "002");
    CHECK(items[2].association_label == "Uses");
}

TEST_CASE("BwGetXrefs: sends correct URL with all params", "[adt][bw][xref]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    BwXrefOptions opts;
    opts.object_type = "ADSO";
    opts.object_name = "ZSALES_DATA";
    opts.object_version = "A";
    opts.association = "003";
    opts.associated_object_type = "IOBJ";
    auto result = BwGetXrefs(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("objectType=ADSO") != std::string::npos);
    CHECK(path.find("objectName=ZSALES_DATA") != std::string::npos);
    CHECK(path.find("objectVersion=A") != std::string::npos);
    CHECK(path.find("association=003") != std::string::npos);
    CHECK(path.find("associatedObjectType=IOBJ") != std::string::npos);
}

TEST_CASE("BwGetXrefs: sends Accept atom+xml header", "[adt][bw][xref]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", "TEST"));
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwGetXrefs: missing type returns error", "[adt][bw][xref]") {
    MockAdtSession mock;
    auto result = BwGetXrefs(mock, MakeXrefOptions("", "NAME"));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("type must not be empty") != std::string::npos);
}

TEST_CASE("BwGetXrefs: missing name returns error", "[adt][bw][xref]") {
    MockAdtSession mock;
    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", ""));
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("name must not be empty") != std::string::npos);
}

TEST_CASE("BwGetXrefs: empty feed returns empty vector", "[adt][bw][xref]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<feed/>"}));

    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", "NONEXIST"));
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwGetXrefs: HTTP error propagated", "[adt][bw][xref]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Internal Error"}));

    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}

TEST_CASE("BwGetXrefs: connection error propagated", "[adt][bw][xref]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/repo/is/xref",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwGetXrefs(mock, MakeXrefOptions("ADSO", "TEST"));
    REQUIRE(result.IsErr());
}
