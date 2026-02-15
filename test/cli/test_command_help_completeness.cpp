#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>

#include <sstream>

using namespace erpl_adt;

// ===========================================================================
// Help completeness: catch drift between registered commands and help output.
// ===========================================================================

TEST_CASE("All router groups appear in top-level help",
          "[cli][help][completeness]") {
    CommandRouter router;
    RegisterAllCommands(router);

    std::ostringstream oss;
    PrintTopLevelHelp(router, oss, /*color=*/false);
    const auto help = oss.str();

    // Each group's commands are listed under their section.
    // Verify at least one action or description from every group appears.
    // This catches a group being omitted from the hardcoded group_order list.
    for (const auto& group : router.Groups()) {
        auto cmds = router.CommandsForGroup(group);
        REQUIRE_FALSE(cmds.empty());

        bool found = false;
        for (const auto& cmd : cmds) {
            // Check for action name or description — default actions
            // may display as "group <arg>" rather than the action name.
            if (help.find(cmd.action) != std::string::npos ||
                help.find(cmd.description) != std::string::npos) {
                found = true;
                break;
            }
        }
        INFO("No commands from group '" << group
             << "' found in top-level help output");
        CHECK(found);
    }
}

TEST_CASE("All BW commands appear in BW group help",
          "[cli][help][completeness]") {
    CommandRouter router;
    RegisterAllCommands(router);

    std::ostringstream oss;
    PrintBwGroupHelp(router, oss, /*color=*/false);
    const auto help = oss.str();

    for (const auto& cmd : router.CommandsForGroup("bw")) {
        INFO("BW action missing from BW group help: " << cmd.action);
        CHECK(help.find(cmd.action) != std::string::npos);
    }
}
