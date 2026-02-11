#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/discovery.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// Discover
// ===========================================================================

TEST_CASE("Discover: returns parsed discovery result on 200", "[adt][discovery]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {{"content-type", "application/xml"}}, "<discovery-xml/>"}));

    DiscoveryResult expected;
    expected.has_abapgit_support = true;
    expected.has_packages_support = true;
    expected.has_activation_support = true;
    expected.services = {{"abapgit", "/sap/bc/adt/abapgit/repos", "application/xml"}};
    codec.SetParseDiscoveryResponse(
        Result<DiscoveryResult, Error>::Ok(expected));

    auto result = Discover(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().has_abapgit_support);
    CHECK(result.Value().has_packages_support);
    CHECK(result.Value().has_activation_support);
    CHECK(result.Value().services.size() == 1);
    CHECK(result.Value().services[0].title == "abapgit");

    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/discovery");

    CHECK(codec.CallCount("ParseDiscoveryResponse") == 1);
}

TEST_CASE("Discover: propagates HTTP error", "[adt][discovery]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/discovery", std::nullopt,
              "connection refused", std::nullopt}));

    auto result = Discover(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "connection refused");
}

TEST_CASE("Discover: returns error on non-200 status", "[adt][discovery]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {401, {}, "Unauthorized"}));

    auto result = Discover(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 401);
    CHECK(result.Error().operation == "Discover");
}

TEST_CASE("Discover: propagates XML parse error", "[adt][discovery]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "not-xml"}));
    codec.SetParseDiscoveryResponse(
        Result<DiscoveryResult, Error>::Err(
            Error{"ParseDiscoveryResponse", "", std::nullopt,
                  "malformed XML", std::nullopt}));

    auto result = Discover(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "malformed XML");
}

// ===========================================================================
// HasAbapGitSupport
// ===========================================================================

TEST_CASE("HasAbapGitSupport: returns true when supported", "[adt][discovery]") {
    DiscoveryResult dr;
    dr.has_abapgit_support = true;
    CHECK(HasAbapGitSupport(dr));
}

TEST_CASE("HasAbapGitSupport: returns false when not supported", "[adt][discovery]") {
    DiscoveryResult dr;
    dr.has_abapgit_support = false;
    CHECK_FALSE(HasAbapGitSupport(dr));
}
