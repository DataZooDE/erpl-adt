#pragma once

#include <erpl_adt/core/result.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CommandArgs — parsed command-line arguments for a specific command.
// ---------------------------------------------------------------------------
struct CommandArgs {
    std::string group;                   // e.g. "search", "object", "deploy"
    std::string action;                  // e.g. "read", "list", "run"
    std::vector<std::string> positional; // remaining positional arguments
    std::map<std::string, std::string> flags; // --key=value pairs
};

// ---------------------------------------------------------------------------
// CommandHandler — function type for command implementations.
// Returns 0 on success, non-zero exit code on failure.
// ---------------------------------------------------------------------------
using CommandHandler = std::function<int(const CommandArgs& args)>;

// ---------------------------------------------------------------------------
// FlagHelp — help metadata for a single command flag.
// ---------------------------------------------------------------------------
struct FlagHelp {
    std::string name;        // e.g. "file"
    std::string placeholder; // e.g. "<path>"
    std::string description; // e.g. "Path to local source file"
    bool required = false;
};

// ---------------------------------------------------------------------------
// CommandHelp — detailed help metadata for a single command.
// ---------------------------------------------------------------------------
struct CommandHelp {
    std::string usage;                    // e.g. "erpl-adt source write <uri> --file <path> [flags]"
    std::string args_description;         // e.g. "<uri>    Source URI (e.g., ...)"
    std::string long_description;         // paragraph below usage (optional)
    std::vector<FlagHelp> flags;
    std::vector<std::string> examples;    // full command lines
};

// ---------------------------------------------------------------------------
// CommandInfo — metadata for a registered command.
// ---------------------------------------------------------------------------
struct CommandInfo {
    std::string group;
    std::string action;
    std::string description;
    CommandHandler handler;
    std::optional<CommandHelp> help;
};

// ---------------------------------------------------------------------------
// CommandRouter — two-level dispatch for CLI commands.
//
// Commands are registered as group/action pairs. The router parses argv,
// extracts the group and action, and dispatches to the registered handler.
//
// Usage:
//   CommandRouter router;
//   router.Register("search", "objects", "Search for objects", handler);
//   return router.Dispatch(argc, argv);
// ---------------------------------------------------------------------------
class CommandRouter {
public:
    CommandRouter() = default;

    // Register a command handler for a group/action pair.
    void Register(const std::string& group,
                  const std::string& action,
                  const std::string& description,
                  CommandHandler handler);

    // Register a command handler with detailed help metadata.
    void Register(const std::string& group,
                  const std::string& action,
                  const std::string& description,
                  CommandHandler handler,
                  CommandHelp help);

    // Set group-level description (shown in group help).
    void SetGroupDescription(const std::string& group,
                             const std::string& description);

    // Set group-level examples (shown in group help).
    void SetGroupExamples(const std::string& group,
                          std::vector<std::string> examples);

    // Set a default action for a group. When the parsed action doesn't match
    // any registered action, the default action is used instead and the
    // parsed "action" token is prepended to positional args.
    // Example: "erpl-adt search ZCL_*" → dispatches to search:query with positional[0]="ZCL_*"
    void SetDefaultAction(const std::string& group, const std::string& action);

    // Parse argv and dispatch to the matching handler.
    // Returns the exit code from the handler, or 1 on routing error.
    // Intercepts --help/-h at group and command levels.
    int Dispatch(int argc, const char* const* argv) const;

    // Parse argv into CommandArgs without dispatching.
    static Result<CommandArgs, std::string> Parse(int argc, const char* const* argv);

    // Return true if arg is a boolean flag that does not consume the next
    // token as its value (e.g. --json, --color, --no-xref-edges, ...).
    // Used by both CommandRouter::Parse and IsNewStyleCommand in the executor.
    static bool IsBooleanFlag(std::string_view arg);

    // Get all registered command groups (sorted).
    [[nodiscard]] std::vector<std::string> Groups() const;

    // Check if a group exists.
    [[nodiscard]] bool HasGroup(const std::string& group) const;

    // Get all commands for a specific group.
    [[nodiscard]] std::vector<CommandInfo> CommandsForGroup(const std::string& group) const;

    // Get group description (empty string if not set).
    [[nodiscard]] std::string GroupDescription(const std::string& group) const;

    // Get group examples (empty vector if not set).
    [[nodiscard]] std::vector<std::string> GroupExamples(const std::string& group) const;

    // Print help text listing all groups and commands (legacy).
    void PrintHelp(std::ostream& out) const;

    // Print group-level help (actions, examples).
    void PrintGroupHelp(const std::string& group, std::ostream& out) const;

    // Print command-level help (flags, args, examples).
    void PrintCommandHelp(const std::string& group,
                          const std::string& action,
                          std::ostream& out) const;

private:
    // Key: "group:action"
    std::map<std::string, CommandInfo> commands_;
    std::map<std::string, std::string> group_descriptions_;
    std::map<std::string, std::vector<std::string>> group_examples_;
    std::map<std::string, std::string> default_actions_;
};

} // namespace erpl_adt
