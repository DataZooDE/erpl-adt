#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/classic_engine.hpp>

#include <string>
#include <vector>

using namespace erpl_adt;

namespace {

std::string Between(const std::string& s, const std::string& from, const std::string& to) {
    auto a = s.find(from);
    if (a == std::string::npos) {
        return "";
    }
    a += from.size();
    auto b = s.find(to, a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

}  // namespace

// ---------------------------------------------------------------------------
// Source generation
// ---------------------------------------------------------------------------

TEST_CASE("Engine source: class name placeholder is replaced everywhere", "[classic_engine]") {
    auto src = BuildEngineSource("ZCL_ERPLHLP_1A2B3C4D", {DomaGetOp("ZERPL_D_SO_STATUS", "E")});
    CHECK(src.find("zcl_erpl_classic_engine") == std::string::npos);
    CHECK(src.find("CLASS zcl_erplhlp_1a2b3c4d DEFINITION") != std::string::npos);
    CHECK(src.find("CLASS zcl_erplhlp_1a2b3c4d IMPLEMENTATION") != std::string::npos);
}

TEST_CASE("Engine source: per-type parts are composed into the class", "[classic_engine]") {
    auto src = BuildEngineSource("ZCL_H", {});
    // The DOMA part (src/abap/engine_doma.abap) contributes definition and implementation.
    auto def = src.find("METHODS doma_get");
    auto impl = src.find("METHOD doma_get.");
    auto impl_start = src.find("CLASS zcl_h IMPLEMENTATION.");
    REQUIRE(def != std::string::npos);
    REQUIRE(impl != std::string::npos);
    CHECK(def < impl_start);
    CHECK(impl > impl_start);
    CHECK(src.find("@@") == std::string::npos);
}

TEST_CASE("Engine source: operations are numbered and called in order", "[classic_engine]") {
    auto src = BuildEngineSource("ZCL_H", {DomaGetOp("ZA", "E"), DomaDeleteOp("ZB", "")});
    auto ops = Between(src, "METHOD run_ops.", "ENDMETHOD.");
    auto first = ops.find("op = '1'.");
    auto get = ops.find("doma_get( name = 'ZA' language = 'E' ).");
    auto second = ops.find("op = '2'.");
    auto del = ops.find("doma_delete( name = 'ZB' trkorr = '' ).");
    REQUIRE(first != std::string::npos);
    REQUIRE(get != std::string::npos);
    REQUIRE(second != std::string::npos);
    REQUIRE(del != std::string::npos);
    CHECK(first < get);
    CHECK(get < second);
    CHECK(second < del);
}

TEST_CASE("Engine source: payload travels as base64 chunks of at most 200 chars", "[classic_engine]") {
    std::string xml(1000, 'x');
    xml += "</DD01V>";
    auto src = BuildEngineSource("ZCL_H", {DomaPutOp("ZA", "E", "$TMP", "", xml)});
    auto ops = Between(src, "METHOD run_ops.", "ENDMETHOD.");
    CHECK(ops.find("CLEAR chunks.") != std::string::npos);
    size_t appends = 0;
    size_t pos = 0;
    while ((pos = ops.find("APPEND `", pos)) != std::string::npos) {
        auto end = ops.find("` TO chunks.", pos);
        REQUIRE(end != std::string::npos);
        CHECK(end - (pos + 8) <= 200);
        ++appends;
        pos = end;
    }
    // 1008 bytes -> 1344 base64 chars -> 7 chunks
    CHECK(appends == 7);
    // The raw XML never appears in the ABAP source: no quoting problems possible.
    CHECK(ops.find("</DD01V>") == std::string::npos);
    CHECK(ops.find("doma_put( name = 'ZA' language = 'E' devclass = '$TMP' trkorr = '' ).") !=
          std::string::npos);
}

TEST_CASE("Engine source: string arguments are ABAP-quoted", "[classic_engine]") {
    auto src = BuildEngineSource("ZCL_H", {DomaGetOp("Z'X", "E")});
    CHECK(src.find("name = 'Z''X'") != std::string::npos);
}

TEST_CASE("Engine source: base64 round trip of payload bytes", "[classic_engine]") {
    const std::string bytes = "<?xml version=\"1.0\"?>\xC3\xBC\x00\xFF end";
    CHECK(DecodeBase64(EncodeBase64(bytes)) == bytes);
    CHECK(EncodeBase64("Man") == "TWFu");
    CHECK(EncodeBase64("Ma") == "TWE=");
    CHECK(EncodeBase64("M") == "TQ==");
}

// ---------------------------------------------------------------------------
// Output parsing
// ---------------------------------------------------------------------------

TEST_CASE("Engine output: DATA chunks of one tag are concatenated and decoded", "[classic_engine]") {
    const std::string xml = "<DD01V><DOMNAME>ZA</DOMNAME></DD01V>";
    const std::string b64 = EncodeBase64(xml);
    std::string output = "@1 EXISTS 1\n@1 META DEVELOPER 20261108 104258\n";
    output += "@1 DATA XML " + b64.substr(0, 10) + "\n";
    output += "@1 DATA XML " + b64.substr(10) + "\n";
    output += "@1 RC 0\n@END\n";

    auto parsed = ParseEngineOutput(output, 1);
    REQUIRE(parsed.IsOk());
    const auto& r = parsed.Value()[0];
    CHECK(r.rc == 0);
    REQUIRE(r.exists.has_value());
    CHECK(*r.exists);
    CHECK(r.changed_by == "DEVELOPER");
    CHECK(r.changed_on == "20261108");
    CHECK(r.data.at("XML") == xml);
}

TEST_CASE("Engine output: SAP messages and activation errors are collected", "[classic_engine]") {
    const std::string output =
        "@1 MSG E TK 115 Object DOMA ZA is locked in request A4HK900012 of user OTHER\n"
        "@1 RC 2\n"
        "@2 ERR Field ZFOO is unknown\n"
        "@2 RC 8\n"
        "@END\n";
    auto parsed = ParseEngineOutput(output, 2);
    REQUIRE(parsed.IsOk());
    const auto& r1 = parsed.Value()[0];
    CHECK(r1.rc == 2);
    REQUIRE(r1.messages.size() == 1);
    CHECK(r1.messages[0].type == "E");
    CHECK(r1.messages[0].id == "TK");
    CHECK(r1.messages[0].number == "115");
    CHECK(r1.messages[0].text == "Object DOMA ZA is locked in request A4HK900012 of user OTHER");
    CHECK(parsed.Value()[1].errors == std::vector<std::string>{"Field ZFOO is unknown"});
}

TEST_CASE("Engine output: a missing @END means the helper died mid-run", "[classic_engine]") {
    auto parsed = ParseEngineOutput("@1 RC 0\n", 1);
    REQUIRE(parsed.IsErr());
    CHECK(parsed.Error().message.find("did not finish") != std::string::npos);
}

TEST_CASE("Engine output: an operation without RC is an error, not success", "[classic_engine]") {
    auto parsed = ParseEngineOutput("@1 RC 0\n@END\n", 2);
    REQUIRE(parsed.IsErr());
    CHECK(parsed.Error().message.find("operation 2") != std::string::npos);
}

TEST_CASE("Engine output: an ABAP exception is reported with its text", "[classic_engine]") {
    auto parsed = ParseEngineOutput("@1 EXC CX_SY_DYN_CALL_PARAM_NOT_FOUND raised\n@END\n", 1);
    REQUIRE(parsed.IsErr());
    CHECK(parsed.Error().message.find("CX_SY_DYN_CALL_PARAM_NOT_FOUND") != std::string::npos);
}

TEST_CASE("Engine output: CRLF line endings and noise lines are tolerated", "[classic_engine]") {
    auto parsed = ParseEngineOutput("some console noise\r\n@1 EXISTS 0\r\n@1 RC 0\r\n@END\r\n", 1);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value()[0].exists == std::optional<bool>(false));
}

TEST_CASE("Engine output: SummarizeFailure prefers SAP messages over return codes",
          "[classic_engine]") {
    EngineOpResult r;
    r.rc = 2;
    r.messages.push_back({"E", "TK", "115", "Object locked in request A4HK900012"});
    r.errors.push_back("activation failed");
    auto text = SummarizeFailure(r);
    CHECK(text.find("Object locked in request A4HK900012") != std::string::npos);
    CHECK(text.find("activation failed") != std::string::npos);

    EngineOpResult bare;
    bare.rc = 4;
    CHECK(SummarizeFailure(bare).find("return code 4") != std::string::npos);
}

TEST_CASE("Engine output: other kinds are kept as values", "[classic_engine]") {
    auto parsed = ParseEngineOutput(
        "@1 META DEVELOPER 20261108 120000\n@1 NS /ACME/\n@1 NS /DZOO/\n@1 RC 0\n@END\n", 1);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value()[0].changed_by == "DEVELOPER");
    CHECK(parsed.Value()[0].values.at("NS") == std::vector<std::string>{"/ACME/", "/DZOO/"});
}

TEST_CASE("Engine source: DDIC deletes record the deletion on the given request",
          "[classic_engine]") {
    // RS_DD_DELETE_OBJ's own CHANGING corrnum feeds its internal corr_insert
    // (the CTS entry for the deletion); without it SAP picks or asks.
    const auto src = BuildEngineSource("ZCL_H", {});
    for (const char* method : {"METHOD doma_delete.", "METHOD dtel_delete."}) {
        const auto begin = src.find(method);
        REQUIRE(begin != std::string::npos);
        const auto body = src.substr(begin, src.find("ENDMETHOD.", begin) - begin);
        CHECK(body.find("corrnum") != std::string::npos);
        CHECK(body.find("= trkorr") != std::string::npos);
    }
}
