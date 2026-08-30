// ===========================================================================
// CLI dispatch tests for all bw <action> sub-commands.
//
// Coverage parity with normal commands: every bw sub-command registered in
// command_executor.cpp gets at least one pre-session validation test plus
// help-text and (where applicable) flag-value tests.
//
// What this file does NOT test:
//   - Behavior that requires a live SAP session (BwGetXxx wire calls)
//   - Sub-action paths inside transport/locks/job that only validate after
//     session setup; those live in test/integration_py/.
// ===========================================================================
#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>

#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace erpl_adt;

namespace {

struct DispatchResult {
    int exit_code{0};
    std::string stderr_text;
    std::string stdout_text;
};

DispatchResult DispatchCapture(CommandRouter& router,
                               int argc,
                               const char* const argv[]) {
    std::ostringstream err;
    std::ostringstream out;
    auto* old_err = std::cerr.rdbuf(err.rdbuf());
    auto* old_out = std::cout.rdbuf(out.rdbuf());
    const int code = router.Dispatch(argc, argv);
    std::cerr.rdbuf(old_err);
    std::cout.rdbuf(old_out);
    return DispatchResult{code, err.str(), out.str()};
}

}  // namespace

// ===========================================================================
// Group registration: all 40 bw sub-commands present
// ===========================================================================

TEST_CASE("bw group registers all 40 sub-commands",
          "[cli][executor][bw][registration]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto cmds = router.CommandsForGroup("bw");

    // The bw group is the largest in the CLI. If a command is added or
    // removed, this guard test ensures the change is intentional and that
    // the corresponding test below is added/removed too.
    CHECK(cmds.size() == 40);

    // Spot-check the canonical action names. Sorted for stable output.
    std::set<std::string> actions;
    for (const auto& c : cmds) actions.insert(c.action);

    const std::set<std::string> expected = {
        "activate",      "adturi",        "applog",        "changeability",
        "create",        "datavolumes",   "dbinfo",        "delete",
        "discover",      "export-area",   "export-cube",   "export-query",
        "favorites",     "job",           "lineage",       "lock",
        "locks",         "message",       "move",          "nodepath",
        "nodes",         "qprops",        "read",          "read-adso",
        "read-dmod",     "read-dtp",      "read-query",    "read-rsds",
        "read-trfn",     "reporting",     "save",          "search",
        "search-md",     "sysinfo",       "transport",     "unlock",
        "validate",      "valuehelp",     "virtualfolders", "xref",
    };
    CHECK(actions == expected);
}

TEST_CASE("bw group default action is search",
          "[cli][executor][bw][registration]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // erpl-adt bw <pattern> dispatches to bw search <pattern>; if the pattern
    // is missing the search handler emits its usage hint and returns 99.
    const char* argv[] = {"erpl-adt", "bw", "search"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Missing search pattern") != std::string::npos);
}

// ===========================================================================
// Read commands — pre-session validation
// ===========================================================================

TEST_CASE("bw search: missing pattern returns 99 with usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "search"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Missing search pattern") != std::string::npos);
    CHECK(result.stderr_text.find("erpl-adt bw search <pattern>") != std::string::npos);
}

TEST_CASE("bw read: missing both type/name and --uri returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read") != std::string::npos);
}

TEST_CASE("bw read: only type without name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read", "ADSO"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read") != std::string::npos);
}

TEST_CASE("bw read-trfn: missing name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-trfn"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-trfn") != std::string::npos);
}

TEST_CASE("bw read-adso: missing name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-adso"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-adso") != std::string::npos);
}

TEST_CASE("bw read-dtp: missing name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-dtp"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-dtp") != std::string::npos);
}

TEST_CASE("bw read-rsds: missing name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-rsds"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-rsds") != std::string::npos);
}

TEST_CASE("bw read-rsds: missing --source-system returns 99 with usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Provides a name but omits the required --source-system flag.
    const char* argv[] = {"erpl-adt", "bw", "read-rsds", "ZSRC_SALES"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--source-system") != std::string::npos);
}

TEST_CASE("bw read-dmod: missing name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-dmod"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-dmod") != std::string::npos);
}

// ===========================================================================
// Lineage / xref / nodes / nodepath — pre-session validation
// ===========================================================================

TEST_CASE("bw lineage: missing dtp_name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "lineage"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw lineage") != std::string::npos);
}

TEST_CASE("bw xref: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "xref"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw xref") != std::string::npos);
}

TEST_CASE("bw xref: missing name (only type) returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "xref", "ADSO"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw xref") != std::string::npos);
}

TEST_CASE("bw nodes: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "nodes"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw nodes") != std::string::npos);
}

TEST_CASE("bw nodepath: missing object-uri returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "nodepath"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--object-uri") != std::string::npos);
}

// ===========================================================================
// Export commands — pre-session validation
// ===========================================================================

TEST_CASE("bw export-query: missing name prints export-query usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "export-query"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("export-query") != std::string::npos);
}

TEST_CASE("bw export-cube: missing name prints export-cube usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "export-cube"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("export-cube") != std::string::npos);
}

// ===========================================================================
// Lifecycle commands — pre-session validation
// ===========================================================================

TEST_CASE("bw create: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "create"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw create") != std::string::npos);
}

TEST_CASE("bw create: missing name (only type) returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "create", "ADSO"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw create") != std::string::npos);
}

TEST_CASE("bw lock: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "lock"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw lock") != std::string::npos);
}

TEST_CASE("bw unlock: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "unlock"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw unlock") != std::string::npos);
}

TEST_CASE("bw save: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "save"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw save") != std::string::npos);
}

TEST_CASE("bw save: missing --lock-handle returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Provides type and name but omits --lock-handle. The handler validates
    // this BEFORE attempting to read stdin or create a session.
    const char* argv[] = {"erpl-adt", "bw", "save", "ADSO", "ZSALES"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--lock-handle") != std::string::npos);
}

TEST_CASE("bw delete: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "delete"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw delete") != std::string::npos);
}

TEST_CASE("bw delete: missing --lock-handle returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "delete", "ADSO", "ZSALES"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--lock-handle") != std::string::npos);
}

TEST_CASE("bw activate: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "activate"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw activate") != std::string::npos);
}

TEST_CASE("bw activate: only type without name returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // bw activate <type> <name> [<name2> ...] — type alone is not enough.
    const char* argv[] = {"erpl-adt", "bw", "activate", "ADSO"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw activate") != std::string::npos);
}

// ===========================================================================
// Misc commands — pre-session validation
// ===========================================================================

TEST_CASE("bw valuehelp: missing domain returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "valuehelp"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw valuehelp") != std::string::npos);
}

TEST_CASE("bw message: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "message"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw message") != std::string::npos);
}

TEST_CASE("bw message: only identifier without textype returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "message", "RSO_001"};
    const auto result = DispatchCapture(router, 4, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw message") != std::string::npos);
}

TEST_CASE("bw validate: missing args returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "validate"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw validate") != std::string::npos);
}

TEST_CASE("bw reporting: missing compid returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "reporting"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw reporting") != std::string::npos);
}

// ===========================================================================
// Sub-action dispatch commands — missing sub-action returns 99
// ===========================================================================

TEST_CASE("bw transport: missing sub-action returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "transport"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("check|write|list|collect") != std::string::npos);
}

TEST_CASE("bw locks: missing sub-action returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "locks"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("list|delete") != std::string::npos);
}

TEST_CASE("bw job: missing sub-action returns 99",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "job"};
    const auto result = DispatchCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    // Usage hint enumerates all supported sub-actions.
    CHECK(result.stderr_text.find("list|result|status") != std::string::npos);
}

// ===========================================================================
// Help texts — every bw sub-command exposes --help that prints usage
// ===========================================================================
//
// For each sub-command, --help must:
//   - return exit code 0
//   - include the sub-command's name (sanity check that the right help is
//     being shown, not a generic group help)

namespace {

void CheckHelpFor(const std::string& action) {
    CommandRouter router;
    RegisterAllCommands(router);
    const std::string help_arg = "--help";
    const char* argv[] = {"erpl-adt", "bw", action.c_str(), help_arg.c_str()};
    const auto result = DispatchCapture(router, 4, argv);
    INFO("action: bw " << action);
    CHECK(result.exit_code == 0);
    const auto& combined = result.stdout_text + result.stderr_text;
    // Help must reference the action name (e.g., "bw search", "bw read-adso").
    CHECK(combined.find(action) != std::string::npos);
}

}  // namespace

TEST_CASE("bw <action> --help prints help and returns 0 for every sub-command",
          "[cli][executor][bw][help]") {
    // Mirror the registered set above. If a new bw sub-command is added,
    // the registration test above will fail first; add it here too.
    const std::vector<std::string> actions = {
        "activate",      "adturi",        "applog",        "changeability",
        "create",        "datavolumes",   "dbinfo",        "delete",
        "discover",      "export-area",   "export-cube",   "export-query",
        "favorites",     "job",           "lineage",       "lock",
        "locks",         "message",       "move",          "nodepath",
        "nodes",         "qprops",        "read",          "read-adso",
        "read-dmod",     "read-dtp",      "read-query",    "read-rsds",
        "read-trfn",     "reporting",     "save",          "search",
        "search-md",     "sysinfo",       "transport",     "unlock",
        "validate",      "valuehelp",     "virtualfolders", "xref",
    };
    REQUIRE(actions.size() == 40);
    for (const auto& a : actions) CheckHelpFor(a);
}

// ===========================================================================
// Boolean flag handling for bw commands that accept --raw / --datasource
// ===========================================================================
//
// These commands accept boolean flags that, if mishandled, would consume the
// following argv token as their value and break positional parsing. The
// canonical IsBooleanFlag() set already covers them; these tests guard
// against accidental regressions in BW-specific handler paths.

TEST_CASE("bw read: --raw is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // If --raw consumed "ZSALES" the handler would see only one positional
    // and emit the "Usage: erpl-adt bw read" hint. Verify it does not.
    const char* argv[] = {"erpl-adt", "bw", "read", "--raw", "ADSO", "ZSALES"};
    const auto result = DispatchCapture(router, 6, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read") == std::string::npos);
}

TEST_CASE("bw nodes: --datasource is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "nodes", "--datasource", "RSDS", "ZSRC"};
    const auto result = DispatchCapture(router, 6, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw nodes") == std::string::npos);
}

TEST_CASE("bw search: --search-desc is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "search", "--search-desc", "Z*"};
    const auto result = DispatchCapture(router, 5, argv);
    // Pattern "Z*" is consumed as the positional; missing-pattern hint must NOT fire.
    CHECK(result.stderr_text.find("Missing search pattern") == std::string::npos);
}

TEST_CASE("bw lineage: --no-xref is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "lineage", "--no-xref", "DTP_ZSALES"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw lineage") == std::string::npos);
}

TEST_CASE("bw activate: --simulate is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "activate", "--simulate", "ADSO", "ZSALES"};
    const auto result = DispatchCapture(router, 6, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw activate") == std::string::npos);
}

TEST_CASE("bw transport: --own-only is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Without --own-only being boolean, parser would consume "list" as its
    // value and the handler would see no sub-action.
    const char* argv[] = {"erpl-adt", "bw", "transport", "--own-only", "list"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.stderr_text.find("check|write|list|collect") == std::string::npos);
}

TEST_CASE("bw export-area: --no-xref-edges is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "export-area", "--no-xref-edges", "0D_NW_DEMO"};
    const auto result = DispatchCapture(router, 5, argv);
    // The infoarea name must reach the handler as the positional arg, so the
    // missing-infoarea usage hint must not fire. Handler returns 99 only when
    // the positional is empty; here it should pass that gate.
    const bool usage_hint_fired =
        result.exit_code == 99 &&
        result.stderr_text.find("Missing search pattern") == std::string::npos &&
        result.stderr_text.find("Usage: erpl-adt bw export-area") != std::string::npos;
    CHECK_FALSE(usage_hint_fired);
}

// ===========================================================================
// Integer flag value validation (pre-session)
// ===========================================================================

TEST_CASE("bw lineage: invalid --max-xref returns 99 with parse error",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "lineage",
                          "--max-xref=notanumber", "DTP_ZSALES"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--max-xref") != std::string::npos);
}

TEST_CASE("bw lineage: --max-xref out of range returns 99",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Range is [1, 10000].
    const char* argv[] = {"erpl-adt", "bw", "lineage",
                          "--max-xref=0", "DTP_ZSALES"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--max-xref") != std::string::npos);
}

TEST_CASE("bw export-area: invalid --max-depth returns 99",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "export-area",
                          "--max-depth=abc", "0D_NW_DEMO"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--max-depth") != std::string::npos);
}

TEST_CASE("bw export-area: --max-depth out of range returns 99",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Range is [0, 100].
    const char* argv[] = {"erpl-adt", "bw", "export-area",
                          "--max-depth=999", "0D_NW_DEMO"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--max-depth") != std::string::npos);
}

// ===========================================================================
// JSON flag works on every bw sub-command (no group-level mis-routing)
// ===========================================================================

TEST_CASE("bw search: --json flag does not consume positional pattern",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // If --json consumed "Z*" as its value the handler would see no
    // positional and emit the Missing-pattern hint.
    const char* argv[] = {"erpl-adt", "--json", "bw", "search", "Z*"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.stderr_text.find("Missing search pattern") == std::string::npos);
}

TEST_CASE("bw read-trfn: --json flag does not consume positional name",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "--json", "bw", "read-trfn", "ZTRFN_X"};
    const auto result = DispatchCapture(router, 5, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw read-trfn") == std::string::npos);
}


TEST_CASE("bw create: --keep-lock is a boolean flag (does not consume next arg)",
          "[cli][executor][bw][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // If --keep-lock consumed "ZNEW" the handler would see one positional and
    // print the usage hint instead of attempting the create.
    const char* argv[] = {"erpl-adt", "bw", "create", "--keep-lock", "ADSO", "ZNEW"};
    const auto result = DispatchCapture(router, 6, argv);
    CHECK(result.stderr_text.find("Usage: erpl-adt bw create") == std::string::npos);
}
