#include <erpl_adt/cli/output_formatter.hpp>
#include <erpl_adt/core/ansi.hpp>

#include <algorithm>
#include <iomanip>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>

namespace erpl_adt {

namespace {

// Escape a string for JSON output (minimal — handles \, ", newline, tab).
std::string JsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

using namespace erpl_adt::ansi;

} // anonymous namespace

void OutputFormatter::PrintTable(
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows) const {

    if (json_mode_) {
        out_ << "[";
        for (size_t r = 0; r < rows.size(); ++r) {
            if (r > 0) out_ << ",";
            out_ << "{";
            for (size_t c = 0; c < headers.size() && c < rows[r].size(); ++c) {
                if (c > 0) out_ << ",";
                out_ << "\"" << JsonEscape(headers[c]) << "\":\""
                     << JsonEscape(rows[r][c]) << "\"";
            }
            out_ << "}";
        }
        out_ << "]\n";
        return;
    }

    if (color_mode_) {
        // Build FTXUI table data: header row + data rows.
        std::vector<std::vector<std::string>> table_data;
        table_data.push_back(headers);
        for (const auto& row : rows) {
            table_data.push_back(row);
        }

        auto table = ftxui::Table(table_data);

        // Style the header row.
        table.SelectRow(0).Decorate(ftxui::bold);
        table.SelectRow(0).SeparatorVertical(ftxui::LIGHT);

        // Add a light separator after the header.
        table.SelectRow(0).BorderBottom(ftxui::LIGHT);

        // Render to string.
        auto element = table.Render();
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element));
        ftxui::Render(screen, element);
        out_ << screen.ToString() << "\n";
        return;
    }

    // Plain human-readable table: compute column widths.
    std::vector<size_t> widths(headers.size(), 0);
    for (size_t c = 0; c < headers.size(); ++c) {
        widths[c] = headers[c].size();
    }
    for (const auto& row : rows) {
        for (size_t c = 0; c < headers.size() && c < row.size(); ++c) {
            widths[c] = std::max(widths[c], row[c].size());
        }
    }

    // Print header.
    for (size_t c = 0; c < headers.size(); ++c) {
        if (c > 0) out_ << "  ";
        out_ << std::left << std::setw(static_cast<int>(widths[c]))
             << headers[c];
    }
    out_ << "\n";

    // Print separator.
    for (size_t c = 0; c < headers.size(); ++c) {
        if (c > 0) out_ << "  ";
        out_ << std::string(widths[c], '-');
    }
    out_ << "\n";

    // Print rows.
    for (const auto& row : rows) {
        for (size_t c = 0; c < headers.size() && c < row.size(); ++c) {
            if (c > 0) out_ << "  ";
            out_ << std::left << std::setw(static_cast<int>(widths[c]))
                 << row[c];
        }
        out_ << "\n";
    }
}

void OutputFormatter::PrintJson(const std::string& json) const {
    out_ << json << "\n";
}

void OutputFormatter::PrintError(const Error& error) const {
    if (json_mode_) {
        err_ << error.ToJson() << "\n";
        return;
    }

    if (color_mode_) {
        err_ << kRed << "Error: " << kReset;
        err_ << kBold << error.operation << kReset;
        if (error.http_status.has_value()) {
            err_ << kDim << " (HTTP " << error.http_status.value() << ")" << kReset;
        }
        err_ << "\n";
        err_ << "  " << error.message << "\n";
        if (error.sap_error.has_value() && !error.sap_error->empty()) {
            err_ << "  " << kDim << "SAP: " << kReset
                 << error.sap_error.value() << "\n";
        }
        return;
    }

    err_ << "Error: " << error.ToString() << "\n";
}

void OutputFormatter::PrintSuccess(const std::string& message) const {
    if (json_mode_) {
        out_ << "{\"success\":true,\"message\":\"" << JsonEscape(message) << "\"}\n";
        return;
    }

    if (color_mode_) {
        out_ << kGreen << "OK" << kReset << " " << message << "\n";
        return;
    }

    out_ << message << "\n";
}

} // namespace erpl_adt
