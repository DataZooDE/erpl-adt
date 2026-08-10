#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/packages.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

PackageName MakePackage(const char* name) {
    return PackageName::Create(name).Value();
}

// Minimal shape of a repository information system quickSearch response.
const char* kEmptySearchXml =
    R"(<?xml version="1.0" encoding="utf-8"?>)"
    R"(<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core"/>)";

std::string SearchXmlFor(const std::string& package_name) {
    return
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">)"
        R"(<adtcore:objectReference adtcore:uri="/sap/bc/adt/packages/)" +
        package_name + R"(" adtcore:type="DEVC/K" adtcore:name=")" +
        package_name + R"("/>)"
        R"(</adtcore:objectReferences>)";
}

} // namespace

// ===========================================================================
// PackageExists
// ===========================================================================

TEST_CASE("PackageExists: returns true on 200", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<package-xml/>"}));

    auto result = PackageExists(session, codec, MakePackage("ZTEST"));

    REQUIRE(result.IsOk());
    CHECK(result.Value() == true);
    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/packages/ZTEST");
}

TEST_CASE("PackageExists: returns false on 404 with no search hit", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {404, {}, "Not Found"}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, kEmptySearchXml}));

    auto result = PackageExists(session, codec, MakePackage("ZNOTFOUND"));

    REQUIRE(result.IsOk());
    CHECK(result.Value() == false);
}

// ---------------------------------------------------------------------------
// GitHub issue #35: SAP_BASIS 7.40 has no /sap/bc/adt/packages/{name}
// resource, so a 404 from it proves nothing about the package's existence.
// ---------------------------------------------------------------------------

TEST_CASE("PackageExists: falls back to search when the package resource 404s",
          "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {404, {}, "Not Found"}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchXmlFor("SABAPDEMOS")}));

    auto result = PackageExists(session, codec, MakePackage("SABAPDEMOS"));

    REQUIRE(result.IsOk());
    CHECK(result.Value() == true);
    REQUIRE(session.GetCallCount() == 2);
    CHECK(session.GetCalls()[1].path.find(
        "/sap/bc/adt/repository/informationsystem/search") == 0);
    CHECK(session.GetCalls()[1].path.find("objectType=DEVC%2FK") !=
          std::string::npos);
}

TEST_CASE("PackageExists: search fallback matches case-insensitively",
          "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchXmlFor("sabapdemos")}));

    auto result = PackageExists(session, codec, MakePackage("SABAPDEMOS"));

    REQUIRE(result.IsOk());
    CHECK(result.Value() == true);
}

TEST_CASE("PackageExists: search fallback requires an exact name match",
          "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    // quickSearch matches by prefix: "SMOI" must not be satisfied by "SMOI_EN".
    session.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, SearchXmlFor("SMOI_EN")}));

    auto result = PackageExists(session, codec, MakePackage("SMOI"));

    REQUIRE(result.IsOk());
    CHECK(result.Value() == false);
}

TEST_CASE("PackageExists: a failing search is an error, not 'does not exist'",
          "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {500, {}, "Internal Server Error"}));

    auto result = PackageExists(session, codec, MakePackage("ZTEST"));

    REQUIRE(result.IsErr());
}

TEST_CASE("ResolvePackageExistence: reports how existence was established",
          "[adt][packages]") {
    SECTION("package resource") {
        MockAdtSession session;
        session.EnqueueGet(Result<HttpResponse, Error>::Ok(
            {200, {}, "<package-xml/>"}));

        auto result = ResolvePackageExistence(session, MakePackage("ZTEST"));

        REQUIRE(result.IsOk());
        CHECK(result.Value().exists);
        CHECK(result.Value().resolved_via == PackageResolution::PackageResource);
        CHECK(std::string(ToString(result.Value().resolved_via)) ==
              "package_resource");
    }

    SECTION("search fallback") {
        MockAdtSession session;
        session.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, ""}));
        session.EnqueueGet(Result<HttpResponse, Error>::Ok(
            {200, {}, SearchXmlFor("ZTEST")}));

        auto result = ResolvePackageExistence(session, MakePackage("ZTEST"));

        REQUIRE(result.IsOk());
        CHECK(result.Value().exists);
        CHECK(result.Value().resolved_via == PackageResolution::Search);
        CHECK(std::string(ToString(result.Value().resolved_via)) == "search");
    }
}

TEST_CASE("PackageExists: returns error on unexpected status", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {500, {}, "Internal Server Error"}));

    auto result = PackageExists(session, codec, MakePackage("ZBAD"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
    CHECK(result.Error().operation == "PackageExists");
}

TEST_CASE("PackageExists: propagates HTTP error", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/packages/ZTEST", std::nullopt,
              "connection refused", std::nullopt}));

    auto result = PackageExists(session, codec, MakePackage("ZTEST"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "connection refused");
}

// ===========================================================================
// CreatePackage
// ===========================================================================

TEST_CASE("CreatePackage: succeeds with 201", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("token-123")));
    codec.SetBuildPackageCreateXmlResponse(
        Result<std::string, Error>::Ok(std::string("<create-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {201, {}, "<package-response/>"}));

    PackageInfo expected{"ZTEST", "Test package", "LOCAL", "/sap/bc/adt/packages/ZTEST", ""};
    codec.SetParsePackageResponse(
        Result<PackageInfo, Error>::Ok(expected));

    auto result = CreatePackage(session, codec, MakePackage("ZTEST"), "Test package", "LOCAL", "ALICE");

    REQUIRE(result.IsOk());
    CHECK(result.Value().name == "ZTEST");
    CHECK(result.Value().description == "Test package");

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path == "/sap/bc/adt/packages");
    CHECK(session.PostCalls()[0].body == "<create-xml/>");
    CHECK(session.PostCalls()[0].headers.at("x-csrf-token") == "token-123");
    CHECK(session.CsrfCallCount() == 1);

    // Verify the responsible user was forwarded to the codec.
    REQUIRE(codec.Calls().size() >= 1);
    const auto& call = codec.Calls()[0];
    REQUIRE(call.method == "BuildPackageCreateXml");
    REQUIRE(call.args.size() == 4);
    CHECK(call.args[3] == "ALICE");
}

TEST_CASE("CreatePackage: propagates CSRF error", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    auto result = CreatePackage(session, codec, MakePackage("ZTEST"), "desc", "LOCAL", "ALICE");

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}

TEST_CASE("CreatePackage: propagates XML build error", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildPackageCreateXmlResponse(
        Result<std::string, Error>::Err(
            Error{"BuildPackageCreateXml", "", std::nullopt,
                  "xml build failed", std::nullopt}));

    auto result = CreatePackage(session, codec, MakePackage("ZTEST"), "desc", "LOCAL", "ALICE");

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "xml build failed");
}

TEST_CASE("CreatePackage: returns error on unexpected status", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildPackageCreateXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {409, {}, "Conflict"}));

    auto result = CreatePackage(session, codec, MakePackage("ZTEST"), "desc", "LOCAL", "ALICE");

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 409);
}

// ===========================================================================
// EnsurePackage
// ===========================================================================

TEST_CASE("EnsurePackage: skips create when package exists", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    // First GET: PackageExists check -> 200
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<existing-package/>"}));
    // Second GET: fetch package info
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<package-info/>"}));

    PackageInfo existing{"ZTEST", "Already here", "LOCAL", "/sap/bc/adt/packages/ZTEST", ""};
    codec.SetParsePackageResponse(
        Result<PackageInfo, Error>::Ok(existing));

    auto result = EnsurePackage(session, codec, MakePackage("ZTEST"), "desc", "LOCAL", "ALICE");

    REQUIRE(result.IsOk());
    CHECK(result.Value().name == "ZTEST");
    CHECK(result.Value().description == "Already here");

    // No POST calls — package was not created
    CHECK(session.PostCallCount() == 0);
    CHECK(session.CsrfCallCount() == 0);
    CHECK(session.GetCallCount() == 2);
}

TEST_CASE("EnsurePackage: creates when package does not exist", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    // PackageExists check -> 404 from the package resource, then no search hit
    // (absence has to be proven before creating).
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {404, {}, "Not Found"}));
    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, kEmptySearchXml}));
    // CreatePackage flow
    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildPackageCreateXmlResponse(
        Result<std::string, Error>::Ok(std::string("<create-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {201, {}, "<created/>"}));

    PackageInfo created{"ZNEW", "New package", "LOCAL", "/sap/bc/adt/packages/ZNEW", ""};
    codec.SetParsePackageResponse(
        Result<PackageInfo, Error>::Ok(created));

    auto result = EnsurePackage(session, codec, MakePackage("ZNEW"), "New package", "LOCAL", "ALICE");

    REQUIRE(result.IsOk());
    CHECK(result.Value().name == "ZNEW");
    CHECK(session.PostCallCount() == 1);
}

TEST_CASE("EnsurePackage: propagates exists check error", "[adt][packages]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "network error", std::nullopt}));

    auto result = EnsurePackage(session, codec, MakePackage("ZTEST"), "desc", "LOCAL", "ALICE");

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "network error");
}
