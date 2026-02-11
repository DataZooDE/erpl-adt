#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_router.hpp>

#include <sstream>
#include <string>

using namespace erpl_adt;

// ===========================================================================
// Parse
// ===========================================================================

TEST_CASE("CommandRouter::Parse: group and action", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "search", "objects"};
    auto result = CommandRouter::Parse(3, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "objects");
    CHECK(result.Value().positional.empty());
    CHECK(result.Value().flags.empty());
}

TEST_CASE("CommandRouter::Parse: with positional args", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/ZCL_TEST/source/main"};
    auto result = CommandRouter::Parse(4, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "source");
    CHECK(result.Value().action == "read");
    REQUIRE(result.Value().positional.size() == 1);
    CHECK(result.Value().positional[0] ==
          "/sap/bc/adt/oo/classes/ZCL_TEST/source/main");
}

TEST_CASE("CommandRouter::Parse: with flags", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "search", "objects",
                          "--type=CLAS", "--max", "50"};
    auto result = CommandRouter::Parse(6, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "objects");
    CHECK(result.Value().flags.at("type") == "CLAS");
    CHECK(result.Value().flags.at("max") == "50");
}

TEST_CASE("CommandRouter::Parse: global flags before group", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "--json", "--host", "myhost.com",
                          "search", "objects"};
    auto result = CommandRouter::Parse(6, argv);
    REQUIRE(result.IsOk());
    CHECK(result.Value().group == "search");
    CHECK(result.Value().action == "objects");
    CHECK(result.Value().flags.at("json") == "true");
    CHECK(result.Value().flags.at("host") == "myhost.com");
}

TEST_CASE("CommandRouter::Parse: missing group", "[cli][router]") {
    const char* argv[] = {"erpl-adt"};
    auto result = CommandRouter::Parse(1, argv);
    REQUIRE(result.IsErr());
    CHECK(result.Error().find("Missing command group") != std::string::npos);
}

TEST_CASE("CommandRouter::Parse: missing action", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "search"};
    auto result = CommandRouter::Parse(2, argv);
    REQUIRE(result.IsErr());
    CHECK(result.Error().find("Missing action") != std::string::npos);
}

TEST_CASE("CommandRouter::Parse: only global flags, no group", "[cli][router]") {
    const char* argv[] = {"erpl-adt", "--json"};
    auto result = CommandRouter::Parse(2, argv);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// Register and Dispatch
// ===========================================================================

TEST_CASE("CommandRouter: dispatch to registered handler", "[cli][router]") {
    CommandRouter router;
    bool called = false;
    router.Register("test", "run", "Run tests", [&](const CommandArgs& args) -> int {
        called = true;
        CHECK(args.group == "test");
        CHECK(args.action == "run");
        return 0;
    });

    const char* argv[] = {"erpl-adt", "test", "run"};
    int exit_code = router.Dispatch(3, argv);

    CHECK(called);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: dispatch with args", "[cli][router]") {
    CommandRouter router;
    std::string captured_uri;
    router.Register("source", "read", "Read source", [&](const CommandArgs& args) -> int {
        if (!args.positional.empty()) {
            captured_uri = args.positional[0];
        }
        return 0;
    });

    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/ZCL_TEST"};
    router.Dispatch(4, argv);

    CHECK(captured_uri == "/sap/bc/adt/oo/classes/ZCL_TEST");
}

TEST_CASE("CommandRouter: unknown command returns 1", "[cli][router]") {
    CommandRouter router;
    router.Register("search", "objects", "Search", [](const CommandArgs&) { return 0; });

    const char* argv[] = {"erpl-adt", "unknown", "cmd"};
    int exit_code = router.Dispatch(3, argv);

    CHECK(exit_code == 1);
}

TEST_CASE("CommandRouter: handler return code propagated", "[cli][router]") {
    CommandRouter router;
    router.Register("fail", "cmd", "Fail", [](const CommandArgs&) { return 42; });

    const char* argv[] = {"erpl-adt", "fail", "cmd"};
    CHECK(router.Dispatch(3, argv) == 42);
}

// ===========================================================================
// Groups and CommandsForGroup
// ===========================================================================

TEST_CASE("CommandRouter: Groups returns sorted groups", "[cli][router]") {
    CommandRouter router;
    router.Register("search", "objects", "Search", [](const CommandArgs&) { return 0; });
    router.Register("object", "read", "Read", [](const CommandArgs&) { return 0; });
    router.Register("source", "read", "Read", [](const CommandArgs&) { return 0; });

    auto groups = router.Groups();
    REQUIRE(groups.size() == 3);
    CHECK(groups[0] == "object");
    CHECK(groups[1] == "search");
    CHECK(groups[2] == "source");
}

TEST_CASE("CommandRouter: CommandsForGroup", "[cli][router]") {
    CommandRouter router;
    router.Register("object", "read", "Read object", [](const CommandArgs&) { return 0; });
    router.Register("object", "create", "Create object", [](const CommandArgs&) { return 0; });
    router.Register("search", "objects", "Search", [](const CommandArgs&) { return 0; });

    auto cmds = router.CommandsForGroup("object");
    REQUIRE(cmds.size() == 2);
    CHECK(cmds[0].action == "create"); // sorted alphabetically
    CHECK(cmds[1].action == "read");
}

TEST_CASE("CommandRouter: CommandsForGroup unknown group", "[cli][router]") {
    CommandRouter router;
    auto cmds = router.CommandsForGroup("nonexistent");
    CHECK(cmds.empty());
}

// ===========================================================================
// HasGroup
// ===========================================================================

TEST_CASE("CommandRouter: HasGroup", "[cli][router]") {
    CommandRouter router;
    router.Register("search", "query", "Search", [](const CommandArgs&) { return 0; });

    CHECK(router.HasGroup("search"));
    CHECK_FALSE(router.HasGroup("nonexistent"));
}

// ===========================================================================
// PrintHelp (legacy)
// ===========================================================================

TEST_CASE("CommandRouter: PrintHelp", "[cli][router]") {
    CommandRouter router;
    router.Register("search", "objects", "Search for ABAP objects",
                    [](const CommandArgs&) { return 0; });
    router.Register("deploy", "run", "Deploy repos",
                    [](const CommandArgs&) { return 0; });

    std::ostringstream out;
    router.PrintHelp(out);

    auto help = out.str();
    CHECK(help.find("search") != std::string::npos);
    CHECK(help.find("deploy") != std::string::npos);
    CHECK(help.find("Search for ABAP objects") != std::string::npos);
    CHECK(help.find("Deploy repos") != std::string::npos);
}

// ===========================================================================
// PrintGroupHelp (Level 2)
// ===========================================================================

TEST_CASE("CommandRouter: PrintGroupHelp shows actions and examples", "[cli][router][help]") {
    CommandRouter router;
    router.SetGroupDescription("source", "Read, write, and check ABAP source code");
    router.SetGroupExamples("source", {
        "erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main",
        "erpl-adt source write .../source/main --file=source.abap",
    });
    router.Register("source", "read", "Read source code",
                    [](const CommandArgs&) { return 0; });
    router.Register("source", "write", "Write source code",
                    [](const CommandArgs&) { return 0; });
    router.Register("source", "check", "Check syntax",
                    [](const CommandArgs&) { return 0; });

    std::ostringstream out;
    router.PrintGroupHelp("source", out);
    auto help = out.str();

    CHECK(help.find("erpl-adt source") != std::string::npos);
    CHECK(help.find("Read, write, and check ABAP source code") != std::string::npos);
    CHECK(help.find("Actions:") != std::string::npos);
    CHECK(help.find("read") != std::string::npos);
    CHECK(help.find("write") != std::string::npos);
    CHECK(help.find("check") != std::string::npos);
    CHECK(help.find("Read source code") != std::string::npos);
    CHECK(help.find("Examples:") != std::string::npos);
    CHECK(help.find("erpl-adt source read") != std::string::npos);
    CHECK(help.find("<action> --help") != std::string::npos);
}

// ===========================================================================
// PrintCommandHelp (Level 3)
// ===========================================================================

TEST_CASE("CommandRouter: PrintCommandHelp shows usage, flags, examples", "[cli][router][help]") {
    CommandRouter router;
    CommandHelp help;
    help.usage = "erpl-adt source write <uri> --file <path> [flags]";
    help.args_description = "<uri>    Source URI";
    help.long_description = "Without --handle, auto-locks, writes, and unlocks.";
    help.flags = {
        {"file", "<path>", "Path to local source file", true},
        {"handle", "<handle>", "Lock handle (skips auto-lock)", false},
        {"transport", "<id>", "Transport request number", false},
    };
    help.examples = {
        "erpl-adt source write .../source/main --file=source.abap",
        "erpl-adt source write .../source/main --file=source.abap --handle=H",
    };
    router.Register("source", "write", "Write source code",
                    [](const CommandArgs&) { return 0; }, std::move(help));

    std::ostringstream out;
    router.PrintCommandHelp("source", "write", out);
    auto text = out.str();

    CHECK(text.find("erpl-adt source write") != std::string::npos);
    CHECK(text.find("Write source code") != std::string::npos);
    CHECK(text.find("Usage:") != std::string::npos);
    CHECK(text.find("erpl-adt source write <uri> --file <path> [flags]") != std::string::npos);
    CHECK(text.find("Arguments:") != std::string::npos);
    CHECK(text.find("<uri>") != std::string::npos);
    CHECK(text.find("Flags:") != std::string::npos);
    CHECK(text.find("--file <path>") != std::string::npos);
    CHECK(text.find("(required)") != std::string::npos);
    CHECK(text.find("--handle <handle>") != std::string::npos);
    CHECK(text.find("auto-locks") != std::string::npos);
    CHECK(text.find("Examples:") != std::string::npos);
    CHECK(text.find("--file=source.abap") != std::string::npos);
}

TEST_CASE("CommandRouter: PrintCommandHelp without help metadata", "[cli][router][help]") {
    CommandRouter router;
    router.Register("test", "run", "Run tests", [](const CommandArgs&) { return 0; });

    std::ostringstream out;
    router.PrintCommandHelp("test", "run", out);
    auto text = out.str();

    CHECK(text.find("erpl-adt test run") != std::string::npos);
    CHECK(text.find("Run tests") != std::string::npos);
    CHECK(text.find("No detailed help") != std::string::npos);
}

TEST_CASE("CommandRouter: PrintCommandHelp unknown command", "[cli][router][help]") {
    CommandRouter router;

    std::ostringstream out;
    router.PrintCommandHelp("nope", "nada", out);
    auto text = out.str();

    CHECK(text.find("Error:") != std::string::npos);
    CHECK(text.find("nope nada") != std::string::npos);
}

// ===========================================================================
// --help Dispatch Interception
// ===========================================================================

TEST_CASE("CommandRouter: group-only dispatch shows group help and returns 0", "[cli][router][help]") {
    CommandRouter router;
    router.SetGroupDescription("search", "Search for ABAP objects");
    router.Register("search", "query", "Search for ABAP objects",
                    [](const CommandArgs&) { return 0; });

    // "erpl-adt search" (missing action) → group help, exit 0
    const char* argv[] = {"erpl-adt", "search"};
    int exit_code = router.Dispatch(2, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: 'group --help' shows group help and returns 0", "[cli][router][help]") {
    CommandRouter router;
    router.SetGroupDescription("object", "Read, create, delete objects");
    router.Register("object", "read", "Read", [](const CommandArgs&) { return 0; });

    const char* argv[] = {"erpl-adt", "object", "--help"};
    int exit_code = router.Dispatch(3, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: 'group action --help' shows command help and returns 0", "[cli][router][help]") {
    CommandRouter router;
    CommandHelp help;
    help.usage = "erpl-adt source read <uri>";
    help.examples = {"erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main"};
    router.Register("source", "read", "Read source code",
                    [](const CommandArgs&) { return 42; }, std::move(help));

    // Handler should NOT be called when --help is present.
    const char* argv[] = {"erpl-adt", "source", "read", "--help"};
    int exit_code = router.Dispatch(4, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: 'group help' shows group help and returns 0", "[cli][router][help]") {
    CommandRouter router;
    router.SetGroupDescription("test", "Run ABAP Unit tests");
    router.Register("test", "run", "Run tests",
                    [](const CommandArgs&) { return 0; });

    const char* argv[] = {"erpl-adt", "test", "help"};
    int exit_code = router.Dispatch(3, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: unknown group with --help returns 1", "[cli][router][help]") {
    CommandRouter router;
    router.Register("search", "query", "Search", [](const CommandArgs&) { return 0; });

    const char* argv[] = {"erpl-adt", "nonexistent", "--help"};
    int exit_code = router.Dispatch(3, argv);
    CHECK(exit_code == 1);
}

// ===========================================================================
// Group descriptions and examples
// ===========================================================================

TEST_CASE("CommandRouter: GroupDescription and GroupExamples", "[cli][router][help]") {
    CommandRouter router;
    router.SetGroupDescription("source", "Read, write, check source code");
    router.SetGroupExamples("source", {"example1", "example2"});

    CHECK(router.GroupDescription("source") == "Read, write, check source code");
    CHECK(router.GroupExamples("source").size() == 2);
    CHECK(router.GroupDescription("nonexistent").empty());
    CHECK(router.GroupExamples("nonexistent").empty());
}

// ===========================================================================
// Register with CommandHelp
// ===========================================================================

// ===========================================================================
// Default Action
// ===========================================================================

TEST_CASE("CommandRouter: default action dispatches short form", "[cli][router][default]") {
    CommandRouter router;
    std::string captured_pattern;
    router.Register("search", "query", "Search", [&](const CommandArgs& args) -> int {
        if (!args.positional.empty()) {
            captured_pattern = args.positional[0];
        }
        return 0;
    });
    router.SetDefaultAction("search", "query");

    // "erpl-adt search ZCL_*" should dispatch to search:query with positional[0] = "ZCL_*"
    const char* argv[] = {"erpl-adt", "search", "ZCL_*"};
    int exit_code = router.Dispatch(3, argv);

    CHECK(exit_code == 0);
    CHECK(captured_pattern == "ZCL_*");
}

TEST_CASE("CommandRouter: default action backward compat with explicit action", "[cli][router][default]") {
    CommandRouter router;
    bool called = false;
    router.Register("search", "query", "Search", [&](const CommandArgs& args) -> int {
        called = true;
        CHECK(args.positional.size() == 1);
        CHECK(args.positional[0] == "ZCL_*");
        return 0;
    });
    router.SetDefaultAction("search", "query");

    // Explicit "query" action still works.
    const char* argv[] = {"erpl-adt", "search", "query", "ZCL_*"};
    int exit_code = router.Dispatch(4, argv);

    CHECK(exit_code == 0);
    CHECK(called);
}

TEST_CASE("CommandRouter: default action with flags", "[cli][router][default]") {
    CommandRouter router;
    std::string captured_type;
    router.Register("search", "query", "Search", [&](const CommandArgs& args) -> int {
        auto it = args.flags.find("type");
        if (it != args.flags.end()) captured_type = it->second;
        return 0;
    });
    router.SetDefaultAction("search", "query");

    const char* argv[] = {"erpl-adt", "search", "ZCL_*", "--type", "CLAS"};
    int exit_code = router.Dispatch(5, argv);

    CHECK(exit_code == 0);
    CHECK(captured_type == "CLAS");
}

TEST_CASE("CommandRouter: default action help still works", "[cli][router][default]") {
    CommandRouter router;
    router.SetGroupDescription("search", "Search for ABAP objects");
    router.Register("search", "query", "Search", [](const CommandArgs&) { return 42; });
    router.SetDefaultAction("search", "query");

    // "erpl-adt search --help" should show group help, NOT dispatch.
    const char* argv[] = {"erpl-adt", "search", "--help"};
    int exit_code = router.Dispatch(3, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: default action group-only shows group help", "[cli][router][default]") {
    CommandRouter router;
    router.SetGroupDescription("search", "Search for ABAP objects");
    router.Register("search", "query", "Search", [](const CommandArgs&) { return 42; });
    router.SetDefaultAction("search", "query");

    // "erpl-adt search" (no action, no args) should show group help.
    const char* argv[] = {"erpl-adt", "search"};
    int exit_code = router.Dispatch(2, argv);
    CHECK(exit_code == 0);
}

TEST_CASE("CommandRouter: PrintGroupHelp shows shorthand note for default action", "[cli][router][default]") {
    CommandRouter router;
    router.SetGroupDescription("search", "Search for ABAP objects");
    router.Register("search", "query", "Search", [](const CommandArgs&) { return 0; });
    router.SetDefaultAction("search", "query");

    std::ostringstream out;
    router.PrintGroupHelp("search", out);
    auto text = out.str();

    CHECK(text.find("Shorthand") != std::string::npos);
    CHECK(text.find("query") != std::string::npos);
}

// ===========================================================================
// Register with CommandHelp
// ===========================================================================

TEST_CASE("CommandRouter: Register with CommandHelp stores help metadata", "[cli][router][help]") {
    CommandRouter router;
    CommandHelp help;
    help.usage = "erpl-adt test run <uri>";
    help.args_description = "<uri>    Object URI";
    help.examples = {"erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST"};
    router.Register("test", "run", "Run tests",
                    [](const CommandArgs&) { return 0; }, std::move(help));

    auto cmds = router.CommandsForGroup("test");
    REQUIRE(cmds.size() == 1);
    REQUIRE(cmds[0].help.has_value());
    CHECK(cmds[0].help->usage == "erpl-adt test run <uri>");
    CHECK(cmds[0].help->args_description == "<uri>    Object URI");
    CHECK(cmds[0].help->examples.size() == 1);
}
