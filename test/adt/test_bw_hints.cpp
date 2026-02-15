#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_hints.hpp>

using namespace erpl_adt;

// ===========================================================================
// 406 on BW endpoint → content type version mismatch hint
// ===========================================================================

TEST_CASE("AddBwHint: 406 on BW endpoint adds content type hint", "[bw_hints]") {
    auto error = Error::FromHttpStatus(
        "BwReadObject", "/sap/bw/modeling/iobj/0CALMONTH/a", 406,
        "Content type version mismatch");
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("Content type") != std::string::npos);
    CHECK(error.hint->find("bw discover") != std::string::npos);
}

TEST_CASE("AddBwHint: 406 on non-BW endpoint adds no hint", "[bw_hints]") {
    auto error = Error::FromHttpStatus(
        "Search", "/sap/bc/adt/repository/informationsystem/search", 406);
    AddBwHint(error);
    CHECK_FALSE(error.hint.has_value());
}

// ===========================================================================
// 404 on BW endpoint → SICF hint
// ===========================================================================

TEST_CASE("AddBwHint: 404 on BW endpoint adds SICF hint", "[bw_hints]") {
    auto error = Error::FromHttpStatus(
        "BwDiscover", "/sap/bw/modeling/discovery", 404);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("SICF") != std::string::npos);
    CHECK(error.hint->find("/sap/bw/modeling/") != std::string::npos);
}

TEST_CASE("AddBwHint: 404 on BW search endpoint adds SICF hint", "[bw_hints]") {
    auto error = Error::FromHttpStatus(
        "BwSearchObjects", "/sap/bw/modeling/repo/is/bwsearch?searchTerm=*", 404);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("SICF") != std::string::npos);
}

// ===========================================================================
// 404 on non-BW endpoint → no hint
// ===========================================================================

TEST_CASE("AddBwHint: 404 on non-BW endpoint adds no hint", "[bw_hints]") {
    auto error = Error::FromHttpStatus(
        "Search", "/sap/bc/adt/repository/informationsystem/search", 404);
    AddBwHint(error);
    CHECK_FALSE(error.hint.has_value());
}

// ===========================================================================
// 500 + "not activated" on bwsearch → RSOSM/Search hint
// ===========================================================================

TEST_CASE("AddBwHint: 500 not activated on bwsearch adds Search hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>BW Search is not activated</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwSearchObjects", "/sap/bw/modeling/repo/is/bwsearch?searchTerm=*", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("BW Search") != std::string::npos);
    CHECK(error.hint->find("RSOSM") != std::string::npos);
}

// ===========================================================================
// 500 + "not activated" on CTO → RSOSM/CTO hint
// ===========================================================================

TEST_CASE("AddBwHint: 500 not activated on CTO adds CTO hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>CTO service is not activated</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwTransportCheck", "/sap/bw/modeling/cto?rddetails=all", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("CTO") != std::string::npos);
    CHECK(error.hint->find("RSOSM") != std::string::npos);
}

TEST_CASE("AddBwHint: 500 not activated on /cto/ path adds CTO hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>Feature not activated</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwTransportWrite", "/sap/bw/modeling/cto/write", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("CTO") != std::string::npos);
    CHECK(error.hint->find("RSOSM") != std::string::npos);
}

// ===========================================================================
// 500 + "not activated" on other BW endpoint → generic RSOSM hint
// ===========================================================================

TEST_CASE("AddBwHint: 500 not activated on other BW endpoint adds generic RSOSM hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>Service not activated</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwActivateObjects", "/sap/bw/modeling/activation?mode=activate", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("RSOSM") != std::string::npos);
}

// ===========================================================================
// 500 + "not implemented" also triggers hint
// ===========================================================================

TEST_CASE("AddBwHint: 500 not implemented on bwsearch adds Search hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>BW Search is NOT IMPLEMENTED</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwSearchObjects", "/sap/bw/modeling/repo/is/bwsearch?searchTerm=*", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("BW Search") != std::string::npos);
    CHECK(error.hint->find("RSOSM") != std::string::npos);
}

// ===========================================================================
// 500 without "not activated" → no hint
// ===========================================================================

TEST_CASE("AddBwHint: 500 without activation message adds no hint", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>Internal processing error</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwSearchObjects", "/sap/bw/modeling/repo/is/bwsearch?searchTerm=*", 500, body);
    AddBwHint(error);
    CHECK_FALSE(error.hint.has_value());
}

// ===========================================================================
// Non-HTTP error → no hint
// ===========================================================================

TEST_CASE("AddBwHint: non-HTTP error on BW endpoint adds no hint", "[bw_hints]") {
    Error error;
    error.operation = "BwSearchObjects";
    error.endpoint = "/sap/bw/modeling/repo/is/bwsearch";
    error.message = "Connection refused";
    error.category = ErrorCategory::Connection;
    AddBwHint(error);
    CHECK_FALSE(error.hint.has_value());
}

// ===========================================================================
// Case insensitivity
// ===========================================================================

TEST_CASE("AddBwHint: case-insensitive matching on error text", "[bw_hints]") {
    std::string body = R"(<exc:exception><exc:message>BW SEARCH IS NOT ACTIVATED</exc:message></exc:exception>)";
    auto error = Error::FromHttpStatus(
        "BwSearchObjects", "/sap/bw/modeling/repo/is/bwsearch?searchTerm=*", 500, body);
    AddBwHint(error);
    REQUIRE(error.hint.has_value());
    CHECK(error.hint->find("BW Search") != std::string::npos);
}
