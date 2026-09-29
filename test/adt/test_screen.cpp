#include <catch2/catch_test_macros.hpp>
#include <erpl_adt/adt/screen.hpp>

using namespace erpl_adt;

namespace {

std::vector<ScreenFieldSpec> TwoFields() {
    return {
        ScreenFieldSpec{"HEADERNAME", "Header Name", 5, 3, 30},
        ScreenFieldSpec{"VALUE", "Value", 7, 3, 30},
    };
}

}  // namespace

TEST_CASE("GenerateScreenCreateSource embeds program, screen and both field names",
          "[screen]") {
    auto source = GenerateScreenCreateSource(
        "ZERPL_ADT_SCREEN_HELPER", "ZJR66_DYNPRO_HOST", "9001",
        "Disposable test screen", "9001", 24, 83, TwoFields());

    REQUIRE(source.find("CLASS zerpl_adt_screen_helper") != std::string::npos);
    REQUIRE(source.find("IF_OO_ADT_CLASSRUN") != std::string::npos);
    REQUIRE(source.find("'ZJR66_DYNPRO_HOST'") != std::string::npos);
    REQUIRE(source.find("'9001'") != std::string::npos);
    REQUIRE(source.find("'HEADERNAME'") != std::string::npos);
    REQUIRE(source.find("'VALUE'") != std::string::npos);
    // The root container row is mandatory — RPY_DYNPRO_INSERT silently drops
    // every field when CONTAINERS is empty.
    REQUIRE(source.find("APPEND wa_cont TO containers") != std::string::npos);
    // Field type must be the plain-field kind, not blank.
    REQUIRE(source.find("'TEXT'") != std::string::npos);
    REQUIRE(source.find("RPY_DYNPRO_INSERT") != std::string::npos);
}

TEST_CASE("GenerateScreenCreateSource escapes single quotes in text", "[screen]") {
    std::vector<ScreenFieldSpec> fields = {
        ScreenFieldSpec{"HEADERNAME", "Don't panic", 5, 3, 30},
    };
    auto source = GenerateScreenCreateSource("ZHELPER", "ZPROG", "9001",
                                             "It's a test", "9001", 24, 83,
                                             fields);
    // ABAP escapes an embedded quote by doubling it.
    REQUIRE(source.find("Don''t panic") != std::string::npos);
    REQUIRE(source.find("It''s a test") != std::string::npos);
}

TEST_CASE("GenerateScreenReadSource and GenerateScreenDeleteSource embed program/screen",
          "[screen]") {
    auto read_src = GenerateScreenReadSource("ZHELPER", "ZPROG", "9001");
    REQUIRE(read_src.find("RPY_DYNPRO_READ") != std::string::npos);
    REQUIRE(read_src.find("'ZPROG'") != std::string::npos);
    REQUIRE(read_src.find("'9001'") != std::string::npos);

    auto delete_src = GenerateScreenDeleteSource("ZHELPER", "ZPROG", "9001");
    REQUIRE(delete_src.find("RS_SCRP_DELETE") != std::string::npos);
    REQUIRE(delete_src.find("'ZPROG'") != std::string::npos);
    REQUIRE(delete_src.find("'9001'") != std::string::npos);
}

TEST_CASE("ParseScreenCreateOutput reports success on RC=0", "[screen]") {
    auto result = ParseScreenCreateOutput("RC=0\n");
    REQUIRE(result.IsOk());
}

TEST_CASE("ParseScreenCreateOutput surfaces the SAP message on failure, never success",
          "[screen]") {
    // Captured live: HEADER-NEXTSCREEN='0' is illegal on this system.
    auto result = ParseScreenCreateOutput(
        "RC=6\nMSG=XI/E/111:DYNP_HEADER|NEXTSCREEN|0|\n");
    REQUIRE(result.IsErr());
    const auto& err = result.Error();
    CHECK(err.category == ErrorCategory::ActivationError);
    CHECK(err.message.find("DYNP_HEADER") != std::string::npos);
    CHECK(err.message.find("NEXTSCREEN") != std::string::npos);
}

TEST_CASE("ParseScreenCreateOutput rejects output with no recognizable RC line",
          "[screen]") {
    // Guards against ever reading unstructured/garbled output as success.
    auto result = ParseScreenCreateOutput("something unexpected happened");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::ActivationError);
}

TEST_CASE("ParseScreenReadOutput parses header and both named fields, "
          "dropping the synthetic OK-code field",
          "[screen]") {
    std::string output =
        "RC=0\n"
        "HEADER=ZJR66_DYNPRO_HOST|9001|024|083\n"
        "FIELD=HEADERNAME|005|003|030|X|\n"
        "FIELD=VALUE|007|003|030|X|\n"
        "FIELD=|000|000|020|X|\n";  // synthetic OK-code-like field, blank name

    auto result = ParseScreenReadOutput(output);
    REQUIRE(result.IsOk());
    const auto& screen = result.Value();
    CHECK(screen.program == "ZJR66_DYNPRO_HOST");
    CHECK(screen.screen == "9001");
    CHECK(screen.lines == 24);
    CHECK(screen.columns == 83);
    REQUIRE(screen.fields.size() == 2);
    CHECK(screen.fields[0].name == "HEADERNAME");
    CHECK(screen.fields[0].line == 5);
    CHECK(screen.fields[0].column == 3);
    CHECK(screen.fields[0].length == 30);
    CHECK(screen.fields[0].input);
    CHECK(screen.fields[1].name == "VALUE");
}

TEST_CASE("ParseScreenReadOutput reports not-found as an error, not an empty screen",
          "[screen]") {
    // Captured live: reading a deleted screen answers RC=2 (NOT_FOUND) with an
    // empty header, which must never be read as "a screen with no fields".
    auto result = ParseScreenReadOutput("RC=2\nHEADER=||000|000\n");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

TEST_CASE("ParseScreenDeleteOutput reports success on RC=0", "[screen]") {
    auto result = ParseScreenDeleteOutput("RC=0\n");
    REQUIRE(result.IsOk());
}

TEST_CASE("ParseScreenDeleteOutput surfaces a non-zero RC as an error", "[screen]") {
    auto result = ParseScreenDeleteOutput("RC=4\nMSG=37/E/403:VALUE||||\n");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::ActivationError);
}
