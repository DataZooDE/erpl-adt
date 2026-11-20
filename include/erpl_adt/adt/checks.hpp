#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// AtcFinding — a single ATC check finding.
// ---------------------------------------------------------------------------
struct AtcFinding {
    std::string uri;
    std::string message;
    int priority = 0;          // 1=error, 2=warning, 3=info
    std::string check_title;
    std::string message_title;
};

// ---------------------------------------------------------------------------
// AtcResult — aggregate results from an ATC check run.
// ---------------------------------------------------------------------------
struct AtcResult {
    std::string worklist_id;
    std::string variant;  // the check variant that ran
    std::vector<AtcFinding> findings;

    [[nodiscard]] int ErrorCount() const {
        int count = 0;
        for (const auto& f : findings) {
            if (f.priority == 1) ++count;
        }
        return count;
    }

    [[nodiscard]] int WarningCount() const {
        int count = 0;
        for (const auto& f : findings) {
            if (f.priority == 2) ++count;
        }
        return count;
    }
};

// ---------------------------------------------------------------------------
// RunAtcCheck — execute ATC checks on an object or package.
//
// check_variant: empty → the system check variant (GET /sap/bc/adt/atc/
// customizing, property systemCheckVariant — the one Eclipse runs), or
// DEFAULT when the customizing names none. The variant is then checked to
// exist (GET /sap/bc/adt/atc/checkvariants/{name}): SAP runs other checks
// without a word for a name it does not know, so that is NotFound here.
//
// Workflow:
//   1. POST /sap/bc/adt/atc/worklists?checkVariant={variant} → worklist ID
//   2. POST /sap/bc/adt/atc/runs?worklistId={id} → run (async)
//   3. GET /sap/bc/adt/atc/worklists/{id} → findings
// ---------------------------------------------------------------------------
[[nodiscard]] Result<AtcResult, Error> RunAtcCheck(
    IAdtSession& session,
    const std::string& uri,
    const std::string& check_variant = "");

} // namespace erpl_adt
