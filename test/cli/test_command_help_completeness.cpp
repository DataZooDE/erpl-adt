#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// ===========================================================================
// Examples use only flags their command knows. Every command warns on an
// unknown flag (and the destructive ones refuse it), so an example with a
// stale or misspelled flag would teach users a warning — or a refusal.
// ===========================================================================

namespace {

// Splits a shell-ish command line; quotes group words, no escapes.
std::vector<std::string> SplitWords(const std::string& line) {
    std::vector<std::string> words;
    std::string cur;
    char quote = 0;
    bool have = false;
    for (char c : line) {
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
            } else {
                cur += c;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            have = true;
        } else if (c == ' ' || c == '\t') {
            if (have || !cur.empty()) {
                words.push_back(cur);
            }
            cur.clear();
            have = false;
        } else if (c == '#' && cur.empty() && !have) {
            break;  // trailing comment
        } else {
            cur += c;
        }
    }
    if (have || !cur.empty()) {
        words.push_back(cur);
    }
    return words;
}

// Unknown flags of one example line ("erpl-adt [global] <group> <action> ..."),
// as "group action --flag"; empty when the line is not a routed command.
std::vector<std::string> UnknownFlagsInExample(const CommandRouter& router,
                                               const std::string& line) {
    auto words = SplitWords(line);
    auto start = std::find(words.begin(), words.end(), "erpl-adt");
    if (start == words.end()) {
        return {};
    }
    std::vector<const char*> argv;
    for (auto it = start; it != words.end(); ++it) {
        if (*it == "|" || *it == ">" || *it == "&&") {
            break;
        }
        argv.push_back(it->c_str());
    }
    auto parsed = CommandRouter::Parse(static_cast<int>(argv.size()), argv.data());
    if (parsed.IsErr()) {
        return {};
    }
    auto args = parsed.Value();
    const auto commands = router.CommandsForGroup(args.group);
    auto cmd = std::find_if(commands.begin(), commands.end(),
                            [&](const CommandInfo& c) { return c.action == args.action; });
    if (cmd == commands.end()) {
        return {};  // default action ("search ZCL_*") or not a router command
    }
    std::vector<std::string> known = CommandRouter::GlobalFlagNames();
    if (cmd->help) {
        for (const auto& f : cmd->help->flags) {
            known.push_back(f.name);
        }
    }
    std::vector<std::string> unknown;
    for (const auto& [flag, value] : args.flags) {
        if (std::find(known.begin(), known.end(), flag) == known.end()) {
            unknown.push_back(args.group + " " + args.action + " --" + flag);
        }
    }
    return unknown;
}

std::string RepoRoot() {
    std::string this_file = __FILE__;
    auto test_dir = this_file.substr(0, this_file.find_last_of("/\\"));
    return test_dir.substr(0, test_dir.find_last_of("/\\") - 5);  // strip "/test"
}

}  // namespace

TEST_CASE("Help examples use only flags their command knows",
          "[cli][help][completeness][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    std::vector<std::string> unknown;
    for (const auto& group : router.Groups()) {
        for (const auto& cmd : router.CommandsForGroup(group)) {
            if (!cmd.help) {
                continue;
            }
            for (const auto& example : cmd.help->examples) {
                for (const auto& u : UnknownFlagsInExample(router, example)) {
                    unknown.push_back(u + "   in: " + example);
                }
            }
        }
        for (const auto& example : router.GroupExamples(group)) {
            for (const auto& u : UnknownFlagsInExample(router, example)) {
                unknown.push_back(u + "   in: " + example);
            }
        }
    }
    for (const auto& u : unknown) {
        UNSCOPED_INFO(u);
    }
    CHECK(unknown.empty());
}

TEST_CASE("Examples in docs/cli-usage.md and docs/abapgit-deploy.md use known flags",
          "[cli][help][completeness][flags]") {
    CommandRouter router;
    RegisterAllCommands(router);
    std::vector<std::string> unknown;
    for (const char* doc : {"docs/cli-usage.md", "docs/abapgit-deploy.md"}) {
        std::ifstream in(RepoRoot() + "/" + doc);
        REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line)) {
            // The unknown-flag section shows misspellings on purpose.
            if (line.find("--activte") != std::string::npos ||
                line.find("--transprt") != std::string::npos) {
                continue;
            }
            for (const auto& u : UnknownFlagsInExample(router, line)) {
                unknown.push_back(std::string(doc) + ": " + u + "   in: " + line);
            }
        }
    }
    for (const auto& u : unknown) {
        UNSCOPED_INFO(u);
    }
    CHECK(unknown.empty());
}
