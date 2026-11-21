#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/dumps.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// Fixtures captured on A4H (#89): a console class that divides by zero in
// line 9 of its source (line 3 of the method include ...CM001).
namespace {

std::string Fixture(const std::string& name) {
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of("/\\"));
    const auto root = here.substr(0, here.find_last_of("/\\"));
    std::ifstream in(root + "/testdata/dumps/" + name);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* kProbeId =
    "20261121161214vhcala4hci_A4H_00%20%20%20%20%20%20%20%20%20%20%20%20%20%20%20"
    "DEVELOPER%20%20%20001%20%20%20%20%20%20%20%205";

HttpResponse Ok(std::string body) { return HttpResponse{200, {}, std::move(body)}; }

}  // namespace

TEST_CASE("ListDumps: parses the feed, newest first", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    auto result = ListDumps(mock, DumpListOptions{});
    REQUIRE(result.IsOk());
    const auto& dumps = result.Value();
    REQUIRE(dumps.size() == 2);
    CHECK(dumps[0].id == kProbeId);
    CHECK(dumps[0].error == "COMPUTE_INT_ZERODIVIDE");
    CHECK(dumps[0].short_text == "Division by 0 (type I or INT8)");
    CHECK(dumps[0].program == "ZCL_ERPL_DUMP_PROBE===========CP");
    CHECK(dumps[0].user == "DEVELOPER");
    CHECK(dumps[0].time == "2026-11-21T16:12:14Z");
    CHECK(dumps[1].time == "2026-11-21T16:11:07Z");

    REQUIRE(mock.GetCallCount() == 1);
    const auto& call = mock.GetCalls()[0];
    CHECK(call.path == "/sap/bc/adt/runtime/dumps?$top=50");
    CHECK(call.headers.at("Accept") == "application/atom+xml;type=feed");
}

TEST_CASE("ListDumps: owner, since and max shape the request", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    DumpListOptions options;
    options.owner = "developer";
    options.since = "2026-11-21T16:12:00Z";
    options.max = 5;
    auto result = ListDumps(mock, options);
    REQUIRE(result.IsOk());
    // The server filters by user and, a day early, by time (its `from` is in
    // system time); the exact cut is made on the UTC publish time.
    CHECK(mock.GetCalls()[0].path ==
          "/sap/bc/adt/runtime/dumps?$top=5&$query=and%28equals%28user%2cDEVELOPER%29%29"
          "&from=20261120161200");
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].time == "2026-11-21T16:12:14Z");
}

TEST_CASE("ListDumps: an owner that is no user name is refused", "[adt][dumps]") {
    MockAdtSession mock;
    DumpListOptions options;
    options.owner = "DEV),or(x";
    auto result = ListDumps(mock, options);
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Validation);
    CHECK(mock.GetCallCount() == 0);
}

TEST_CASE("ParseDumpSince: the accepted forms", "[adt][dumps]") {
    CHECK(ParseDumpSince("2026-11-21T16:12:00Z").Value() == "2026-11-21T16:12:00Z");
    CHECK(ParseDumpSince("2026-11-21T16:12").Value() == "2026-11-21T16:12:00Z");
    CHECK(ParseDumpSince("2026-11-21").Value() == "2026-11-21T00:00:00Z");
    CHECK(ParseDumpSince("20261121161200").Value() == "2026-11-21T16:12:00Z");
    for (const char* bad : {"yesterday", "2026-13-01", "2026-11-21T25:00", "2026-11-21T16:12:00+01:00", ""}) {
        INFO(bad);
        auto r = ParseDumpSince(bad);
        REQUIRE(r.IsErr());
        CHECK(r.Error().category == ErrorCategory::Validation);
    }
}

TEST_CASE("GetDump: runtime error, exception and where it terminated", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_detail.xml"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_unformatted.txt"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_formatted.txt"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_summary.html"))));
    auto result = GetDump(mock, kProbeId, /*full_text=*/false);
    REQUIRE(result.IsOk());
    const auto& d = result.Value();
    CHECK(d.id == kProbeId);
    CHECK(d.error == "COMPUTE_INT_ZERODIVIDE");
    CHECK(d.exception == "CX_SY_ZERODIVIDE");
    CHECK(d.short_text == "Division by 0 (type I or INT8)");
    CHECK(d.program == "ZCL_ERPL_DUMP_PROBE===========CP");
    CHECK(d.include == "ZCL_ERPL_DUMP_PROBE===========CM001");
    CHECK(d.include_line == 3);
    CHECK(d.event == "IF_OO_ADT_CLASSRUN~MAIN");
    CHECK(d.main_program == "SAPMHTTP");
    CHECK(d.user == "DEVELOPER");
    CHECK(d.time == "2026-11-21T16:12:14Z");
    // The place in the source an agent can open, as ADT resolves it.
    CHECK(d.uri == "/sap/bc/adt/oo/classes/zcl_erpl_dump_probe/source/main");
    CHECK(d.line == 9);
    CHECK(d.text.empty());

    REQUIRE(mock.GetCallCount() == 4);
    const std::string base = std::string("/sap/bc/adt/runtime/dump/") + kProbeId;
    CHECK(mock.GetCalls()[0].path == base);
    CHECK(mock.GetCalls()[1].path == base + "/unformatted");
    CHECK(mock.GetCalls()[2].path == base + "/formatted");
    CHECK(mock.GetCalls()[3].path == base + "/summary");
}

TEST_CASE("GetDump: full_text keeps the formatted dump", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_detail.xml"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_unformatted.txt"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_formatted.txt"))));
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_summary.html"))));
    auto result = GetDump(mock, kProbeId, /*full_text=*/true);
    REQUIRE(result.IsOk());
    CHECK(result.Value().text.find("Division by 0") != std::string::npos);
}

TEST_CASE("GetDump: the id may be given as the full ADT URI", "[adt][dumps]") {
    for (const std::string& given : {std::string("/sap/bc/adt/runtime/dump/") + kProbeId,
                                    std::string("adt://A4H/sap/bc/adt/runtime/dump/") + kProbeId}) {
        auto id = NormalizeDumpId(given);
        REQUIRE(id.IsOk());
        CHECK(id.Value() == kProbeId);
    }
    for (const char* bad : {"", "../oo/classes/x", "a/b", "x y"}) {
        INFO(bad);
        auto id = NormalizeDumpId(bad);
        REQUIRE(id.IsErr());
        CHECK(id.Error().category == ErrorCategory::Validation);
    }
}

TEST_CASE("GetDump: an unknown dump is not found (exit 2)", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(HttpResponse{
        404, {}, "<exc:exception><message>No short dumps match the selection criteria</message></exc:exception>"}));
    auto result = GetDump(mock, "20000101000000x_00%20NOBODY%20001%201", false);
    REQUIRE(result.IsErr());
    CHECK(result.Error().ExitCode() == 2);
}

TEST_CASE("FindDumpSince: the newest dump of a user after a moment", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    auto found = FindDumpSince(mock, "DEVELOPER", "2026-11-21T16:12:10Z");
    REQUIRE(found.has_value());
    CHECK(found->id == kProbeId);

    MockAdtSession later;
    later.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    CHECK_FALSE(FindDumpSince(later, "DEVELOPER", "2026-11-21T16:13:00Z").has_value());

    MockAdtSession failing;
    failing.EnqueueGet(Result<HttpResponse, Error>::Ok(HttpResponse{500, {}, "boom"}));
    CHECK_FALSE(FindDumpSince(failing, "DEVELOPER", "2026-11-21T16:12:10Z").has_value());
}

TEST_CASE("DumpDetailToJson: the documented shape", "[adt][dumps]") {
    DumpDetail d;
    d.id = "X";
    d.error = "COMPUTE_INT_ZERODIVIDE";
    d.line = 9;
    d.uri = "/sap/bc/adt/oo/classes/zcl_x/source/main";
    d.include_line = 3;
    const auto j = DumpDetailToJson(d);
    for (const char* key : {"id", "error", "exception", "short_text", "program", "include",
                            "include_line", "event", "main_program", "user", "time", "uri", "line"}) {
        CHECK(j.contains(key));
    }
    CHECK_FALSE(j.contains("text"));
    d.text = "full";
    CHECK(DumpDetailToJson(d)["text"] == "full");
}

TEST_CASE("DumpTime: UTC in the --since form", "[adt][dumps]") {
    // 2026-11-21T16:12:14Z
    const auto when = std::chrono::system_clock::time_point(std::chrono::seconds(1795277534));
    CHECK(DumpTime(when) == "2026-11-21T16:12:14Z");
    CHECK(ParseDumpSince(DumpTime(when)).Value() == DumpTime(when));
}

TEST_CASE("AttachRunDump: a failed run names the dump of the logon user", "[adt][dumps]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    Error error{"RunClass", "/sap/bc/adt/oo/classrun/ZCL_ERPL_DUMP_PROBE", 500,
                "SAP server error: Application Server Error", std::nullopt};
    AttachRunDump(mock, "2026-11-21T16:12:10Z", error);
    CHECK(error.category == ErrorCategory::RuntimeDump);
    CHECK(error.ExitCode() == 99);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("COMPUTE_INT_ZERODIVIDE (Division by 0 (type I or INT8))") != std::string::npos);
    CHECK(error.hint->find(std::string("erpl-adt dump show ") + kProbeId) != std::string::npos);
    CHECK(mock.GetCalls()[0].path.find("equals%28user%2cDEVELOPER%29") != std::string::npos);

    // No dump since the run started: the error stays what it was.
    MockAdtSession none;
    none.EnqueueGet(Result<HttpResponse, Error>::Ok(Ok(Fixture("dump_feed.xml"))));
    Error other{"RunClass", "", 500, "boom", std::nullopt};
    AttachRunDump(none, "2026-11-21T17:00:00Z", other);
    CHECK(other.category == ErrorCategory::Internal);
    CHECK_FALSE(other.hint.has_value());
}
