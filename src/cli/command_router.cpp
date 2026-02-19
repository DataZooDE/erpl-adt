#include <erpl_adt/cli/command_router.hpp>

#include <algorithm>
#include <iostream>
#include <set>

namespace erpl_adt {

namespace {

bool IsBooleanFlag(std::string_view arg) {
    return arg == "--color" || arg == "--no-color" ||
           arg == "--json" || arg == "--https" || arg == "--insecure" ||
           arg == "--help" || arg == "--raw" || arg == "--datasource" ||
           arg == "--search-desc" || arg == "--own-only" ||
           arg == "--simulate" || arg == "--validate" ||
           arg == "--background" || arg == "--force" || arg == "--no-search";
}

// Check if --json appears anywhere in argv (for pre-parse error formatting).
bool HasJsonFlag(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--json") return true;
    }
    return false;
}

// Emit a JSON-formatted error object to stderr.
void PrintJsonError(const std::string& message, std::ostream& out) {
    out << R"({"error":{"message":")" << message << R"("}})";
    out << "\n";
}

} // namespace

void CommandRouter::Register(const std::string& group,
                             const std::string& action,
                             const std::string& description,
                             CommandHandler handler) {
    auto key = group + ":" + action;
    CommandInfo info;
    info.group = group;
    info.action = action;
    info.description = description;
    info.handler = std::move(handler);
    commands_[key] = std::move(info);
}

void CommandRouter::Register(const std::string& group,
                             const std::string& action,
                             const std::string& description,
                             CommandHandler handler,
                             CommandHelp help) {
    auto key = group + ":" + action;
    CommandInfo info;
    info.group = group;
    info.action = action;
    info.description = description;
    info.handler = std::move(handler);
    info.help = std::move(help);
    commands_[key] = std::move(info);
}

void CommandRouter::SetGroupDescription(const std::string& group,
                                         const std::string& description) {
    group_descriptions_[group] = description;
}

void CommandRouter::SetGroupExamples(const std::string& group,
                                      std::vector<std::string> examples) {
    group_examples_[group] = std::move(examples);
}

void CommandRouter::SetDefaultAction(const std::string& group,
                                      const std::string& action) {
    default_actions_[group] = action;
}

int CommandRouter::Dispatch(int argc, const char* const* argv) const {
    bool json_mode = HasJsonFlag(argc, argv);
    auto parse_result = Parse(argc, argv);
    if (parse_result.IsErr()) {
        const auto& err = parse_result.Error();

        // Check if this is "Missing action for group 'X'" — show group help.
        const std::string prefix = "Missing action for group '";
        auto pos = err.find(prefix);
        if (pos != std::string::npos) {
            auto start = pos + prefix.size();
            auto end = err.find('\'', start);
            if (end != std::string::npos) {
                auto group = err.substr(start, end - start);
                if (HasGroup(group)) {
                    if (json_mode) {
                        PrintJsonError("Missing action for group '" + group + "'", std::cerr);
                    } else {
                        PrintGroupHelp(group, std::cout);
                    }
                    return 0;
                }
            }
        }

        if (json_mode) {
            PrintJsonError(err, std::cerr);
        } else {
            std::cerr << "Error: " << err << "\n";
            PrintHelp(std::cerr);
        }
        return 1;
    }

    auto args = std::move(parse_result).Value();
    json_mode = json_mode || args.flags.count("json") > 0;

    if (args.action.empty()) {
        if (args.flags.count("help") > 0) {
            if (HasGroup(args.group)) {
                PrintGroupHelp(args.group, std::cout);
            } else {
                if (json_mode) {
                    PrintJsonError("Unknown command group '" + args.group + "'", std::cerr);
                } else {
                    std::cerr << "Error: unknown command group '" << args.group << "'\n";
                    PrintHelp(std::cerr);
                }
                return 1;
            }
            return 0;
        }
        auto def_it = default_actions_.find(args.group);
        if (def_it != default_actions_.end()) {
            args.action = def_it->second;
        } else {
            if (json_mode) {
                PrintJsonError("Missing action for group '" + args.group + "'", std::cerr);
            } else {
                std::cerr << "Error: Missing action for group '" << args.group
                          << "'. Usage: erpl-adt " << args.group
                          << " <action> [args]\n";
                PrintHelp(std::cerr);
            }
            return 1;
        }
    }

    // If action is --help, -h, or "help" → show group help.
    if (args.action == "--help" || args.action == "-h" || args.action == "help") {
        if (HasGroup(args.group)) {
            PrintGroupHelp(args.group, std::cout);
        } else {
            if (json_mode) {
                PrintJsonError("Unknown command group '" + args.group + "'", std::cerr);
            } else {
                std::cerr << "Error: unknown command group '" << args.group << "'\n";
                PrintHelp(std::cerr);
            }
            return 1;
        }
        return 0;
    }

    // If flags contain "help" → show command help.
    if (args.flags.count("help") > 0) {
        auto key = args.group + ":" + args.action;
        if (commands_.find(key) != commands_.end()) {
            PrintCommandHelp(args.group, args.action, std::cout);
        } else if (HasGroup(args.group)) {
            PrintGroupHelp(args.group, std::cout);
        } else {
            if (json_mode) {
                PrintJsonError("Unknown command '" + args.group + " " + args.action + "'", std::cerr);
            } else {
                std::cerr << "Error: unknown command '" << args.group
                          << " " << args.action << "'\n";
                PrintHelp(std::cerr);
            }
            return 1;
        }
        return 0;
    }

    auto key = args.group + ":" + args.action;
    auto it = commands_.find(key);

    // Default action fallback: if action not found, check if group has a
    // default action. Treat the parsed "action" as the first positional arg.
    if (it == commands_.end()) {
        auto def_it = default_actions_.find(args.group);
        if (def_it != default_actions_.end()) {
            args.positional.insert(args.positional.begin(), args.action);
            args.action = def_it->second;
            key = args.group + ":" + args.action;
            it = commands_.find(key);
        }
    }

    if (it == commands_.end()) {
        if (json_mode) {
            PrintJsonError("Unknown command '" + args.group + " " + args.action + "'", std::cerr);
        } else {
            std::cerr << "Error: unknown command '" << args.group
                      << " " << args.action << "'\n";
            if (HasGroup(args.group)) {
                PrintGroupHelp(args.group, std::cerr);
            } else {
                PrintHelp(std::cerr);
            }
        }
        return 1;
    }

    return it->second.handler(args);
}

Result<CommandArgs, std::string> CommandRouter::Parse(
    int argc, const char* const* argv) {
    CommandArgs args;

    // Skip argv[0] (program name).
    // We need at least group and action.
    int i = 1;

    // Skip global flags before the group.
    while (i < argc) {
        std::string_view arg{argv[i]};
        // Skip short verbosity flags (-v, -vv) — already handled by main.
        if (arg == "-v" || arg == "-vv") {
            ++i;
            continue;
        }
        if (arg.substr(0, 2) == "--") {
            // Global flag: --key=value or --key value
            auto eq = arg.find('=');
            if (eq != std::string_view::npos) {
                auto key = std::string(arg.substr(2, eq - 2));
                auto val = std::string(arg.substr(eq + 1));
                args.flags[key] = val;
                ++i;
            } else {
                auto key = std::string(arg.substr(2));
                if (IsBooleanFlag(arg)) {
                    args.flags[key] = "true";
                    ++i;
                } else if (i + 1 < argc &&
                           std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                    args.flags[key] = argv[i + 1];
                    i += 2;
                } else {
                    args.flags[key] = "true";
                    ++i;
                }
            }
        } else {
            break;
        }
    }

    if (i >= argc) {
        return Result<CommandArgs, std::string>::Err(
            "Missing command group. Usage: erpl-adt <group> <action> [args]");
    }
    args.group = argv[i++];

    if (i >= argc) {
        return Result<CommandArgs, std::string>::Err(
            "Missing action for group '" + args.group +
            "'. Usage: erpl-adt " + args.group + " <action> [args]");
    }
    if (std::string_view{argv[i]}.substr(0, 2) == "--") {
        args.action = "";
    } else {
        args.action = argv[i++];
    }

    // Remaining args: flags and positional.
    while (i < argc) {
        std::string_view arg{argv[i]};
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq != std::string_view::npos) {
                auto key = std::string(arg.substr(2, eq - 2));
                auto val = std::string(arg.substr(eq + 1));
                args.flags[key] = val;
                ++i;
            } else {
                auto key = std::string(arg.substr(2));
                if (IsBooleanFlag(arg)) {
                    args.flags[key] = "true";
                    ++i;
                } else if (i + 1 < argc &&
                           std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                    args.flags[key] = argv[i + 1];
                    i += 2;
                } else {
                    args.flags[key] = "true";
                    ++i;
                }
            }
        } else {
            args.positional.emplace_back(argv[i]);
            ++i;
        }
    }

    return Result<CommandArgs, std::string>::Ok(std::move(args));
}

std::vector<std::string> CommandRouter::Groups() const {
    std::set<std::string> groups;
    for (const auto& [key, info] : commands_) {
        groups.insert(info.group);
    }
    return {groups.begin(), groups.end()};
}

bool CommandRouter::HasGroup(const std::string& group) const {
    for (const auto& [key, info] : commands_) {
        if (info.group == group) {
            return true;
        }
    }
    return false;
}

std::vector<CommandInfo> CommandRouter::CommandsForGroup(
    const std::string& group) const {
    std::vector<CommandInfo> result;
    for (const auto& [key, info] : commands_) {
        if (info.group == group) {
            result.push_back(info);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const CommandInfo& a, const CommandInfo& b) {
                  return a.action < b.action;
              });
    return result;
}

std::string CommandRouter::GroupDescription(const std::string& group) const {
    auto it = group_descriptions_.find(group);
    return (it != group_descriptions_.end()) ? it->second : "";
}

std::vector<std::string> CommandRouter::GroupExamples(const std::string& group) const {
    auto it = group_examples_.find(group);
    return (it != group_examples_.end()) ? it->second : std::vector<std::string>{};
}

void CommandRouter::PrintHelp(std::ostream& out) const {
    out << "\nUsage: erpl-adt <group> <action> [options]\n\n";
    out << "Available commands:\n";

    auto groups = Groups();
    for (const auto& group : groups) {
        out << "\n  " << group << ":\n";
        auto cmds = CommandsForGroup(group);
        for (const auto& cmd : cmds) {
            out << "    " << cmd.action;
            if (!cmd.description.empty()) {
                out << " - " << cmd.description;
            }
            out << "\n";
        }
    }
    out << "\n";
}

void CommandRouter::PrintGroupHelp(const std::string& group,
                                    std::ostream& out) const {
    auto desc = GroupDescription(group);
    if (desc.empty()) {
        desc = group;
    }
    out << "erpl-adt " << group << " - " << desc << "\n";

    out << "\nActions:\n";
    auto cmds = CommandsForGroup(group);
    // Find max action name length for alignment.
    size_t max_len = 0;
    for (const auto& cmd : cmds) {
        max_len = std::max(max_len, cmd.action.size());
    }
    for (const auto& cmd : cmds) {
        out << "  " << cmd.action;
        out << std::string(max_len - cmd.action.size() + 6, ' ');
        out << cmd.description << "\n";
    }

    auto examples = GroupExamples(group);
    if (!examples.empty()) {
        out << "\nExamples:\n";
        for (const auto& ex : examples) {
            out << "  " << ex << "\n";
        }
    }

    auto def_it = default_actions_.find(group);
    if (def_it != default_actions_.end()) {
        out << "\nShorthand: the '" << def_it->second
            << "' action is the default, so 'erpl-adt " << group
            << " <args>' is equivalent to 'erpl-adt " << group
            << " " << def_it->second << " <args>'.\n";
    }

    out << "\nUse \"erpl-adt " << group
        << " <action> --help\" for details on a specific action.\n";
}

void CommandRouter::PrintCommandHelp(const std::string& group,
                                      const std::string& action,
                                      std::ostream& out) const {
    auto key = group + ":" + action;
    auto it = commands_.find(key);
    if (it == commands_.end()) {
        out << "Error: unknown command '" << group << " " << action << "'\n";
        return;
    }

    const auto& cmd = it->second;
    out << "erpl-adt " << group << " " << action << " - " << cmd.description << "\n";

    if (!cmd.help) {
        // No detailed help — just show the description.
        out << "\nNo detailed help available for this command.\n";
        return;
    }

    const auto& help = *cmd.help;

    if (!help.usage.empty()) {
        out << "\nUsage:\n  " << help.usage << "\n";
    }

    if (!help.args_description.empty()) {
        out << "\nArguments:\n  " << help.args_description << "\n";
    }

    if (!help.flags.empty()) {
        out << "\nFlags:\n";
        // Find max flag display length for alignment.
        std::vector<std::string> flag_displays;
        size_t max_len = 0;
        for (const auto& f : help.flags) {
            std::string display = "--" + f.name;
            if (!f.placeholder.empty()) {
                display += " " + f.placeholder;
            }
            max_len = std::max(max_len, display.size());
            flag_displays.push_back(std::move(display));
        }
        for (size_t i = 0; i < help.flags.size(); ++i) {
            out << "  " << flag_displays[i];
            out << std::string(max_len - flag_displays[i].size() + 4, ' ');
            out << help.flags[i].description;
            if (help.flags[i].required) {
                out << " (required)";
            }
            out << "\n";
        }
    }

    if (!help.long_description.empty()) {
        out << "\n" << help.long_description << "\n";
    }

    if (!help.examples.empty()) {
        out << "\nExamples:\n";
        for (const auto& ex : help.examples) {
            out << "  " << ex << "\n";
        }
    }
}

} // namespace erpl_adt
