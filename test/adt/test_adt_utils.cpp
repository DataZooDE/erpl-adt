#include <catch2/catch_test_macros.hpp>

// Include the internal header directly (adt_utils is internal, not in public include/).
#include "../../src/adt/adt_utils.hpp"

#include <erpl_adt/core/result.hpp>
#include <string>
#include <vector>

using namespace erpl_adt;

// ===========================================================================
// ParseXmlWithRoot
// ===========================================================================

TEST_CASE("ParseXmlWithRoot: calls fn with root element on valid XML", "[adt][adt_utils]") {
    const std::string xml = R"(<root attr="hello"/>)";

    auto result = adt_utils::ParseXmlWithRoot<std::string>(
        xml, "TestOp", "/test",
        "Failed to parse",
        [](const tinyxml2::XMLElement* root) -> Result<std::string, Error> {
            const char* v = root->Attribute("attr");
            return Result<std::string, Error>::Ok(v ? v : "");
        });

    REQUIRE(result.IsOk());
    CHECK(result.Value() == "hello");
}

TEST_CASE("ParseXmlWithRoot: returns parse error on malformed XML", "[adt][adt_utils]") {
    const std::string xml = R"(<unclosed)";

    auto result = adt_utils::ParseXmlWithRoot<std::string>(
        xml, "TestOp", "/test",
        "Failed to parse",
        [](const tinyxml2::XMLElement*) -> Result<std::string, Error> {
            FAIL("fn should not be called on parse error");
            return Result<std::string, Error>::Ok("");
        });

    REQUIRE(result.IsErr());
    CHECK(result.Error().operation == "TestOp");
    CHECK(result.Error().endpoint == "/test");
}

TEST_CASE("ParseXmlWithRoot: returns error on empty XML (no root)", "[adt][adt_utils]") {
    const std::string xml = "";

    auto result = adt_utils::ParseXmlWithRoot<std::string>(
        xml, "TestOp", "/test",
        "Failed to parse",
        [](const tinyxml2::XMLElement*) -> Result<std::string, Error> {
            FAIL("fn should not be called when root is null");
            return Result<std::string, Error>::Ok("");
        });

    REQUIRE(result.IsErr());
}

TEST_CASE("ParseXmlWithRoot: propagates error returned by fn", "[adt][adt_utils]") {
    const std::string xml = R"(<root/>)";

    auto result = adt_utils::ParseXmlWithRoot<std::string>(
        xml, "TestOp", "/test",
        "Failed to parse",
        [](const tinyxml2::XMLElement*) -> Result<std::string, Error> {
            return Result<std::string, Error>::Err(Error{
                "TestOp", "/test", std::nullopt, "inner error", std::nullopt});
        });

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "inner error");
}

TEST_CASE("ParseXmlWithRoot: works with vector result type", "[adt][adt_utils]") {
    const std::string xml = R"(<items><item name="A"/><item name="B"/></items>)";

    auto result = adt_utils::ParseXmlWithRoot<std::vector<std::string>>(
        xml, "TestOp", "/test",
        "Failed to parse",
        [](const tinyxml2::XMLElement* root) -> Result<std::vector<std::string>, Error> {
            std::vector<std::string> names;
            for (auto* el = root->FirstChildElement("item"); el;
                 el = el->NextSiblingElement("item")) {
                const char* n = el->Attribute("name");
                if (n) names.emplace_back(n);
            }
            return Result<std::vector<std::string>, Error>::Ok(std::move(names));
        });

    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 2);
    CHECK(result.Value()[0] == "A");
    CHECK(result.Value()[1] == "B");
}
