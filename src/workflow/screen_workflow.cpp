#include <erpl_adt/workflow/screen_workflow.hpp>

#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/classrun.hpp>
#include <erpl_adt/adt/object.hpp>
#include <erpl_adt/adt/object_exists.hpp>
#include <erpl_adt/core/log.hpp>
#include <erpl_adt/core/types.hpp>
#include <erpl_adt/workflow/lock_workflow.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace erpl_adt {

namespace {

constexpr const char* kComponent = "screen_workflow";

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string HelperClassUri(const std::string& helper_class) {
    return "/sap/bc/adt/oo/classes/" + ToLowerCopy(helper_class);
}

// Creates the disposable helper class in `package` if it does not already
// exist. Any error other than "not found" (e.g. a connection problem) is
// propagated rather than papered over.
Result<void, Error> EnsureHelperClass(IAdtSession& session,
                                      const std::string& helper_class,
                                      const std::string& package) {
    const std::string uri = HelperClassUri(helper_class);
    auto exists = EnsureObjectExists(session, uri, "ScreenWorkflow",
                                     "Helper class " + helper_class);
    if (exists.IsOk()) {
        return Result<void, Error>::Ok();
    }
    if (exists.Error().category != ErrorCategory::NotFound) {
        return Result<void, Error>::Err(exists.Error());
    }

    CreateObjectParams params;
    params.object_type = "CLAS/OC";
    params.name = helper_class;
    params.package_name = package;
    params.description = "Disposable erpl-adt screen helper (safe to delete)";

    auto created = CreateObject(session, params);
    if (created.IsErr()) {
        return Result<void, Error>::Err(std::move(created).Error());
    }
    return Result<void, Error>::Ok();
}

// Writes `source` to the helper class, activates it, runs it via classrun,
// and returns the raw console output.
//
// RunClass is issued against `classrun_session` — a *different* IAdtSession
// than the one used for write/activate — rather than retried on the same
// connection. Confirmed live: a classrun POST reusing the very session/
// connection that just wrote and activated the class can answer 200 with
// "Class does not implement if_oo_adt_classrun~main method!" for 30+ seconds
// of retries on that same session, while a brand new session/connection
// (e.g. a separate erpl-adt invocation) succeeds immediately — SAP's
// generated-program buffer for that class appears stale specifically on the
// dispatcher work process pinned to the stateful session that just wrote it,
// not stale in general. A few short retries remain as a safety net in case
// the fresh session still lands on the same pinned worker.
Result<std::string, Error> WriteActivateRun(IAdtSession& session,
                                            IAdtSession& classrun_session,
                                            const std::string& helper_class,
                                            const std::string& source) {
    const std::string object_uri = HelperClassUri(helper_class);
    const std::string source_uri = object_uri + "/source/main";

    auto write_result = WriteSourceWithAutoLock(session, source_uri, source, std::nullopt);
    if (write_result.IsErr()) {
        return Result<std::string, Error>::Err(std::move(write_result).Error());
    }

    ActivateObjectParams act_params;
    act_params.uri = write_result.Value();
    act_params.type = "CLAS/OC";
    act_params.name = helper_class;

    auto act_result = ActivateObject(session, act_params);
    if (act_result.IsErr()) {
        return Result<std::string, Error>::Err(std::move(act_result).Error());
    }
    if (act_result.Value().failed > 0) {
        Error err;
        err.operation = "ScreenWorkflow";
        err.endpoint = object_uri;
        err.category = ErrorCategory::ActivationError;
        err.message = "Activation of the disposable screen helper class failed";
        if (!act_result.Value().error_messages.empty()) {
            err.message += ": " + act_result.Value().error_messages.front();
        }
        return Result<std::string, Error>::Err(err);
    }

    constexpr int kMaxClassrunAttempts = 5;
    for (int attempt = 1; attempt <= kMaxClassrunAttempts; ++attempt) {
        auto run_result = RunClass(classrun_session, helper_class);
        if (run_result.IsErr()) {
            return Result<std::string, Error>::Err(std::move(run_result).Error());
        }
        const std::string& output = run_result.Value().output;
        const bool transient_not_ready =
            output.find("does not implement if_oo_adt_classrun") != std::string::npos;
        if (!transient_not_ready || attempt == kMaxClassrunAttempts) {
            return Result<std::string, Error>::Ok(output);
        }
        LogWarn(kComponent,
               "classrun not yet ready after activation (attempt " +
                   std::to_string(attempt) + "/" + std::to_string(kMaxClassrunAttempts) +
                   "), retrying: " + helper_class);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // Unreachable: the loop above always returns.
    Error err;
    err.operation = "ScreenWorkflow";
    err.category = ErrorCategory::Internal;
    err.message = "Unreachable classrun retry state";
    return Result<std::string, Error>::Err(err);
}

// Best-effort cleanup — logs but never fails the caller's result on error,
// since a cleanup failure must not mask the actual operation's outcome.
void CleanupHelper(IAdtSession& session, const std::string& helper_class) {
    const std::string uri = HelperClassUri(helper_class);
    auto object_uri = ObjectUri::Create(uri);
    if (object_uri.IsErr()) {
        LogWarn(kComponent, "Could not build URI to clean up helper class " + helper_class);
        return;
    }
    auto deleted = DeleteObjectWithAutoLock(session, object_uri.Value(), std::nullopt);
    if (deleted.IsErr()) {
        LogWarn(kComponent, "Failed to clean up disposable screen helper class " +
                                helper_class + ": " + deleted.Error().message);
    }
}

template <typename T>
Result<T, Error> FinishWithCleanup(IAdtSession& session,
                                   const ScreenWorkflowTarget& target,
                                   Result<T, Error> result) {
    if (!target.keep_helper) {
        CleanupHelper(session, target.helper_class);
    }
    return result;
}

}  // namespace

Result<void, Error> RunScreenCreateWorkflow(IAdtSession& session,
                                            IAdtSession& classrun_session,
                                            const ScreenCreateWorkflowParams& params) {
    const auto& target = params.target;

    auto ensured = EnsureHelperClass(session, target.helper_class, target.package);
    if (ensured.IsErr()) {
        return Result<void, Error>::Err(std::move(ensured).Error());
    }

    const std::string next_screen =
        params.next_screen.empty() ? target.screen : params.next_screen;
    const std::string source = GenerateScreenCreateSource(
        target.helper_class, target.program, target.screen, params.description,
        next_screen, params.lines, params.columns, params.fields);

    auto run_result = WriteActivateRun(session, classrun_session, target.helper_class, source);
    if (run_result.IsErr()) {
        return FinishWithCleanup(session, target,
                                 Result<void, Error>::Err(std::move(run_result).Error()));
    }

    auto parsed = ParseScreenCreateOutput(run_result.Value());
    return FinishWithCleanup(session, target, std::move(parsed));
}

Result<ScreenReadResult, Error> RunScreenReadWorkflow(IAdtSession& session,
                                                      IAdtSession& classrun_session,
                                                      const ScreenWorkflowTarget& target) {
    auto ensured = EnsureHelperClass(session, target.helper_class, target.package);
    if (ensured.IsErr()) {
        return Result<ScreenReadResult, Error>::Err(std::move(ensured).Error());
    }

    const std::string source =
        GenerateScreenReadSource(target.helper_class, target.program, target.screen);

    auto run_result = WriteActivateRun(session, classrun_session, target.helper_class, source);
    if (run_result.IsErr()) {
        return FinishWithCleanup(
            session, target,
            Result<ScreenReadResult, Error>::Err(std::move(run_result).Error()));
    }

    auto parsed = ParseScreenReadOutput(run_result.Value());
    return FinishWithCleanup(session, target, std::move(parsed));
}

Result<void, Error> RunScreenDeleteWorkflow(IAdtSession& session,
                                            IAdtSession& classrun_session,
                                            const ScreenWorkflowTarget& target) {
    auto ensured = EnsureHelperClass(session, target.helper_class, target.package);
    if (ensured.IsErr()) {
        return Result<void, Error>::Err(std::move(ensured).Error());
    }

    const std::string source =
        GenerateScreenDeleteSource(target.helper_class, target.program, target.screen);

    auto run_result = WriteActivateRun(session, classrun_session, target.helper_class, source);
    if (run_result.IsErr()) {
        return FinishWithCleanup(session, target,
                                 Result<void, Error>::Err(std::move(run_result).Error()));
    }

    auto parsed = ParseScreenDeleteOutput(run_result.Value());
    return FinishWithCleanup(session, target, std::move(parsed));
}

}  // namespace erpl_adt
