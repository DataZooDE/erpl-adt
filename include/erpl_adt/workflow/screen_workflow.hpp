#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/adt/screen.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Screen workflow — orchestrates the disposable-helper-class dance that is
// the only genuine backend path for classic Dynpro CRUD (see adt/screen.hpp
// for why: ADT has no REST collection for classic screens). Each call:
//   1. Ensures a disposable IF_OO_ADT_CLASSRUN helper class exists (creating
//      it in `target.package` if missing).
//   2. Writes + activates the generated ABAP source for the requested
//      operation and runs it via classrun.
//   3. Parses the console output into a Result — a non-zero ABAP return code
//      is always an Err, never read as success.
//   4. Deletes the helper class again unless `target.keep_helper` is set,
//      so a caller never accumulates orphaned helper classes across calls.
// Cleanup happens on both the success and failure path.
// ---------------------------------------------------------------------------

struct ScreenWorkflowTarget {
    std::string program;
    std::string screen;
    std::string package;       // e.g. "$TMP" — where the helper class lives
    std::string helper_class;  // disposable IF_OO_ADT_CLASSRUN class name
    bool keep_helper = false;  // skip deleting the helper class afterwards
};

struct ScreenCreateWorkflowParams {
    ScreenWorkflowTarget target;
    std::string description;
    std::string next_screen;  // empty means "self-loop" (same as `screen`)
    int lines = 24;
    int columns = 83;
    std::vector<ScreenFieldSpec> fields;
};

// `classrun_session` must be a genuinely different IAdtSession/connection
// than `session` — confirmed live that reusing the very session which wrote
// and activated the disposable helper class to then run it can see SAP's
// generated-program buffer as stale ("Class does not implement
// if_oo_adt_classrun~main method!") for 30+ seconds, while a fresh session
// succeeds immediately. Callers (e.g. the CLI) should construct a second,
// independently-authenticated session for this parameter.
[[nodiscard]] Result<void, Error> RunScreenCreateWorkflow(
    IAdtSession& session, IAdtSession& classrun_session,
    const ScreenCreateWorkflowParams& params);

[[nodiscard]] Result<ScreenReadResult, Error> RunScreenReadWorkflow(
    IAdtSession& session, IAdtSession& classrun_session,
    const ScreenWorkflowTarget& target);

[[nodiscard]] Result<void, Error> RunScreenDeleteWorkflow(
    IAdtSession& session, IAdtSession& classrun_session,
    const ScreenWorkflowTarget& target);

}  // namespace erpl_adt
