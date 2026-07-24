#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/rfc_function_module.hpp>
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

const RfcParameter* FindParam(const RfcSignature& sig, const std::string& name) {
    for (const auto& p : sig.parameters) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

} // namespace

TEST_CASE("ParseRfcSignature: parses a real captured BAPI_USER_GET_DETAIL signature",
          "[adt][rfc]") {
    auto source = LoadFixture("abap/rfc_bapi_user_get_detail_source.abap");
    auto sig = ParseRfcSignature(source);

    // 3 importing + 15 exporting + 26 tables = 44 total parameters in the
    // real captured interface.
    REQUIRE(sig.parameters.size() == 44);

    auto* username = FindParam(sig, "USERNAME");
    REQUIRE(username != nullptr);
    CHECK(username->kind == "importing");
    CHECK(username->type_name == "BAPIBNAME");  // "-BAPIBNAME" field suffix stripped
    CHECK(username->is_optional == false);

    auto* extuid = FindParam(sig, "EXTUID_GET");
    REQUIRE(extuid != nullptr);
    CHECK(extuid->kind == "importing");
    CHECK(extuid->type_name == "BAPIEXTUIDGET");
    CHECK(extuid->is_optional == true);

    auto* cache_results = FindParam(sig, "CACHE_RESULTS");
    REQUIRE(cache_results != nullptr);
    REQUIRE(cache_results->default_value.has_value());
    CHECK(*cache_results->default_value == "X");
    CHECK(username->default_value.has_value() == false);  // no default on USERNAME

    auto* logondata = FindParam(sig, "LOGONDATA");
    REQUIRE(logondata != nullptr);
    CHECK(logondata->kind == "exporting");
    CHECK(logondata->type_name == "BAPILOGOND");

    auto* ret = FindParam(sig, "RETURN");
    REQUIRE(ret != nullptr);
    CHECK(ret->kind == "tables");
    CHECK(ret->type_name == "BAPIRET2");
    CHECK(ret->is_optional == false);  // not marked "optional" in the source

    auto* addtel = FindParam(sig, "ADDTEL");
    REQUIRE(addtel != nullptr);
    CHECK(addtel->kind == "tables");
    CHECK(addtel->is_optional == true);
}

TEST_CASE("ParseRfcSignature: stops at the interface terminator, ignoring the body",
          "[adt][rfc]") {
    auto source = LoadFixture("abap/rfc_bapi_user_get_detail_source.abap");
    auto sig = ParseRfcSignature(source);

    // The captured fixture's body (past the interface) declares locals via
    // "data:" statements like "lt_bname type suid_tt_bname" — if the parser
    // didn't stop at the terminating "." it would misread these as more
    // TABLES/CHANGING parameters.
    CHECK(FindParam(sig, "LT_BNAME") == nullptr);
    CHECK(FindParam(sig, "LS_LOGONDATA") == nullptr);
}

TEST_CASE("ParseRfcSignature: empty source yields no parameters", "[adt][rfc]") {
    auto sig = ParseRfcSignature("");
    CHECK(sig.parameters.empty());
}

TEST_CASE("ParseRfcSignature: a FM with no parameters at all", "[adt][rfc]") {
    auto sig = ParseRfcSignature("function z_no_params\n.\n\n  \"body\n");
    CHECK(sig.parameters.empty());
}

TEST_CASE("ListFunctionModules: parses a real captured node-structure listing",
          "[adt][rfc]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        HttpResponse{200, {}, LoadFixture("abap/fmodules_su_user.xml")}));

    auto result = ListFunctionModules(mock, "SU_USER");
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);
    CHECK(result.Value()[0].object_name == "BAPI_USER_GET_DETAIL");
    CHECK(result.Value()[0].object_type == "FUGR/FF");
    CHECK(result.Value()[0].object_uri ==
          "/sap/bc/adt/functions/groups/su_user/fmodules/bapi_user_get_detail");
    CHECK(result.Value()[1].object_name == "BAPI_USER_CHANGE");

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path.find("parent_type=FUGR%2FF") != std::string::npos);
    CHECK(mock.PostCalls()[0].path.find("parent_name=SU_USER") != std::string::npos);
}

TEST_CASE("ListFunctionModules: empty body means no function modules (not an error)",
          "[adt][rfc]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(HttpResponse{200, {}, ""}));

    auto result = ListFunctionModules(mock, "Z_EMPTY_GROUP");
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("GetFunctionModuleSignature: reads source via {uri}/source/main and parses it",
          "[adt][rfc]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(
        HttpResponse{200, {}, LoadFixture("abap/rfc_bapi_user_get_detail_source.abap")}));

    auto result = GetFunctionModuleSignature(
        mock, "/sap/bc/adt/functions/groups/su_user/fmodules/bapi_user_get_detail");
    REQUIRE(result.IsOk());
    CHECK(result.Value().parameters.size() == 44);

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.rfind(
              "/sap/bc/adt/functions/groups/su_user/fmodules/bapi_user_get_detail/source/main",
              0) == 0);
}
