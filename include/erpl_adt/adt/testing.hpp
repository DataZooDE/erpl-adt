#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// TestAlert — a single test assertion failure or warning.
// ---------------------------------------------------------------------------
struct TestAlert {
    std::string kind;       // "failedAssertion", etc.
    std::string severity;   // "critical", "warning", etc.
    std::string title;
    std::string detail;
};

// ---------------------------------------------------------------------------
// TestMethodResult — result of a single test method execution.
// ---------------------------------------------------------------------------
struct TestMethodResult {
    std::string name;
    int execution_time_ms = 0;
    std::vector<TestAlert> alerts;

    [[nodiscard]] bool Passed() const { return alerts.empty(); }
};

// ---------------------------------------------------------------------------
// TestClassResult — results for a test class.
// ---------------------------------------------------------------------------
struct TestClassResult {
    std::string name;
    std::string uri;
    std::string risk_level;
    std::string duration_category;
    std::vector<TestMethodResult> methods;
    std::vector<TestAlert> alerts;  // class-level alerts (e.g. risk level exceeded)

    [[nodiscard]] int FailedCount() const {
        int count = 0;
        for (const auto& m : methods) {
            if (!m.Passed()) ++count;
        }
        return count;
    }

    [[nodiscard]] bool Skipped() const {
        return methods.empty() && !alerts.empty();
    }
};

// ---------------------------------------------------------------------------
// TestRunResult — aggregate results from a test run.
// ---------------------------------------------------------------------------
struct TestRunResult {
    std::vector<TestClassResult> classes;

    [[nodiscard]] int TotalMethods() const {
        int count = 0;
        for (const auto& c : classes) {
            count += static_cast<int>(c.methods.size());
        }
        return count;
    }

    [[nodiscard]] int TotalFailed() const {
        int count = 0;
        for (const auto& c : classes) {
            count += c.FailedCount();
        }
        return count;
    }

    [[nodiscard]] bool AllPassed() const { return TotalFailed() == 0; }

    [[nodiscard]] int TotalSkipped() const {
        int count = 0;
        for (const auto& c : classes) {
            if (c.Skipped()) ++count;
        }
        return count;
    }
};

// ---------------------------------------------------------------------------
// RunTests — execute ABAP Unit tests for an object or package.
//
// Endpoint: POST /sap/bc/adt/abapunit/testruns
// The uri can be an object URI or package URI.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<TestRunResult, Error> RunTests(
    IAdtSession& session,
    const std::string& uri);

} // namespace erpl_adt
