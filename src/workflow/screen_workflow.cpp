#include <erpl_adt/workflow/screen_workflow.hpp>

#include <erpl_adt/core/log.hpp>
#include <erpl_adt/workflow/classrun_helper.hpp>

namespace erpl_adt {

namespace {

constexpr const char* kComponent = "screen_workflow";

// The helper class is a throwaway console class; it always goes to a local
// package so it can never end up on a transport request, even when the user
// passed a transportable --package for the screen's program.
ClassrunHelperOptions HelperOptions(const ScreenWorkflowTarget& target) {
    ClassrunHelperOptions options;
    options.helper_class = target.helper_class;
    options.package = (!target.package.empty() && target.package[0] == '$') ? target.package
                                                                            : "$TMP";
    if (options.package != target.package) {
        LogInfo(kComponent, "Creating helper class " + target.helper_class +
                                " in $TMP instead of transportable package " + target.package);
    }
    options.description = "Disposable erpl-adt screen helper (safe to delete)";
    options.keep_helper = target.keep_helper;
    return options;
}

template <typename T, typename Parser>
Result<T, Error> RunAndParse(IAdtSession& session, IAdtSession& classrun_session,
                             const ScreenWorkflowTarget& target, const std::string& source,
                             Parser parse) {
    auto output = RunClassrunHelper(session, classrun_session, HelperOptions(target), source);
    if (output.IsErr()) {
        return Result<T, Error>::Err(std::move(output).Error());
    }
    return parse(output.Value());
}

}  // namespace

Result<void, Error> RunScreenCreateWorkflow(IAdtSession& session,
                                            IAdtSession& classrun_session,
                                            const ScreenCreateWorkflowParams& params) {
    const auto& target = params.target;
    const std::string next_screen =
        params.next_screen.empty() ? target.screen : params.next_screen;
    const std::string source = GenerateScreenCreateSource(
        target.helper_class, target.program, target.screen, params.description,
        next_screen, params.lines, params.columns, params.fields);
    return RunAndParse<void>(session, classrun_session, target, source,
                             ParseScreenCreateOutput);
}

Result<ScreenReadResult, Error> RunScreenReadWorkflow(IAdtSession& session,
                                                      IAdtSession& classrun_session,
                                                      const ScreenWorkflowTarget& target) {
    const std::string source =
        GenerateScreenReadSource(target.helper_class, target.program, target.screen);
    return RunAndParse<ScreenReadResult>(session, classrun_session, target, source,
                                         ParseScreenReadOutput);
}

Result<void, Error> RunScreenDeleteWorkflow(IAdtSession& session,
                                            IAdtSession& classrun_session,
                                            const ScreenWorkflowTarget& target) {
    const std::string source =
        GenerateScreenDeleteSource(target.helper_class, target.program, target.screen);
    return RunAndParse<void>(session, classrun_session, target, source,
                             ParseScreenDeleteOutput);
}

}  // namespace erpl_adt
