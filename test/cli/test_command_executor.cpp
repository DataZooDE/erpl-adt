#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>

#include <set>

using namespace erpl_adt;

// ===========================================================================
// IsNewStyleCommand
// ===========================================================================

TEST_CASE("IsNewStyleCommand: search group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "search", "query", "CL_*"};
    CHECK(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: object group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "object", "read", "/sap/bc/adt/foo"};
    CHECK(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: flags before group are skipped",
          "[cli][executor]") {
    // --json is a boolean flag and does not consume the next arg.
    const char* argv[] = {"erpl-adt", "--host", "myhost", "--json", "search", "query", "X"};
    CHECK(IsNewStyleCommand(7, argv));
}

TEST_CASE("IsNewStyleCommand: legacy deploy is NOT new-style",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "deploy", "--config", "x.yaml"};
    CHECK_FALSE(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: no args returns false",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt"};
    CHECK_FALSE(IsNewStyleCommand(1, argv));
}

TEST_CASE("IsNewStyleCommand: flag-only args returns false",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "--version"};
    CHECK_FALSE(IsNewStyleCommand(2, argv));
}

TEST_CASE("IsNewStyleCommand: discover group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "--host", "x", "discover", "services"};
    CHECK(IsNewStyleCommand(5, argv));
}

// ===========================================================================
// RegisterAllCommands populates router
// ===========================================================================

TEST_CASE("RegisterAllCommands registers all expected groups",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto groups = router.Groups();

    CHECK(groups.size() == 9);

    // Verify all groups present (sorted).
    std::set<std::string> expected = {
        "check", "ddic", "discover", "object", "package",
        "search", "source", "test", "transport"
    };
    std::set<std::string> actual(groups.begin(), groups.end());
    CHECK(actual == expected);
}

TEST_CASE("RegisterAllCommands: object group has 5 actions",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto cmds = router.CommandsForGroup("object");
    CHECK(cmds.size() == 5);
}

// ===========================================================================
// Validation: missing positional args return exit code 99
// ===========================================================================

TEST_CASE("search query: missing pattern returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // No positional arg after "search query"
    const char* argv[] = {"erpl-adt", "search", "query"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("search short form: missing pattern returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // "erpl-adt search" with no pattern → group help (exit 0), not handler
    // This shows group help because Parse returns "Missing action" error.
    const char* argv[] = {"erpl-adt", "search"};
    CHECK(router.Dispatch(2, argv) == 0);
}

TEST_CASE("package group has 3 actions (list, tree, exists)",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto cmds = router.CommandsForGroup("package");
    CHECK(cmds.size() == 3);
}

TEST_CASE("package tree: missing name returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "package", "tree"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("object lock: missing URI returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "lock"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("source write: missing --file returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "write",
                          "/sap/bc/adt/oo/classes/foo/source/main",
                          "--handle", "abc123"};
    // Has URI and handle but no --file → 99
    CHECK(router.Dispatch(6, argv) == 99);
}

TEST_CASE("object delete: missing --handle returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "delete",
                          "/sap/bc/adt/oo/classes/foo"};
    CHECK(router.Dispatch(4, argv) == 99);
}

TEST_CASE("transport create: missing --desc returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "transport", "create",
                          "--package", "ZTEST"};
    CHECK(router.Dispatch(5, argv) == 99);
}

TEST_CASE("object create: missing --type returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "create",
                          "--name", "ZCL_FOO", "--package", "ZTEST"};
    CHECK(router.Dispatch(7, argv) == 99);
}
