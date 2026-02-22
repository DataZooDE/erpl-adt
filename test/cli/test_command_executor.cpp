#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/command_router.hpp>

#include <iostream>
#include <sstream>
#include <set>
#include <string>

using namespace erpl_adt;

namespace {

struct DispatchResult {
    int exit_code{0};
    std::string stderr_text;
    std::string stdout_text;
};

DispatchResult DispatchWithStderrCapture(CommandRouter& router,
                                         int argc,
                                         const char* const argv[]) {
    std::ostringstream err;
    auto* old = std::cerr.rdbuf(err.rdbuf());
    const int code = router.Dispatch(argc, argv);
    std::cerr.rdbuf(old);
    return DispatchResult{code, err.str(), {}};
}

DispatchResult DispatchCaptureBoth(CommandRouter& router,
                                   int argc,
                                   const char* const argv[]) {
    std::ostringstream err;
    std::ostringstream out;
    auto* old_err = std::cerr.rdbuf(err.rdbuf());
    auto* old_out = std::cout.rdbuf(out.rdbuf());
    const int code = router.Dispatch(argc, argv);
    std::cerr.rdbuf(old_err);
    std::cout.rdbuf(old_out);
    return DispatchResult{code, err.str(), out.str()};
}

}  // namespace

// ===========================================================================
// IsNewStyleCommand
// ===========================================================================

TEST_CASE("IsNewStyleCommand: search group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "search", "query", "CL_*"};
    CHECK(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: object group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "object", "read", "/sap/bc/adt/foo"};
    CHECK(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: flags before group are skipped",
          "[cli][executor]") {
    // --json is a boolean flag and does not consume the next arg.
    const char* argv[] = {"erpl-adt", "--host", "myhost", "--json", "search", "query", "X"};
    CHECK(IsNewStyleCommand(7, argv));
}

TEST_CASE("IsNewStyleCommand: legacy deploy is NOT new-style",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "deploy", "--config", "x.yaml"};
    CHECK_FALSE(IsNewStyleCommand(4, argv));
}

TEST_CASE("IsNewStyleCommand: no args returns false",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt"};
    CHECK_FALSE(IsNewStyleCommand(1, argv));
}

TEST_CASE("IsNewStyleCommand: flag-only args returns false",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "--version"};
    CHECK_FALSE(IsNewStyleCommand(2, argv));
}

TEST_CASE("IsNewStyleCommand: discover group recognized",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "--host", "x", "discover", "services"};
    CHECK(IsNewStyleCommand(5, argv));
}

// ===========================================================================
// RegisterAllCommands populates router
// ===========================================================================

TEST_CASE("RegisterAllCommands registers all expected groups",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto groups = router.Groups();

    CHECK(groups.size() == 11);

    // Verify all groups present (sorted).
    std::set<std::string> expected = {
        "activate", "bw", "check", "ddic", "discover", "object", "package",
        "search", "source", "test", "transport"
    };
    std::set<std::string> actual(groups.begin(), groups.end());
    CHECK(actual == expected);
}

TEST_CASE("RegisterAllCommands: object group has 6 actions",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto cmds = router.CommandsForGroup("object");
    CHECK(cmds.size() == 6);
}

// ===========================================================================
// Validation: missing positional args return exit code 99
// ===========================================================================

TEST_CASE("search query: missing pattern returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // No positional arg after "search query"
    const char* argv[] = {"erpl-adt", "search", "query"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("search short form: missing pattern returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // "erpl-adt search" with no pattern → group help (exit 0), not handler
    // This shows group help because Parse returns "Missing action" error.
    const char* argv[] = {"erpl-adt", "search"};
    CHECK(router.Dispatch(2, argv) == 0);
}

TEST_CASE("package group has 3 actions (list, tree, exists)",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    auto cmds = router.CommandsForGroup("package");
    CHECK(cmds.size() == 3);
}

TEST_CASE("package tree: missing name returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "package", "tree"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("object lock: missing URI returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "lock"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("source write: missing --file returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "write",
                          "/sap/bc/adt/oo/classes/foo/source/main",
                          "--handle", "abc123"};
    // Has URI and handle but no --file → 99
    CHECK(router.Dispatch(6, argv) == 99);
}

TEST_CASE("object delete: invalid URI returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "delete", "not-a-uri"};
    CHECK(router.Dispatch(4, argv) == 99);
}

TEST_CASE("transport create: missing --desc returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "transport", "create",
                          "--package", "ZTEST"};
    CHECK(router.Dispatch(5, argv) == 99);
}

TEST_CASE("object create: missing --type returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "create",
                          "--name", "ZCL_FOO", "--package", "ZTEST"};
    CHECK(router.Dispatch(7, argv) == 99);
}

TEST_CASE("bw read-query: invalid component type fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "foo", "ZQ_TEST"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Unsupported query component type") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --format fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--format=dot"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --format") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --version fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--version=x"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --version") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --layout fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--layout=wide"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --layout") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --direction fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--direction=BT"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --direction") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --focus-role fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--focus-role=everything"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --focus-role") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --max-nodes-per-role fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--max-nodes-per-role=0"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --max-nodes-per-role") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --json-shape fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--json-shape=flat"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --json-shape") != std::string::npos);
}

TEST_CASE("bw read-query: --upstream-dtp requires query component type",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "variable", "ZVAR_FY", "--upstream-dtp=DTP_ZSALES"};
    const auto result = DispatchWithStderrCapture(router, 6, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--upstream-dtp is only supported") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --upstream mode fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--upstream=smart"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --upstream") != std::string::npos);
}

TEST_CASE("bw read-query: --upstream=auto requires query component type",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "variable", "ZVAR_FY", "--upstream=auto"};
    const auto result = DispatchWithStderrCapture(router, 6, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("--upstream=auto is only supported") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --upstream-max-xref fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--upstream-max-xref=0"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --upstream-max-xref") != std::string::npos);
}

TEST_CASE("bw read-query: invalid --lineage-max-steps fails before session setup",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "ZQ_TEST", "--lineage-max-steps=0"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --lineage-max-steps") != std::string::npos);
}

TEST_CASE("bw read-query: too many positional args fails with usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "read-query", "query", "ZQ_TEST", "EXTRA"};
    const auto result = DispatchWithStderrCapture(router, 6, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Too many arguments") != std::string::npos);
}

// ===========================================================================
// source read — pre-session validation
// ===========================================================================

TEST_CASE("source read: missing arg returns 99",
          "[cli][executor][source]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read"};
    CHECK(router.Dispatch(3, argv) == 99);
}

TEST_CASE("source read: invalid --section value returns 99",
          "[cli][executor][source]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--section=bogus"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Invalid --section") != std::string::npos);
}

TEST_CASE("source read: valid --section values are accepted past validation",
          "[cli][executor][source]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // These should NOT return 99 for the section validation check.
    // They will fail later at session setup (exit 99 for missing credentials)
    // but we verify the section name itself is accepted by checking the
    // error message does NOT mention "Invalid --section".
    for (const auto* sec : {"main", "localdefinitions", "localimplementations",
                             "testclasses", "all"}) {
        std::string section_flag = std::string("--section=") + sec;
        const char* argv[] = {"erpl-adt", "source", "read",
                               "/sap/bc/adt/oo/classes/zcl_test/source/main",
                               section_flag.c_str()};
        const auto result = DispatchWithStderrCapture(router, 5, argv);
        // Must not complain about the section value itself.
        CHECK(result.stderr_text.find("Invalid --section") == std::string::npos);
    }
}

// ===========================================================================
// source read --editor and --color — flag parsing tests
// ===========================================================================

TEST_CASE("source read: --editor is a boolean flag (does not consume next arg)",
          "[cli][executor][source]") {
    // If --editor consumed "ZCL_MY_CLASS" as its value the positional would be
    // empty and we'd get exit 99 with "Missing source URI".
    // But with section validation passing (valid URI provided as first positional)
    // the handler must get past section validation, proving the flag parsed correctly.
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--editor"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    // Should NOT complain about missing URI or invalid section — those validations
    // pass. It will fail at session creation (no SAP system), but that's fine.
    CHECK(result.stderr_text.find("Missing source URI") == std::string::npos);
    CHECK(result.stderr_text.find("Invalid --section") == std::string::npos);
}

TEST_CASE("source read: --color is a boolean flag (does not consume next arg)",
          "[cli][executor][source]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--color"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.stderr_text.find("Missing source URI") == std::string::npos);
    CHECK(result.stderr_text.find("Invalid --section") == std::string::npos);
}

TEST_CASE("source read: --no-color is a boolean flag (does not consume next arg)",
          "[cli][executor][source]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--no-color"};
    const auto result = DispatchWithStderrCapture(router, 5, argv);
    CHECK(result.stderr_text.find("Missing source URI") == std::string::npos);
    CHECK(result.stderr_text.find("Invalid --section") == std::string::npos);
}

// ===========================================================================
// --section all routing (liy)
// ===========================================================================

TEST_CASE("source read: --section all is a valid section value (liy)",
          "[cli][executor][source]") {
    // Validates that --section all dispatches correctly without a "Invalid --section" error.
    // Error propagation of non-NotFound secondary-section failures is verified by integration tests
    // (test_06_source.py) since it requires a live mock session.
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "read",
                          "/sap/bc/adt/oo/classes/zcl_test/source/main",
                          "--section", "all"};
    const auto result = DispatchWithStderrCapture(router, 6, argv);
    CHECK(result.stderr_text.find("Invalid --section") == std::string::npos);
}

// ===========================================================================
// IsNewStyleCommand: --no-xref-edges is a boolean flag (5lu)
// ===========================================================================

TEST_CASE("IsNewStyleCommand: --no-xref-edges does not consume positional arg",
          "[cli][executor]") {
    const char* argv[] = {"erpl-adt", "--no-xref-edges", "bw", "export-query"};
    // --no-xref-edges is a global boolean flag — must not consume "bw" as its value
    CHECK(IsNewStyleCommand(4, argv));
}

// ===========================================================================
// bw export-area usage string (y20)
// ===========================================================================

TEST_CASE("bw export-area: missing infoarea prints export-area usage hint",
          "[cli][executor][bw]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "bw", "export-area"};
    const auto result = DispatchWithStderrCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    // Error must reference "export-area", not the stale "export".
    CHECK(result.stderr_text.find("export-area") != std::string::npos);
}

// ===========================================================================
// activate: missing positional arg (y21)
// ===========================================================================

TEST_CASE("activate run: missing object name returns 99",
          "[cli][executor]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "activate", "run"};
    const auto result = DispatchWithStderrCapture(router, 3, argv);
    CHECK(result.exit_code == 99);
    CHECK(result.stderr_text.find("Missing object") != std::string::npos);
}

// ===========================================================================
// Help text: object create mentions TABL/DT annotations (Task 5)
// ===========================================================================

TEST_CASE("object create --help mentions TABL/DT annotation requirement",
          "[cli][executor][help]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "object", "create", "--help"};
    const auto result = DispatchCaptureBoth(router, 4, argv);
    CHECK(result.exit_code == 0);
    const auto& combined = result.stdout_text + result.stderr_text;
    CHECK(combined.find("TABL/DT") != std::string::npos);
}

// ===========================================================================
// Help text: source write mentions TABL/DT and mandt rule (Task 5)
// ===========================================================================

TEST_CASE("source write --help mentions TABL/DT annotations and ABAP Cloud mandt rule",
          "[cli][executor][help]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "write", "--help"};
    const auto result = DispatchCaptureBoth(router, 4, argv);
    CHECK(result.exit_code == 0);
    const auto& combined = result.stdout_text + result.stderr_text;
    CHECK(combined.find("TABL/DT") != std::string::npos);
    CHECK(combined.find("mandt") != std::string::npos);
}

// ===========================================================================
// Outcome reporters
// ===========================================================================

// ---------------------------------------------------------------------------
// ReportActivationOutcome
// ---------------------------------------------------------------------------

TEST_CASE("ReportActivationOutcome: total==0 prints nothing-to-activate and returns 0",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/false, /*color=*/false, out, err);
    ActivationResult act;
    act.total = 0;
    const int code = ReportActivationOutcome(act, fmt, "ZCL_FOO", err);
    CHECK(code == 0);
    CHECK(out.str().find("Nothing to activate") != std::string::npos);
    CHECK(err.str().empty());
}

TEST_CASE("ReportActivationOutcome: failed>0 writes error counts to err and returns 5",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/false, /*color=*/false, out, err);
    ActivationResult act;
    act.total = 1;
    act.failed = 1;
    act.error_messages = {"Syntax error in line 42"};
    const int code = ReportActivationOutcome(act, fmt, "ZCL_FOO", err);
    CHECK(code == 5);
    CHECK(err.str().find("1 error") != std::string::npos);
    CHECK(err.str().find("Syntax error in line 42") != std::string::npos);
}

TEST_CASE("ReportActivationOutcome: success prints Activated label and returns 0",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/false, /*color=*/false, out, err);
    ActivationResult act;
    act.total = 1;
    act.activated = 1;
    act.failed = 0;
    const int code = ReportActivationOutcome(act, fmt, "/sap/bc/adt/oo/classes/zcl_foo", err);
    CHECK(code == 0);
    CHECK(out.str().find("Activated") != std::string::npos);
    CHECK(out.str().find("zcl_foo") != std::string::npos);
    CHECK(err.str().empty());
}

// ---------------------------------------------------------------------------
// ReportClassRunOutcome
// ---------------------------------------------------------------------------

TEST_CASE("ReportClassRunOutcome: Error: prefix causes PrintError and returns 99",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/false, /*color=*/false, out, err);
    ClassRunResult cr;
    cr.class_name = "ZCL_MY_CLASS";
    cr.output = "Error: Something went wrong";
    const int code = ReportClassRunOutcome(cr, fmt, out);
    CHECK(code == 99);
    CHECK(out.str().empty());
    CHECK(err.str().find("Something went wrong") != std::string::npos);
}

TEST_CASE("ReportClassRunOutcome: normal output is written to out stream and returns 0",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/false, /*color=*/false, out, err);
    ClassRunResult cr;
    cr.class_name = "ZCL_MY_CLASS";
    cr.output = "Hello from ABAP console\n";
    const int code = ReportClassRunOutcome(cr, fmt, out);
    CHECK(code == 0);
    CHECK(out.str() == "Hello from ABAP console\n");
    CHECK(err.str().empty());
}

TEST_CASE("ReportClassRunOutcome: JSON mode emits JSON and returns 0",
          "[cli][executor][outcome]") {
    std::ostringstream out;
    std::ostringstream err;
    OutputFormatter fmt(/*json=*/true, /*color=*/false, out, err);
    ClassRunResult cr;
    cr.class_name = "ZCL_MY_CLASS";
    cr.output = "Error: this is ignored in JSON mode";
    const int code = ReportClassRunOutcome(cr, fmt, out);
    CHECK(code == 0);
    // JSON output must contain both the class name and output fields.
    const auto& text = out.str();
    CHECK(text.find("ZCL_MY_CLASS") != std::string::npos);
    CHECK(text.find("Error: this is ignored in JSON mode") != std::string::npos);
}

// ---------------------------------------------------------------------------
// PrintTableAnnotationHintIfNeeded
// ---------------------------------------------------------------------------

TEST_CASE("PrintTableAnnotationHintIfNeeded: ddic/tables URI + can't save → hint printed",
          "[cli][executor][outcome]") {
    std::ostringstream err;
    Error e;
    e.message = "Can't save due to errors in source; execute check for details";
    PrintTableAnnotationHintIfNeeded(e, "/sap/bc/adt/ddic/tables/ztbl_foo/source/main", err);
    CHECK(err.str().find("@AbapCatalog.tableCategory") != std::string::npos);
    CHECK(err.str().find("TABL/DT") != std::string::npos);
}

TEST_CASE("PrintTableAnnotationHintIfNeeded: non-table URI → no hint printed",
          "[cli][executor][outcome]") {
    std::ostringstream err;
    Error e;
    e.message = "Can't save due to errors in source";
    PrintTableAnnotationHintIfNeeded(e, "/sap/bc/adt/oo/classes/zcl_foo/source/main", err);
    CHECK(err.str().empty());
}

TEST_CASE("PrintTableAnnotationHintIfNeeded: ddic/tables URI + other error → no hint printed",
          "[cli][executor][outcome]") {
    std::ostringstream err;
    Error e;
    e.message = "Object not found";
    PrintTableAnnotationHintIfNeeded(e, "/sap/bc/adt/ddic/tables/ztbl_foo/source/main", err);
    CHECK(err.str().empty());
}

// ===========================================================================
// Name-or-URI support: object read, source write, source check
// ===========================================================================

TEST_CASE("object read: plain name fails at session (not URI parse)",
          "[cli][executor][name-resolution]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Pass a plain name with --host invalid.local — connection will fail,
    // but the error must NOT be "Invalid URI".
    const char* argv[] = {"erpl-adt", "--host", "invalid.local",
                          "object", "read", "ZCL_TEST"};
    std::ostringstream err;
    auto* old_err = std::cerr.rdbuf(err.rdbuf());
    const int code = router.Dispatch(6, argv);
    std::cerr.rdbuf(old_err);
    CHECK(code != 0);
    CHECK(err.str().find("Invalid URI") == std::string::npos);
}

TEST_CASE("source write: plain name fails at session (not /source/ segment error)",
          "[cli][executor][name-resolution]") {
    CommandRouter router;
    RegisterAllCommands(router);
    // Need a real (but missing) file so we get past file-read validation.
    // Use /dev/null as a zero-byte "file".
    const char* argv[] = {"erpl-adt", "--host", "invalid.local",
                          "source", "write", "ZCL_TEST",
                          "--file", "/dev/null"};
    std::ostringstream err;
    auto* old_err = std::cerr.rdbuf(err.rdbuf());
    const int code = router.Dispatch(8, argv);
    std::cerr.rdbuf(old_err);
    CHECK(code != 0);
    CHECK(err.str().find("expected /source/ segment") == std::string::npos);
}

TEST_CASE("source check: plain name fails at session (not Invalid URI error)",
          "[cli][executor][name-resolution]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "--host", "invalid.local",
                          "source", "check", "ZCL_TEST"};
    std::ostringstream err;
    auto* old_err = std::cerr.rdbuf(err.rdbuf());
    const int code = router.Dispatch(6, argv);
    std::cerr.rdbuf(old_err);
    CHECK(code != 0);
    CHECK(err.str().find("Invalid URI") == std::string::npos);
}

TEST_CASE("source write: --section invalid value returns 99 before session",
          "[cli][executor][name-resolution]") {
    CommandRouter router;
    RegisterAllCommands(router);
    const char* argv[] = {"erpl-adt", "source", "write", "ZCL_TEST",
                          "--file", "/dev/null", "--section", "badvalue"};
    CHECK(router.Dispatch(8, argv) == 99);
}
