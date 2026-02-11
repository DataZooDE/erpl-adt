#pragma once

#include <erpl_adt/core/result.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// OutputFormatter — handles human-readable and JSON output for CLI commands.
//
// Constructor takes booleans for json mode and color mode, plus optional
// ostream references. All output methods write to the configured streams.
// When color_mode is true and json_mode is false, uses FTXUI tables and
// ANSI escape codes for richer terminal output.
// ---------------------------------------------------------------------------
class OutputFormatter {
public:
    explicit OutputFormatter(bool json_mode, bool color_mode = false,
                             std::ostream& out = std::cout,
                             std::ostream& err = std::cerr)
        : json_mode_(json_mode), color_mode_(color_mode && !json_mode),
          out_(out), err_(err) {}

    [[nodiscard]] bool IsJsonMode() const noexcept { return json_mode_; }
    [[nodiscard]] bool IsColorMode() const noexcept { return color_mode_; }

    // Print a table with headers and rows (human-readable mode).
    // In JSON mode, outputs a JSON array of objects.
    // In color mode, uses FTXUI table rendering.
    void PrintTable(const std::vector<std::string>& headers,
                    const std::vector<std::vector<std::string>>& rows) const;

    // Print a raw JSON string to stdout.
    void PrintJson(const std::string& json) const;

    // Print an error to stderr.
    void PrintError(const Error& error) const;

    // Print a success message to stdout (human mode) or JSON (json mode).
    void PrintSuccess(const std::string& message) const;

private:
    bool json_mode_;
    bool color_mode_;
    std::ostream& out_;
    std::ostream& err_;
};

} // namespace erpl_adt
