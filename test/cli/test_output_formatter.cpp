#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/output_formatter.hpp>

#include <sstream>
#include <string>

using namespace erpl_adt;

// ===========================================================================
// PrintTable — human-readable
// ===========================================================================

TEST_CASE("OutputFormatter: table with headers and rows", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, false, out, err);

    std::vector<std::string> headers = {"Name", "Type", "Package"};
    std::vector<std::vector<std::string>> rows = {
        {"ZCL_TEST", "CLAS/OC", "ZTEST"},
        {"ZPROGRAM", "PROG/P", "ZDEV"},
    };

    fmt.PrintTable(headers, rows);

    auto output = out.str();
    CHECK(output.find("Name") != std::string::npos);
    CHECK(output.find("Type") != std::string::npos);
    CHECK(output.find("Package") != std::string::npos);
    CHECK(output.find("ZCL_TEST") != std::string::npos);
    CHECK(output.find("ZPROGRAM") != std::string::npos);
    // Should have separator line with dashes.
    CHECK(output.find("---") != std::string::npos);
}

TEST_CASE("OutputFormatter: table with empty rows", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, false, out, err);

    fmt.PrintTable({"A", "B"}, {});

    auto output = out.str();
    CHECK(output.find("A") != std::string::npos);
    CHECK(output.find("B") != std::string::npos);
    // Just header + separator, no data rows.
}

// ===========================================================================
// PrintTable — JSON mode
// ===========================================================================

TEST_CASE("OutputFormatter: table in JSON mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(true, false, out, err);

    std::vector<std::string> headers = {"name", "type"};
    std::vector<std::vector<std::string>> rows = {
        {"ZCL_TEST", "CLAS/OC"},
    };

    fmt.PrintTable(headers, rows);

    auto output = out.str();
    CHECK(output.find("[{") != std::string::npos);
    CHECK(output.find("\"name\":\"ZCL_TEST\"") != std::string::npos);
    CHECK(output.find("\"type\":\"CLAS/OC\"") != std::string::npos);
}

TEST_CASE("OutputFormatter: table JSON empty rows", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(true, false, out, err);

    fmt.PrintTable({"a"}, {});

    CHECK(out.str() == "[]\n");
}

// ===========================================================================
// PrintJson
// ===========================================================================

TEST_CASE("OutputFormatter: PrintJson", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, false, out, err);

    fmt.PrintJson(R"({"key":"value"})");

    CHECK(out.str() == "{\"key\":\"value\"}\n");
}

// ===========================================================================
// PrintError
// ===========================================================================

TEST_CASE("OutputFormatter: PrintError human mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, false, out, err);

    Error e{"Search", "/sap/bc/adt/search", 404, "Not found", std::nullopt};
    fmt.PrintError(e);

    auto error_output = err.str();
    CHECK(error_output.find("Error:") != std::string::npos);
    CHECK(error_output.find("Search") != std::string::npos);
    CHECK(error_output.find("Not found") != std::string::npos);
    CHECK(out.str().empty());
}

TEST_CASE("OutputFormatter: PrintError JSON mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(true, false, out, err);

    Error e{"Search", "/sap/bc/adt/search", 404, "Not found", std::nullopt};
    fmt.PrintError(e);

    auto error_output = err.str();
    CHECK(error_output.find("\"category\"") != std::string::npos);
    CHECK(error_output.find("\"operation\":\"Search\"") != std::string::npos);
    CHECK(out.str().empty());
}

// ===========================================================================
// PrintSuccess
// ===========================================================================

TEST_CASE("OutputFormatter: PrintSuccess human mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, false, out, err);

    fmt.PrintSuccess("Operation completed");

    CHECK(out.str() == "Operation completed\n");
}

TEST_CASE("OutputFormatter: PrintSuccess JSON mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(true, false, out, err);

    fmt.PrintSuccess("Done");

    auto output = out.str();
    CHECK(output.find("\"success\":true") != std::string::npos);
    CHECK(output.find("\"message\":\"Done\"") != std::string::npos);
}

// ===========================================================================
// IsJsonMode
// ===========================================================================

TEST_CASE("OutputFormatter: IsJsonMode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;

    OutputFormatter human(false, false, out, err);
    CHECK_FALSE(human.IsJsonMode());

    OutputFormatter json(true, false, out, err);
    CHECK(json.IsJsonMode());
}

// ===========================================================================
// Color mode — PrintTable
// ===========================================================================

TEST_CASE("OutputFormatter: color table uses FTXUI box-drawing", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, true, out, err);

    std::vector<std::string> headers = {"Name", "Type"};
    std::vector<std::vector<std::string>> rows = {
        {"ZCL_TEST", "CLAS/OC"},
    };

    fmt.PrintTable(headers, rows);

    auto output = out.str();
    CHECK(output.find("Name") != std::string::npos);
    CHECK(output.find("ZCL_TEST") != std::string::npos);
    // FTXUI tables use Unicode box-drawing characters.
    // Check for common box-drawing elements (thin lines).
    CHECK(output.find("\xe2\x94") != std::string::npos); // UTF-8 prefix for box-drawing
}

TEST_CASE("OutputFormatter: color table empty rows still renders", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, true, out, err);

    fmt.PrintTable({"A", "B"}, {});

    auto output = out.str();
    CHECK(output.find("A") != std::string::npos);
    CHECK(output.find("B") != std::string::npos);
}

// ===========================================================================
// Color mode — PrintError
// ===========================================================================

TEST_CASE("OutputFormatter: color error has red ANSI codes", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, true, out, err);

    Error e{"Search", "/sap/bc/adt/search", 404, "Not found", std::nullopt};
    fmt.PrintError(e);

    auto error_output = err.str();
    // Should contain red ANSI code.
    CHECK(error_output.find("\033[1;31m") != std::string::npos);
    CHECK(error_output.find("Error:") != std::string::npos);
    CHECK(error_output.find("Not found") != std::string::npos);
    CHECK(error_output.find("HTTP 404") != std::string::npos);
}

TEST_CASE("OutputFormatter: color error with SAP error shows it", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, true, out, err);

    Error e{"Write", "/sap/bc/adt/source", 400, "Bad request",
            std::string("ABAP syntax error in line 42")};
    fmt.PrintError(e);

    auto error_output = err.str();
    CHECK(error_output.find("SAP:") != std::string::npos);
    CHECK(error_output.find("ABAP syntax error in line 42") != std::string::npos);
}

// ===========================================================================
// Color mode — PrintSuccess
// ===========================================================================

TEST_CASE("OutputFormatter: color success has green ANSI codes", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(false, true, out, err);

    fmt.PrintSuccess("Object created");

    auto output = out.str();
    // Should contain green ANSI code.
    CHECK(output.find("\033[1;32m") != std::string::npos);
    CHECK(output.find("OK") != std::string::npos);
    CHECK(output.find("Object created") != std::string::npos);
}

// ===========================================================================
// Color mode — JSON wins over color
// ===========================================================================

TEST_CASE("OutputFormatter: JSON mode overrides color mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    // Both json and color requested — json should win.
    OutputFormatter fmt(true, true, out, err);

    CHECK(fmt.IsJsonMode());
    CHECK_FALSE(fmt.IsColorMode());

    fmt.PrintSuccess("Done");
    auto output = out.str();
    // No ANSI codes in output.
    CHECK(output.find("\033[") == std::string::npos);
    CHECK(output.find("\"success\":true") != std::string::npos);
}

TEST_CASE("OutputFormatter: JSON table unaffected by color mode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(true, true, out, err);

    fmt.PrintTable({"name"}, {{"ZCL_TEST"}});

    auto output = out.str();
    CHECK(output.find("\033[") == std::string::npos);
    CHECK(output.find("\"name\":\"ZCL_TEST\"") != std::string::npos);
}

// ===========================================================================
// IsColorMode
// ===========================================================================

TEST_CASE("OutputFormatter: IsColorMode", "[cli][formatter]") {
    std::ostringstream out;
    std::ostringstream err;

    OutputFormatter plain(false, false, out, err);
    CHECK_FALSE(plain.IsColorMode());

    OutputFormatter colored(false, true, out, err);
    CHECK(colored.IsColorMode());

    // JSON overrides color.
    OutputFormatter json_color(true, true, out, err);
    CHECK_FALSE(json_color.IsColorMode());
}
