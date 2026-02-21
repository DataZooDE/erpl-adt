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

void OutputFormatter::PrintDetail(
    const std::string& title,
    const std::vector<DetailSection>& sections) const {

    // Flatten all entries for tree rendering.
    // Sections with a title become sub-trees; sections without title are root-level.
    struct TreeEntry {
        std::string key;
        std::string value;
        bool is_section_header = false;
        bool is_last = false;           // Last in its group
        bool is_child = false;          // Inside a sub-section
        bool is_last_child = false;     // Last in sub-section
    };

    std::vector<TreeEntry> entries;
    for (size_t si = 0; si < sections.size(); ++si) {
        const auto& sec = sections[si];
        if (sec.entries.empty()) continue;

        if (sec.title.empty()) {
            // Root-level entries
            for (const auto& e : sec.entries) {
                entries.push_back({e.first, e.second, false, false, false, false});
            }
        } else {
            // Section header
            entries.push_back({sec.title, "", true, false, false, false});
            for (size_t ei = 0; ei < sec.entries.size(); ++ei) {
                entries.push_back({sec.entries[ei].first, sec.entries[ei].second,
                                   false, false, true,
                                   ei == sec.entries.size() - 1});
            }
        }
    }

    // Mark last root-level entry
    for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
        if (!entries[static_cast<size_t>(i)].is_child) {
            entries[static_cast<size_t>(i)].is_last = true;
            break;
        }
    }

    if (color_mode_) {
        out_ << kBold << title << kReset << "\n";
        for (const auto& e : entries) {
            if (e.is_child) {
                // Child of a section
                out_ << kDim << (e.is_last_child ? "    \u2514\u2500\u2500 " : "    \u251C\u2500\u2500 ") << kReset;
                out_ << e.key << ": " << e.value << "\n";
            } else if (e.is_section_header) {
                out_ << kDim << (e.is_last ? "\u2514\u2500\u2500 " : "\u251C\u2500\u2500 ") << kReset;
                out_ << kBold << e.key << kReset << "\n";
            } else {
                out_ << kDim << (e.is_last ? "\u2514\u2500\u2500 " : "\u251C\u2500\u2500 ") << kReset;
                out_ << e.key << ": " << e.value << "\n";
            }
        }
    } else {
        // Plain mode — same tree structure, no ANSI
        out_ << title << "\n";
        for (const auto& e : entries) {
            if (e.is_child) {
                out_ << (e.is_last_child ? "    +-- " : "    |-- ");
                out_ << e.key << ": " << e.value << "\n";
            } else if (e.is_section_header) {
                out_ << (e.is_last ? "+-- " : "|-- ");
                out_ << e.key << "\n";
            } else {
                out_ << (e.is_last ? "+-- " : "|-- ");
                out_ << e.key << ": " << e.value << "\n";
            }
        }
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
        if (error.hint.has_value() && !error.hint->empty()) {
            err_ << "  " << kYellow << "Hint: " << kReset
                 << error.hint.value() << "\n";
        }
        return;
    }

    // Plain-text multi-line layout (same structure as color path, no ANSI).
    err_ << "Error: " << error.operation;
    if (error.http_status.has_value()) {
        err_ << " (HTTP " << error.http_status.value() << ")";
    }
    err_ << "\n";
    err_ << "  " << error.message << "\n";
    if (error.sap_error.has_value() && !error.sap_error->empty()) {
        err_ << "  SAP: " << error.sap_error.value() << "\n";
    }
    if (error.hint.has_value() && !error.hint->empty()) {
        err_ << "  Hint: " << error.hint.value() << "\n";
    }
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
