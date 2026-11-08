#include <erpl_adt/workflow/classrun_helper.hpp>

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
#include <cstdio>
#include <random>
#include <thread>

namespace erpl_adt {

namespace {

constexpr const char* kComponent = "classrun_helper";

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string HelperClassUri(const std::string& helper_class) {
    return "/sap/bc/adt/oo/classes/" + ToLowerCopy(helper_class);
}

Error HelperError(const std::string& message, ErrorCategory category) {
    Error err;
    err.operation = "ClassrunHelper";
    err.endpoint = "classrun";
    err.message = message;
    err.category = category;
    return err;
}

Result<void, Error> CheckLocalPackage(const ClassrunHelperOptions& options) {
    if (options.package.empty() || options.package[0] != '$') {
        return Result<void, Error>::Err(HelperError(
            "Helper classes must live in a local package such as $TMP, never in '" +
                options.package +
                "' — a transportable package would put them on a transport request.",
            ErrorCategory::Internal));
    }
    return Result<void, Error>::Ok();
}

// Creates the helper class in `package` if it does not exist yet. Any error
// other than "not found" (e.g. a connection problem) is propagated.
Result<void, Error> EnsureHelperClass(IAdtSession& session, const ClassrunHelperOptions& options) {
    const std::string uri = HelperClassUri(options.helper_class);
    auto exists = EnsureObjectExists(session, uri, "ClassrunHelper",
                                     "Helper class " + options.helper_class);
    if (exists.IsOk()) {
        return Result<void, Error>::Ok();
    }
    if (exists.Error().category != ErrorCategory::NotFound) {
        return Result<void, Error>::Err(exists.Error());
    }

    CreateObjectParams params;
    params.object_type = "CLAS/OC";
    params.name = options.helper_class;
    params.package_name = options.package;
    params.description = options.description;

    auto created = CreateObject(session, params);
    if (created.IsErr()) {
        return Result<void, Error>::Err(std::move(created).Error());
    }
    return Result<void, Error>::Ok();
}

// Writes and activates `source`. A helper whose activation failed must never
// be run: SAP would silently execute the previous active version.
Result<void, Error> WriteAndActivate(IAdtSession& session, const std::string& helper_class,
                                     const std::string& source) {
    const std::string object_uri = HelperClassUri(helper_class);
    auto write_result =
        WriteSourceWithAutoLock(session, object_uri + "/source/main", source, std::nullopt);
    if (write_result.IsErr()) {
        return Result<void, Error>::Err(std::move(write_result).Error());
    }

    ActivateObjectParams act_params;
    act_params.uri = write_result.Value();
    act_params.type = "CLAS/OC";
    act_params.name = helper_class;
    auto act_result = ActivateObject(session, act_params);
    if (act_result.IsErr()) {
        return Result<void, Error>::Err(std::move(act_result).Error());
    }
    if (act_result.Value().failed > 0) {
        std::string detail;
        for (const auto& m : act_result.Value().error_messages) {
            if (m == "Activation was cancelled.") {
                continue;
            }
            detail += (detail.empty() ? "" : "; ") + m;
        }
        auto err = HelperError(
            "The temporary helper class " + helper_class +
                " did not compile on this system, so nothing was run" +
                (detail.empty() ? std::string(".") : ": " + detail),
            ErrorCategory::ActivationError);
        err.endpoint = object_uri;
        return Result<void, Error>::Err(err);
    }
    return Result<void, Error>::Ok();
}

// RunClass is issued against `classrun_session` — a different IAdtSession
// than the one that wrote and activated the class. Confirmed live: reusing
// the writing session can answer "Class does not implement
// if_oo_adt_classrun~main method!" for 30+ seconds (stale generated-program
// buffer on the work process pinned to that stateful session), while a fresh
// session succeeds at once. A few short retries remain as a safety net.
Result<std::string, Error> RunHelperClass(IAdtSession& classrun_session,
                                          const std::string& helper_class) {
    constexpr int kMaxAttempts = 5;
    for (int attempt = 1;; ++attempt) {
        auto run_result = RunClass(classrun_session, helper_class);
        if (run_result.IsErr()) {
            return Result<std::string, Error>::Err(std::move(run_result).Error());
        }
        const std::string& output = run_result.Value().output;
        const bool not_ready =
            output.find("does not implement if_oo_adt_classrun") != std::string::npos;
        if (!not_ready || attempt == kMaxAttempts) {
            return Result<std::string, Error>::Ok(output);
        }
        LogWarn(kComponent, "classrun not yet ready after activation (attempt " +
                                std::to_string(attempt) + "/" + std::to_string(kMaxAttempts) +
                                "), retrying: " + helper_class);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Best effort: a cleanup failure is logged but never masks the real result.
void DeleteHelper(IAdtSession& session, const std::string& helper_class) {
    auto object_uri = ObjectUri::Create(HelperClassUri(helper_class));
    if (object_uri.IsErr()) {
        LogWarn(kComponent, "Could not build URI to clean up helper class " + helper_class);
        return;
    }
    auto deleted = DeleteObjectWithAutoLock(session, object_uri.Value(), std::nullopt);
    if (deleted.IsErr()) {
        LogWarn(kComponent, "Failed to delete temporary helper class " + helper_class +
                                " (delete it in SE24 or with 'erpl-adt object delete'): " +
                                deleted.Error().message);
    }
}

}  // namespace

std::string MakeHelperClassName() {
    static std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<unsigned long> dist(0, 0xFFFFFFFFUL);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08lX", dist(engine));
    return std::string("ZCL_ERPLHLP_") + buf;
}

Result<std::string, Error> RunClassrunHelper(IAdtSession& session, IAdtSession& classrun_session,
                                             const ClassrunHelperOptions& options,
                                             const std::string& source) {
    auto local = CheckLocalPackage(options);
    if (local.IsErr()) {
        return Result<std::string, Error>::Err(std::move(local).Error());
    }
    auto ensured = EnsureHelperClass(session, options);
    if (ensured.IsErr()) {
        return Result<std::string, Error>::Err(std::move(ensured).Error());
    }

    Result<std::string, Error> result = [&]() -> Result<std::string, Error> {
        auto written = WriteAndActivate(session, options.helper_class, source);
        if (written.IsErr()) {
            return Result<std::string, Error>::Err(std::move(written).Error());
        }
        return RunHelperClass(classrun_session, options.helper_class);
    }();

    if (!options.keep_helper) {
        DeleteHelper(session, options.helper_class);
    }
    return result;
}

ClassrunEngineRunner::ClassrunEngineRunner(IAdtSession& session, IAdtSession& classrun_session,
                                           ClassrunHelperOptions options)
    : session_(session), classrun_session_(classrun_session), options_(std::move(options)) {}

ClassrunEngineRunner::~ClassrunEngineRunner() {
    if (created_ && !options_.keep_helper) {
        DeleteHelper(session_, options_.helper_class);
    }
}

Result<std::vector<EngineOpResult>, Error> ClassrunEngineRunner::Run(
    const std::vector<EngineOp>& ops) {
    using R = Result<std::vector<EngineOpResult>, Error>;
    if (!created_) {
        auto local = CheckLocalPackage(options_);
        if (local.IsErr()) {
            return R::Err(std::move(local).Error());
        }
        auto ensured = EnsureHelperClass(session_, options_);
        if (ensured.IsErr()) {
            return R::Err(std::move(ensured).Error());
        }
        created_ = true;
    }
    auto written =
        WriteAndActivate(session_, options_.helper_class,
                         BuildEngineSource(options_.helper_class, ops));
    if (written.IsErr()) {
        return R::Err(std::move(written).Error());
    }
    auto output = RunHelperClass(classrun_session_, options_.helper_class);
    if (output.IsErr()) {
        return R::Err(std::move(output).Error());
    }
    return ParseEngineOutput(output.Value(), ops.size());
}

}  // namespace erpl_adt
