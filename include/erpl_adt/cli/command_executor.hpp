#pragma once

#include <erpl_adt/adt/classrun.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/i_xml_codec.hpp>
#include <erpl_adt/cli/command_router.hpp>
#include <erpl_adt/cli/output_formatter.hpp>
#include <erpl_adt/core/result.hpp>

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

namespace erpl_adt {

// Register all ADT operation commands with the router.
void RegisterAllCommands(CommandRouter& router);

// ---------------------------------------------------------------------------
// RunSourceEdit — core read→edit→write logic for `source edit`.
//
// Exposed here for unit-testing with an injectable editor function.
// Production code passes the real LaunchEditor; tests pass a lambda.
//
// Parameters:
//   session     — ADT session (used for ReadSource / WriteSourceWithAutoLock)
//   source_uri  — Full source URI, e.g. /sap/bc/adt/oo/classes/zcl_x/source/main
//   transport   — Optional transport request number
//   activate    — If true, activate object after successful write
//   no_write    — If true, open editor but skip writing back (dry run)
//   editor_fn   — Callable invoked with temp-file path; return value ignored
//   out         — Stream for success/status messages (default: stdout)
//   err         — Stream for error messages (default: stderr)
//
// Returns 0 on success (including no-change), non-zero on failure.
// ---------------------------------------------------------------------------
using SourceEditorFn = std::function<int(const std::string& path)>;

int RunSourceEdit(IAdtSession& session,
                  const std::string& source_uri,
                  const std::optional<std::string>& transport,
                  bool activate,
                  bool no_write,
                  SourceEditorFn editor_fn,
                  std::ostream& out,
                  std::ostream& err);

// Print top-level help (all groups, global flags, quick-start examples).
// When color=true, uses ANSI escape codes for bold/dim/yellow formatting.
void PrintTopLevelHelp(const CommandRouter& router, std::ostream& out, bool color);

// Print login-specific help.
void PrintLoginHelp(std::ostream& out, bool color);

// Print logout-specific help.
void PrintLogoutHelp(std::ostream& out, bool color);

// Print detailed BW group help with sub-categories and ANSI formatting.
void PrintBwGroupHelp(const CommandRouter& router, std::ostream& out, bool color);

// Check if argv contains a new-style command group (search, object, etc.)
// rather than a legacy deploy subcommand.
bool IsNewStyleCommand(int argc, const char* const* argv);

// Check if argv is a BW group help request (bare "bw", "bw --help", "bw help").
// Returns false for "bw <action> ..." — those are handled by the router.
bool IsBwHelpRequest(int argc, const char* const* argv);

// Handle `erpl-adt login ...` — save credentials to .adt.creds.
int HandleLogin(int argc, const char* const* argv);

// Handle `erpl-adt logout` — delete .adt.creds.
int HandleLogout();

// Returns true if the file at `path` has group- or other-readable permissions
// on POSIX (.adt.creds is supposed to be chmod 600). Always returns false on
// Windows (no equivalent POSIX permission model).
//
// Exposed for unit testing; production callers use it to emit a warning when
// loading a creds file with weak permissions.
[[nodiscard]] bool CredentialsFileHasLooseMode(const std::string& path);

// ---------------------------------------------------------------------------
// Outcome reporters — pure logic extracted for direct unit-testing.
//
// These functions interpret already-fetched result structs and emit output.
// No IAdtSession or HTTP interaction required.
// ---------------------------------------------------------------------------

// Report the outcome of an activation request.
// Returns 0 on success (including nothing-to-activate), 5 if activation failed.
// label is included in the "Activated: …" success message.
// Error counts and messages are written to err.
int ReportActivationOutcome(const ActivationResult& act,
                            OutputFormatter& fmt,
                            const std::string& label,
                            std::ostream& err);

// Report the output of a class run (object run) request.
// Returns 99 if the output starts with "Error:", 0 otherwise.
// In JSON mode the class name and output are emitted via fmt.
// In plain mode normal output is written to out.
int ReportClassRunOutcome(const ClassRunResult& cr,
                          OutputFormatter& fmt,
                          std::ostream& out);

// Print an actionable TABL/DT annotation hint to err_stream when
// source_uri contains "ddic/tables" and err.message contains "can't save".
// No output is produced if either condition is not met.
void PrintTableAnnotationHintIfNeeded(const Error& err,
                                      const std::string& source_uri,
                                      std::ostream& err_stream);

} // namespace erpl_adt
