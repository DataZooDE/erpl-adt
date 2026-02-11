#pragma once

#include <erpl_adt/cli/command_router.hpp>

#include <iosfwd>

namespace erpl_adt {

// Register all ADT operation commands with the router.
void RegisterAllCommands(CommandRouter& router);

// Print top-level help (all groups, global flags, quick-start examples).
// When color=true, uses ANSI escape codes for bold/dim/yellow formatting.
void PrintTopLevelHelp(const CommandRouter& router, std::ostream& out, bool color);

// Print login-specific help.
void PrintLoginHelp(std::ostream& out, bool color);

// Print logout-specific help.
void PrintLogoutHelp(std::ostream& out, bool color);

// Check if argv contains a new-style command group (search, object, etc.)
// rather than a legacy deploy subcommand.
bool IsNewStyleCommand(int argc, const char* const* argv);

// Handle `erpl-adt login ...` — save credentials to .adt.creds.
int HandleLogin(int argc, const char* const* argv);

// Handle `erpl-adt logout` — delete .adt.creds.
int HandleLogout();

} // namespace erpl_adt
