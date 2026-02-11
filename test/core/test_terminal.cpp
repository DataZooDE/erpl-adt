#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/terminal.hpp>

using namespace erpl_adt;

// ===========================================================================
// Terminal detection — basic smoke tests.
// ===========================================================================

TEST_CASE("IsTerminal: returns bool without crashing", "[core][terminal]") {
    // stdin (fd 0) — in CI this is typically not a TTY.
    auto result = IsTerminal(0);
    CHECK((result == true || result == false));
}

TEST_CASE("IsStderrTty: returns bool without crashing", "[core][terminal]") {
    auto result = IsStderrTty();
    CHECK((result == true || result == false));
}

TEST_CASE("IsStdoutTty: returns bool without crashing", "[core][terminal]") {
    auto result = IsStdoutTty();
    CHECK((result == true || result == false));
}

TEST_CASE("IsStdinTty: returns bool without crashing", "[core][terminal]") {
    auto result = IsStdinTty();
    CHECK((result == true || result == false));
}

TEST_CASE("NoColorEnvSet: returns bool without crashing", "[core][terminal]") {
    auto result = NoColorEnvSet();
    CHECK((result == true || result == false));
}
