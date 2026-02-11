#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_router.hpp>
#include <erpl_adt/cli/output_formatter.hpp>

#include <sstream>
#include <string>

using namespace erpl_adt;

// ===========================================================================
// CLI examples from docs/cli-usage.md — verify command parsing produces
// the expected group, action, positional args, and flags.
// ===========================================================================

// ---------------------------------------------------------------------------
// search group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: search query with type and max", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "search", "query", "ZCL_*",
                          "--type=CLAS", "--max", "50"};
    auto result = CommandRouter::Parse(7, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "query");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "ZCL_*");
    CHECK(result.Value().flags.at("type") == "CLAS");
    CHECK(result.Value().flags.at("max") == "50");
}

TEST_CASE("CLI example: search query with namespace", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "search", "query", "/NAMESPACE/*"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "query");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/NAMESPACE/*");
}

TEST_CASE("CLI example: search short form (no query action)", "[cli][examples]") {
    // Short form: "erpl-adt search ZCL_* --type=CLAS"
    // Parse sees group=search, action=ZCL_* — Dispatch remaps via default action.
    const char* argv[] = {"erpl-adt", "search", "ZCL_*", "--type=CLAS"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "ZCL_*");
    CHECK(result.Value().flags.at("type") == "CLAS");
}

// ---------------------------------------------------------------------------
// object group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: object read", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "object", "read",
                          "/sap/bc/adt/oo/classes/ZCL_EXAMPLE"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "object");
    CHECK(result.Value().action == "read");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/oo/classes/ZCL_EXAMPLE");
}

TEST_CASE("CLI example: object create", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "object", "create",
                          "--type=CLAS/OC", "--name=ZCL_NEW",
                          "--package=ZTEST", "--transport=NPLK900001"};
    auto result = CommandRouter::Parse(7, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "object");
    CHECK(result.Value().action == "create");
    CHECK(result.Value().flags.at("type") == "CLAS/OC");
    CHECK(result.Value().flags.at("name") == "ZCL_NEW");
    CHECK(result.Value().flags.at("package") == "ZTEST");
    CHECK(result.Value().flags.at("transport") == "NPLK900001");
}

TEST_CASE("CLI example: object delete", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "object", "delete",
                          "/sap/bc/adt/oo/classes/ZCL_OLD",
                          "--transport=NPLK900001"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "object");
    CHECK(result.Value().action == "delete");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/oo/classes/ZCL_OLD");
    CHECK(result.Value().flags.at("transport") == "NPLK900001");
}

TEST_CASE("CLI example: object lock", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "object", "lock",
                          "/sap/bc/adt/oo/classes/ZCL_EXAMPLE"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "object");
    CHECK(result.Value().action == "lock");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/oo/classes/ZCL_EXAMPLE");
}

TEST_CASE("CLI example: object unlock", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "object", "unlock",
                          "/sap/bc/adt/oo/classes/ZCL_EXAMPLE",
                          "--handle=LOCK_HANDLE"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "object");
    CHECK(result.Value().action == "unlock");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/oo/classes/ZCL_EXAMPLE");
    CHECK(result.Value().flags.at("handle") == "LOCK_HANDLE");
}

// ---------------------------------------------------------------------------
// source group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: source read", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "source");
    CHECK(result.Value().action == "read");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] ==
          "/sap/bc/adt/oo/classes/zcl_test/source/main");
}

TEST_CASE("CLI example: source read inactive version", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--version=inactive"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "source");
    CHECK(result.Value().action == "read");
    CHECK(result.Value().flags.at("version") == "inactive");
}

TEST_CASE("CLI example: source write", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "source", "write",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--file=source.abap", "--handle=LOCK_HANDLE",
                          "--transport=NPLK900001"};
    auto result = CommandRouter::Parse(7, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "source");
    CHECK(result.Value().action == "write");
    CHECK(result.Value().flags.at("file") == "source.abap");
    CHECK(result.Value().flags.at("handle") == "LOCK_HANDLE");
    CHECK(result.Value().flags.at("transport") == "NPLK900001");
}

TEST_CASE("CLI example: source check", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "source", "check",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "source");
    CHECK(result.Value().action == "check");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] ==
          "/sap/bc/adt/oo/classes/zcl_test/source/main");
}

// ---------------------------------------------------------------------------
// test group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: test run", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "test", "run",
                          "/sap/bc/adt/oo/classes/ZCL_TEST"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "test");
    CHECK(result.Value().action == "run");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/oo/classes/ZCL_TEST");
}

TEST_CASE("CLI example: test run package", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "test", "run",
                          "/sap/bc/adt/packages/ZTEST_PKG"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "test");
    CHECK(result.Value().action == "run");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/packages/ZTEST_PKG");
}

TEST_CASE("CLI example: test run with JSON output", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "--json=true", "test", "run",
                          "/sap/bc/adt/oo/classes/ZCL_TEST"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "test");
    CHECK(result.Value().action == "run");
    CHECK(result.Value().flags.at("json") == "true");
}

// ---------------------------------------------------------------------------
// check group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: check run", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "check", "run",
                          "/sap/bc/adt/packages/ZTEST_PKG"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "check");
    CHECK(result.Value().action == "run");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "/sap/bc/adt/packages/ZTEST_PKG");
}

TEST_CASE("CLI example: check run with variant", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "check", "run",
                          "/sap/bc/adt/oo/classes/ZCL_TEST",
                          "--variant=FUNCTIONAL_DB_ADDITION"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "check");
    CHECK(result.Value().action == "run");
    CHECK(result.Value().flags.at("variant") == "FUNCTIONAL_DB_ADDITION");
}

// ---------------------------------------------------------------------------
// transport group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: transport list", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "transport", "list",
                          "--user=DEVELOPER"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "transport");
    CHECK(result.Value().action == "list");
    CHECK(result.Value().flags.at("user") == "DEVELOPER");
}

TEST_CASE("CLI example: transport create", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "transport", "create",
                          "--desc=Feature X implementation",
                          "--package=ZTEST_PKG"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "transport");
    CHECK(result.Value().action == "create");
    CHECK(result.Value().flags.at("desc") == "Feature X implementation");
    CHECK(result.Value().flags.at("package") == "ZTEST_PKG");
}

TEST_CASE("CLI example: transport release", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "transport", "release", "NPLK900001"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "transport");
    CHECK(result.Value().action == "release");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "NPLK900001");
}

// ---------------------------------------------------------------------------
// package group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: package list", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "package", "list", "ZTEST_PKG"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "package");
    CHECK(result.Value().action == "list");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "ZTEST_PKG");
}

TEST_CASE("CLI example: package exists", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "package", "exists", "ZTEST_PKG"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "package");
    CHECK(result.Value().action == "exists");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "ZTEST_PKG");
}

// ---------------------------------------------------------------------------
// ddic group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: ddic table", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "ddic", "table", "SFLIGHT"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "ddic");
    CHECK(result.Value().action == "table");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "SFLIGHT");
}

TEST_CASE("CLI example: ddic cds", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "ddic", "cds", "ZCDS_VIEW"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "ddic");
    CHECK(result.Value().action == "cds");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "ZCDS_VIEW");
}

// ---------------------------------------------------------------------------
// git group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: git list", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "git", "list"};
    auto result = CommandRouter::Parse(3, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "git");
    CHECK(result.Value().action == "list");
}

TEST_CASE("CLI example: git clone", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "git", "clone",
                          "--url=https://github.com/org/repo.git",
                          "--branch=refs/heads/main",
                          "--package=ZTEST_PKG"};
    auto result = CommandRouter::Parse(6, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "git");
    CHECK(result.Value().action == "clone");
    CHECK(result.Value().flags.at("url") == "https://github.com/org/repo.git");
    CHECK(result.Value().flags.at("branch") == "refs/heads/main");
    CHECK(result.Value().flags.at("package") == "ZTEST_PKG");
}

TEST_CASE("CLI example: git pull", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "git", "pull", "REPO_KEY"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "git");
    CHECK(result.Value().action == "pull");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "REPO_KEY");
}

// ---------------------------------------------------------------------------
// deploy group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: deploy with config", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "deploy", "run", "--config=deploy.yaml"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "deploy");
    CHECK(result.Value().action == "run");
    CHECK(result.Value().flags.at("config") == "deploy.yaml");
}

// ---------------------------------------------------------------------------
// discover group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: discover", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "discover", "services"};
    auto result = CommandRouter::Parse(3, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "discover");
    CHECK(result.Value().action == "services");
}

// ---------------------------------------------------------------------------
// mcp group
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: mcp start", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "--host", "sap.example.com",
                          "--user", "ADMIN", "--password", "secret",
                          "mcp", "start"};
    auto result = CommandRouter::Parse(9, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "mcp");
    CHECK(result.Value().action == "start");
    CHECK(result.Value().flags.at("host") == "sap.example.com");
    CHECK(result.Value().flags.at("user") == "ADMIN");
    CHECK(result.Value().flags.at("password") == "secret");
}

// ---------------------------------------------------------------------------
// Global flags combinations
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: global flags with command", "[cli][examples]") {
    const char* argv[] = {"erpl-adt",
                          "--host", "sap.example.com",
                          "--port", "8443",
                          "--user", "DEV",
                          "--client", "001",
                          "--json=true", "--insecure=true",
                          "search", "query", "ZCL_*"};
    auto result = CommandRouter::Parse(14, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "query");
    CHECK(result.Value().flags.at("host") == "sap.example.com");
    CHECK(result.Value().flags.at("port") == "8443");
    CHECK(result.Value().flags.at("user") == "DEV");
    CHECK(result.Value().flags.at("client") == "001");
    CHECK(result.Value().flags.at("insecure") == "true");
    CHECK(result.Value().flags.at("json") == "true");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] == "ZCL_*");
}

// ---------------------------------------------------------------------------
// OutputFormatter integration — verify JSON mode flag from parsed args
// ---------------------------------------------------------------------------

TEST_CASE("CLI example: OutputFormatter uses json flag from parse", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "--json=true", "test", "run",
                          "/sap/bc/adt/oo/classes/ZCL_TEST"};
    auto result = CommandRouter::Parse(5, argv);
    REQUIRE(result.IsOk());

    bool json_mode = result.Value().flags.count("json") > 0;
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(json_mode, false, out, err);

    CHECK(fmt.IsJsonMode());
    fmt.PrintSuccess("Tests passed");
    CHECK(out.str().find("\"success\":true") != std::string::npos);
}

TEST_CASE("CLI example: OutputFormatter human mode when no json flag", "[cli][examples]") {
    const char* argv[] = {"erpl-adt", "test", "run",
                          "/sap/bc/adt/oo/classes/ZCL_TEST"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());

    bool json_mode = result.Value().flags.count("json") > 0;
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(json_mode, false, out, err);

    CHECK_FALSE(fmt.IsJsonMode());
    fmt.PrintSuccess("Tests passed");
    CHECK(out.str() == "Tests passed\n");
}
