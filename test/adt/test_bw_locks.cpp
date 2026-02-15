#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_locks.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// BwListLocks
// ===========================================================================

TEST_CASE("BwListLocks: parses lock entries", "[adt][bw][locks]") {
    MockAdtSession mock;
    std::string xml = R"(
        <bwLocks:dataContainer xmlns:bwLocks="http://sap.com/bw/locks">
            <bwLocks:lock client="001" user="DEVELOPER" mode="E"
                tableName="RSBWOBJ_ENQUEUE" tableDesc="BW Object Lock"
                object="ZADSO_TEST" arg="QkFTRQ==" owner1="T1dORVIx"
                owner2="T1dORVIy" timestamp="20260214120000"
                updCount="0" diaCount="1"/>
            <bwLocks:lock client="001" user="ADMIN" mode="E"
                tableName="RSBWOBJ_ENQUEUE" tableDesc="BW Object Lock"
                object="ZIOBJ_TEST" arg="QVJH" owner1="T1cxMQ=="
                owner2="T1cxMg==" timestamp="20260214130000"
                updCount="1" diaCount="2"/>
        </bwLocks:dataContainer>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsOk());

    const auto& locks = result.Value();
    REQUIRE(locks.size() == 2);

    CHECK(locks[0].client == "001");
    CHECK(locks[0].user == "DEVELOPER");
    CHECK(locks[0].mode == "E");
    CHECK(locks[0].table_name == "RSBWOBJ_ENQUEUE");
    CHECK(locks[0].table_desc == "BW Object Lock");
    CHECK(locks[0].object == "ZADSO_TEST");
    CHECK(locks[0].arg == "QkFTRQ==");
    CHECK(locks[0].owner1 == "T1dORVIx");
    CHECK(locks[0].owner2 == "T1dORVIy");
    CHECK(locks[0].timestamp == "20260214120000");
    CHECK(locks[0].upd_count == 0);
    CHECK(locks[0].dia_count == 1);

    CHECK(locks[1].user == "ADMIN");
    CHECK(locks[1].object == "ZIOBJ_TEST");
    CHECK(locks[1].upd_count == 1);
    CHECK(locks[1].dia_count == 2);
}

TEST_CASE("BwListLocks: parses unnamespaced lock elements", "[adt][bw][locks]") {
    MockAdtSession mock;
    std::string xml = R"(
        <dataContainer>
            <lock client="001" user="DEVELOPER" mode="E"
                  tableName="RSBWOBJ_ENQUEUE" object="ZADSO_TEST"
                  arg="QkFTRQ==" owner1="T1cxMQ==" owner2="T1cxMg=="/>
        </dataContainer>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsOk());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].user == "DEVELOPER");
    CHECK(result.Value()[0].object == "ZADSO_TEST");
}

TEST_CASE("BwListLocks: sends correct URL with defaults", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<locks/>"}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/utils/locks?resultsize=100");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/xml");
}

TEST_CASE("BwListLocks: user filter appended to URL", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<locks/>"}));

    BwListLocksOptions options;
    options.user = "DEVELOPER";
    auto result = BwListLocks(mock, options);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("user=DEVELOPER") != std::string::npos);
}

TEST_CASE("BwListLocks: search filter appended to URL", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<locks/>"}));

    BwListLocksOptions options;
    options.search = "ZADSO*";
    auto result = BwListLocks(mock, options);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path.find("search=ZADSO") != std::string::npos);
}

TEST_CASE("BwListLocks: user and search filters combined", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<locks/>"}));

    BwListLocksOptions options;
    options.user = "ADMIN";
    options.search = "Z*";
    options.max_results = 50;
    auto result = BwListLocks(mock, options);
    REQUIRE(result.IsOk());

    const auto& path = mock.GetCalls()[0].path;
    CHECK(path.find("resultsize=50") != std::string::npos);
    CHECK(path.find("user=ADMIN") != std::string::npos);
    CHECK(path.find("search=Z") != std::string::npos);
}

TEST_CASE("BwListLocks: empty response returns empty vector", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<locks/>"}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwListLocks: HTTP error propagated", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsErr());
}

TEST_CASE("BwListLocks: connection error propagated", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/utils/locks",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsErr());
}

TEST_CASE("BwListLocks: malformed XML returns error", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "not xml"}));

    auto result = BwListLocks(mock, BwListLocksOptions{});
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("parse") != std::string::npos);
}

// ===========================================================================
// BwDeleteLock
// ===========================================================================

TEST_CASE("BwDeleteLock: sends DELETE with correct headers", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    BwDeleteLockOptions options;
    options.user = "DEVELOPER";
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    options.scope = "1";
    options.lock_mode = "E";
    options.owner1 = "T1dORVIx";
    options.owner2 = "T1dORVIy";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsOk());

    REQUIRE(mock.DeleteCallCount() == 1);
    const auto& call = mock.DeleteCalls()[0];
    CHECK(call.path.find("user=DEVELOPER") != std::string::npos);
    CHECK(call.headers.at("BW_OBJNAME") == "RSBWOBJ_ENQUEUE");
    CHECK(call.headers.at("BW_ARGUMENT") == "QkFTRQ==");
    CHECK(call.headers.at("BW_SCOPE") == "1");
    CHECK(call.headers.at("BW_TYPE") == "E");
    CHECK(call.headers.at("BW_OWNER1") == "T1dORVIx");
    CHECK(call.headers.at("BW_OWNER2") == "T1dORVIy");
}

TEST_CASE("BwDeleteLock: accepts 200 response", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, "OK"}));

    BwDeleteLockOptions options;
    options.user = "DEVELOPER";
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    options.lock_mode = "E";
    options.owner1 = "T1c=";
    options.owner2 = "T1c=";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsOk());
}

TEST_CASE("BwDeleteLock: empty user returns error", "[adt][bw][locks]") {
    MockAdtSession mock;

    BwDeleteLockOptions options;
    // user is empty
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    options.lock_mode = "E";
    options.owner1 = "T1c=";
    options.owner2 = "T1c=";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("User") != std::string::npos);
    CHECK(mock.DeleteCallCount() == 0);
}

TEST_CASE("BwDeleteLock: scope defaults to 1 when empty", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({204, {}, ""}));

    BwDeleteLockOptions options;
    options.user = "DEVELOPER";
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    // scope is empty — should default to "1"
    options.lock_mode = "E";
    options.owner1 = "T1c=";
    options.owner2 = "T1c=";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsOk());

    CHECK(mock.DeleteCalls()[0].headers.at("BW_SCOPE") == "1");
}

TEST_CASE("BwDeleteLock: HTTP error propagated", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({403, {}, "Forbidden"}));

    BwDeleteLockOptions options;
    options.user = "DEVELOPER";
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    options.lock_mode = "E";
    options.owner1 = "T1c=";
    options.owner2 = "T1c=";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsErr());
}

TEST_CASE("BwDeleteLock: connection error propagated", "[adt][bw][locks]") {
    MockAdtSession mock;
    mock.EnqueueDelete(Result<HttpResponse, Error>::Err(Error{
        "Delete", "/sap/bw/modeling/utils/locks",
        std::nullopt, "Connection refused", std::nullopt}));

    BwDeleteLockOptions options;
    options.user = "DEVELOPER";
    options.table_name = "RSBWOBJ_ENQUEUE";
    options.arg = "QkFTRQ==";
    options.lock_mode = "E";
    options.owner1 = "T1c=";
    options.owner2 = "T1c=";

    auto result = BwDeleteLock(mock, options);
    REQUIRE(result.IsErr());
}
