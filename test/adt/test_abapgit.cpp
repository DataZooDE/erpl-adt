#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit.hpp>
#include "../../test/mocks/mock_adt_session.hpp"
#include "../../test/mocks/mock_xml_codec.hpp"

#include <chrono>
#include <optional>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

RepoUrl MakeUrl(const char* url) {
    return RepoUrl::Create(url).Value();
}

BranchRef MakeBranch(const char* ref) {
    return BranchRef::Create(ref).Value();
}

PackageName MakePackage(const char* name) {
    return PackageName::Create(name).Value();
}

RepoKey MakeKey(const char* key) {
    return RepoKey::Create(key).Value();
}

std::vector<RepoInfo> SampleRepos() {
    return {
        {"KEY1", "https://github.com/org/repo1.git", "refs/heads/main", "ZREPO1",
         RepoStatusEnum::Active, "Linked"},
        {"KEY2", "https://github.com/org/repo2.git", "refs/heads/main", "ZREPO2",
         RepoStatusEnum::Inactive, "Cloned"},
    };
}

} // namespace

// ===========================================================================
// ListRepos
// ===========================================================================

TEST_CASE("ListRepos: returns parsed repo list on 200", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {200, {}, "<repos-xml/>"}));
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(SampleRepos()));

    auto result = ListRepos(session, codec);

    REQUIRE(result.IsOk());
    CHECK(result.Value().size() == 2);
    CHECK(result.Value()[0].key == "KEY1");
    CHECK(result.Value()[1].url == "https://github.com/org/repo2.git");

    REQUIRE(session.GetCallCount() == 1);
    CHECK(session.GetCalls()[0].path == "/sap/bc/adt/abapgit/repos");
}

TEST_CASE("ListRepos: propagates HTTP error", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/abapgit/repos", std::nullopt,
              "connection refused", std::nullopt}));

    auto result = ListRepos(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "connection refused");
}

TEST_CASE("ListRepos: returns error on non-200", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok(
        {500, {}, "Error"}));

    auto result = ListRepos(session, codec);

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}

// ===========================================================================
// FindRepo
// ===========================================================================

TEST_CASE("FindRepo: finds matching repo by URL", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<xml/>"}));
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(SampleRepos()));

    auto result = FindRepo(session, codec, MakeUrl("https://github.com/org/repo2.git"));

    REQUIRE(result.IsOk());
    REQUIRE(result.Value().has_value());
    CHECK(result.Value()->key == "KEY2");
}

TEST_CASE("FindRepo: returns nullopt when not found", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<xml/>"}));
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(SampleRepos()));

    auto result = FindRepo(session, codec, MakeUrl("https://github.com/org/nonexistent.git"));

    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().has_value());
}

TEST_CASE("FindRepo: returns empty optional on empty repo list", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<xml/>"}));
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(std::vector<RepoInfo>{}));

    auto result = FindRepo(session, codec, MakeUrl("https://github.com/org/repo1.git"));

    REQUIRE(result.IsOk());
    CHECK_FALSE(result.Value().has_value());
}

TEST_CASE("FindRepo: propagates list error", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "", std::nullopt, "network error", std::nullopt}));

    auto result = FindRepo(session, codec, MakeUrl("https://github.com/org/repo1.git"));

    REQUIRE(result.IsErr());
}

// ===========================================================================
// CloneRepo
// ===========================================================================

TEST_CASE("CloneRepo: handles sync 200 response", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-tok")));
    codec.SetBuildRepoCloneXmlResponse(
        Result<std::string, Error>::Ok(std::string("<clone-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<repos-response/>"}));

    std::vector<RepoInfo> repos = {
        {"KEY3", "https://github.com/org/new-repo.git", "refs/heads/main", "ZNEW",
         RepoStatusEnum::Active, "Cloned"},
    };
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(repos));

    auto result = CloneRepo(session, codec,
        MakeUrl("https://github.com/org/new-repo.git"),
        MakeBranch("refs/heads/main"),
        MakePackage("ZNEW"));

    REQUIRE(result.IsOk());
    CHECK(result.Value().key == "KEY3");

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path == "/sap/bc/adt/abapgit/repos");
    CHECK(session.PostCalls()[0].headers.at("x-csrf-token") == "csrf-tok");
}

TEST_CASE("CloneRepo: handles async 202 response with poll", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-tok")));
    codec.SetBuildRepoCloneXmlResponse(
        Result<std::string, Error>::Ok(std::string("<clone-xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/clone/123"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<repos-xml/>", std::chrono::milliseconds{5000}}));

    std::vector<RepoInfo> repos = {
        {"KEY4", "https://github.com/org/async-repo.git", "refs/heads/main", "ZASYNC",
         RepoStatusEnum::Active, "Cloned"},
    };
    codec.SetParseRepoListResponse(
        Result<std::vector<RepoInfo>, Error>::Ok(repos));

    auto result = CloneRepo(session, codec,
        MakeUrl("https://github.com/org/async-repo.git"),
        MakeBranch("refs/heads/main"),
        MakePackage("ZASYNC"));

    REQUIRE(result.IsOk());
    CHECK(result.Value().key == "KEY4");

    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/clone/123");
}

TEST_CASE("CloneRepo: returns error when async poll fails", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    codec.SetBuildRepoCloneXmlResponse(
        Result<std::string, Error>::Ok(std::string("<xml/>")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/123"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Failed, "", std::chrono::milliseconds{2000}}));

    auto result = CloneRepo(session, codec,
        MakeUrl("https://github.com/org/fail.git"),
        MakeBranch("refs/heads/main"),
        MakePackage("ZFAIL"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "async clone operation failed");
}

TEST_CASE("CloneRepo: propagates CSRF error", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    auto result = CloneRepo(session, codec,
        MakeUrl("https://github.com/org/repo.git"),
        MakeBranch("refs/heads/main"),
        MakePackage("ZTEST"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}

// ===========================================================================
// PullRepo
// ===========================================================================

TEST_CASE("PullRepo: handles async 202 with poll", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {{"Location", "/poll/pull/456"}}, ""}));
    session.EnqueuePoll(Result<PollResult, Error>::Ok(
        PollResult{PollStatus::Completed, "<result/>", std::chrono::milliseconds{3000}}));

    auto result = PullRepo(session, codec, MakeKey("KEY1"));

    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Completed);
    CHECK(result.Value().elapsed.count() == 3000);

    REQUIRE(session.PostCallCount() == 1);
    CHECK(session.PostCalls()[0].path == "/sap/bc/adt/abapgit/repos/KEY1/pull");
    REQUIRE(session.PollCallCount() == 1);
    CHECK(session.PollCalls()[0].location_url == "/poll/pull/456");
}

TEST_CASE("PullRepo: handles sync 200 (no changes)", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<no-changes/>"}));

    auto result = PullRepo(session, codec, MakeKey("KEY1"));

    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Completed);
    CHECK(result.Value().body == "<no-changes/>");
}

TEST_CASE("PullRepo: returns error on missing Location header", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {202, {}, ""}));

    auto result = PullRepo(session, codec, MakeKey("KEY1"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "202 response missing Location header");
}

TEST_CASE("PullRepo: returns error on unexpected status", "[adt][abapgit]") {
    MockAdtSession session;
    MockXmlCodec codec;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {500, {}, "Error"}));

    auto result = PullRepo(session, codec, MakeKey("KEY1"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 500);
}

// ===========================================================================
// UnlinkRepo
// ===========================================================================

TEST_CASE("UnlinkRepo: succeeds with 204", "[adt][abapgit]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueueDelete(Result<HttpResponse, Error>::Ok(
        {204, {}, ""}));

    auto result = UnlinkRepo(session, MakeKey("KEY1"));

    REQUIRE(result.IsOk());
    REQUIRE(session.DeleteCallCount() == 1);
    CHECK(session.DeleteCalls()[0].path == "/sap/bc/adt/abapgit/repos/KEY1");
    CHECK(session.DeleteCalls()[0].headers.at("x-csrf-token") == "tok");
}

TEST_CASE("UnlinkRepo: succeeds with 200", "[adt][abapgit]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueueDelete(Result<HttpResponse, Error>::Ok(
        {200, {}, ""}));

    auto result = UnlinkRepo(session, MakeKey("KEY1"));

    REQUIRE(result.IsOk());
}

TEST_CASE("UnlinkRepo: returns error on unexpected status", "[adt][abapgit]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("tok")));
    session.EnqueueDelete(Result<HttpResponse, Error>::Ok(
        {404, {}, "Not Found"}));

    auto result = UnlinkRepo(session, MakeKey("KEY1"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().http_status.value() == 404);
}

TEST_CASE("UnlinkRepo: propagates CSRF error", "[adt][abapgit]") {
    MockAdtSession session;

    session.EnqueueCsrfToken(Result<std::string, Error>::Err(
        Error{"FetchCsrfToken", "", std::nullopt, "csrf failed", std::nullopt}));

    auto result = UnlinkRepo(session, MakeKey("KEY1"));

    REQUIRE(result.IsErr());
    CHECK(result.Error().message == "csrf failed");
}
