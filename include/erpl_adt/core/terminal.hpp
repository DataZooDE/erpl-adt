#pragma once

namespace erpl_adt {

/// Returns true if the given file descriptor is connected to a terminal.
bool IsTerminal(int fd);

/// Returns true if stderr is a terminal (for colored log output).
bool IsStderrTty();

/// Returns true if stdout is a terminal (for colored table/error output).
bool IsStdoutTty();

/// Returns true if stdin is a terminal (for interactive prompts).
bool IsStdinTty();

/// Returns true if the NO_COLOR environment variable is set (https://no-color.org/).
bool NoColorEnvSet();

} // namespace erpl_adt
