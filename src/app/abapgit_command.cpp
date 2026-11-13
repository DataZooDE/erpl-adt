#include <erpl_adt/app/abapgit_command.hpp>

#include <erpl_adt/core/log.hpp>
#include <erpl_adt/core/types.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace erpl_adt {

namespace {

namespace fs = std::filesystem;

Error ValidationError(const std::string& message) {
    return Error{"Validation", "", std::nullopt, message, std::nullopt};
}

Error NotFound(const std::string& message) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    err.category = ErrorCategory::NotFound;
    return err;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// The .abapgit.xml that governs `start` (the folder itself or a parent), not
// looking above `boundary` when one is given.
std::optional<fs::path> FindDotAbapGit(fs::path start, const std::optional<fs::path>& boundary) {
    std::error_code ec;
    start = fs::absolute(start, ec);
    std::optional<fs::path> stop;
    if (boundary) {
        stop = fs::weakly_canonical(fs::absolute(*boundary, ec), ec);
        start = fs::weakly_canonical(start, ec);
    }
    for (auto dir = start; !dir.empty(); dir = dir.parent_path()) {
        if (fs::exists(dir / ".abapgit.xml", ec)) {
            return dir / ".abapgit.xml";
        }
        if (dir == dir.parent_path() || (stop && dir == *stop)) {
            break;
        }
    }
    return std::nullopt;
}

Result<AbapGitRepoConfig, Error> LoadDotAbapGit(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    auto parsed = ParseDotAbapGit(buf.str());
    if (parsed.IsErr()) {
        auto err = parsed.Error();
        err.message = file.string() + ": " + err.message;
        return Result<AbapGitRepoConfig, Error>::Err(err);
    }
    return parsed;
}

AbapGitLayout ToLayout(const AbapGitRepoConfig& config) {
    AbapGitLayout layout;
    layout.starting_folder = config.starting_folder;
    layout.folder_logic = config.folder_logic;
    layout.ignore = config.ignore;
    return layout;
}

Result<AbapGitRunOptions, Error> MakeOptions(const AbapGitRequest& request) {
    using R = Result<AbapGitRunOptions, Error>;
    AbapGitRunOptions options;
    options.root_package = Upper(request.package);
    if (options.root_package.empty()) {
        return R::Err(
            ValidationError("Missing --package (e.g. --package '$TMP' or --package ZDEMO)"));
    }
    auto valid = PackageName::Create(options.root_package);
    if (valid.IsErr()) {
        return R::Err(ValidationError("Invalid --package: " + valid.Error()));
    }
    options.transport = Upper(request.transport);
    if (!options.transport.empty()) {
        // Validated once here: it goes into ABAP literals and ?corrNr= queries.
        auto valid_transport = TransportId::Create(options.transport);
        if (valid_transport.IsErr()) {
            return R::Err(ValidationError("Invalid --transport: " + valid_transport.Error()));
        }
    }
    options.dry_run = request.dry_run;
    options.force = request.force;
    options.strict = request.strict;
    return R::Ok(std::move(options));
}

struct ResolvedFiles {
    std::vector<AbapGitObjectFiles> objects;
    std::vector<std::string> skipped;
    std::string language = "E";
    fs::path repo_root;  // folder of the governing .abapgit.xml, if any
    fs::path scope;
};

// <path> is either an abapGit repository folder (with .abapgit.xml) or one
// file of a single object (e.g. src/zfoo.prog.xml).
Result<ResolvedFiles, Error> ResolveFiles(const AbapGitRequest& request,
                                          const std::string& package) {
    using R = Result<ResolvedFiles, Error>;
    const fs::path& path = request.path;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return R::Err(NotFound("Path not found: " + path.string()));
    }
    ResolvedFiles target;
    const bool is_dir = fs::is_directory(path, ec);
    auto dot = FindDotAbapGit(is_dir ? path : path.parent_path(), request.boundary);
    AbapGitRepoConfig config;
    if (dot) {
        auto loaded = LoadDotAbapGit(*dot);
        if (loaded.IsErr()) {
            return R::Err(loaded.Error());
        }
        config = loaded.Value();
        target.language = config.master_language.empty() ? "E" : config.master_language;
        target.repo_root = dot->parent_path();
    }
    target.scope = dot ? dot->parent_path() : (is_dir ? path : path.parent_path());

    if (fs::is_regular_file(path, ec)) {
        // Inside a repository: the same scan as for the whole repository,
        // filtered to this object, so its folder decides its package.
        auto object = dot ? ScanAbapGitObjectInRepo(path, dot->parent_path(), ToLayout(config),
                                                     package)
                          : ScanAbapGitObject(path, package);
        if (object.IsErr()) {
            return R::Err(object.Error());
        }
        target.objects.push_back(object.Value());
        return R::Ok(std::move(target));
    }
    if (!dot || fs::weakly_canonical(dot->parent_path(), ec) !=
                    fs::weakly_canonical(fs::absolute(path, ec), ec)) {
        return R::Err(NotFound("No .abapgit.xml in " + path.string() +
                               " — pass the root of an abapGit repository or a single object "
                               "file"));
    }
    auto scan = ScanAbapGitFolder(path, ToLayout(config), package);
    if (scan.IsErr()) {
        return R::Err(scan.Error());
    }
    target.objects = scan.Value().objects;
    for (const auto& f : scan.Value().unsupported) {
        target.skipped.push_back(f + " (object type not supported yet)");
    }
    return R::Ok(std::move(target));
}

Result<void, Error> PrepareExport(const AbapGitRequest& request, PreparedAbapGitRun& run) {
    using R = Result<void, Error>;
    const fs::path& dir = request.path;
    run.scope = dir;

    // Repository layout: reuse an existing .abapgit.xml, otherwise start a
    // new repository with abapGit's defaults (PREFIX, /src/).
    std::error_code ec;
    const bool has_dot = fs::exists(dir / ".abapgit.xml", ec);
    if (has_dot) {
        auto loaded = LoadDotAbapGit(dir / ".abapgit.xml");
        if (loaded.IsErr()) {
            return R::Err(loaded.Error());
        }
        run.export_config = loaded.Value();
    } else {
        run.export_config.master_language =
            request.master_language.empty() ? "E" : request.master_language;
    }
    run.write_dot_abapgit = !has_dot;
    run.options.language =
        run.export_config.master_language.empty() ? "E" : run.export_config.master_language;

    // What the repository already holds: each object's folder and files, so a
    // refresh writes back in place and removes files of deleted parts.
    std::vector<AbapGitObjectFiles> scanned;
    if (has_dot) {
        auto scan = ScanAbapGitFolder(dir, ToLayout(run.export_config), run.options.root_package);
        if (scan.IsErr()) {
            return R::Err(scan.Error());
        }
        scanned = scan.Value().objects;
    }

    // Which objects: the requested list, else the supported objects already in <dir>.
    for (const auto& raw : request.objects) {
        auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const std::string spec = raw.substr(first, raw.find_last_not_of(" \t") - first + 1);
        auto colon = spec.find(':');
        if (colon == std::string::npos) {
            return R::Err(ValidationError("Invalid --object '" + spec +
                                          "', expected TYPE:NAME, e.g. PROG:ZERPL_DEMO"));
        }
        AbapGitObjectKey key{Upper(spec.substr(0, colon)), Upper(spec.substr(colon + 1))};
        auto known = std::find_if(scanned.begin(), scanned.end(),
                                  [&](const AbapGitObjectFiles& o) { return o.key == key; });
        if (known != scanned.end()) {
            run.objects.push_back(*known);
        } else {
            AbapGitObjectFiles wanted;  // folder derived from its TADIR package
            wanted.key = key;
            run.objects.push_back(wanted);
        }
    }
    if (request.objects.empty()) {
        for (const auto& o : scanned) {
            if (o.key.type != "DEVC") {
                run.objects.push_back(o);
            }
        }
    }
    if (run.objects.empty()) {
        return R::Err(ValidationError(
            "Nothing to export: pass --object TYPE:NAME[,...] or point <dir> at an abapGit "
            "repository whose objects should be refreshed from the system"));
    }
    run.export_target.repo_root = dir;
    run.export_target.layout = ToLayout(run.export_config);
    return R::Ok();
}

}  // namespace

std::string ToString(AbapGitCommand command) {
    switch (command) {
        case AbapGitCommand::Deploy: return "deploy";
        case AbapGitCommand::Status: return "deploy";  // status is a dry-run deploy
        case AbapGitCommand::Export: return "export";
        case AbapGitCommand::Delete: return "delete";
    }
    return "deploy";
}

Result<PreparedAbapGitRun, Error> PrepareAbapGitRun(AbapGitCommand command,
                                                    const AbapGitRequest& request) {
    using R = Result<PreparedAbapGitRun, Error>;
    AbapGitRequest effective = request;
    if (command == AbapGitCommand::Status) {
        effective.dry_run = true;
    }
    auto options = MakeOptions(effective);
    if (options.IsErr()) {
        return R::Err(options.Error());
    }
    PreparedAbapGitRun run;
    run.command = command;
    run.options = options.Value();

    if (command == AbapGitCommand::Export) {
        auto prepared = PrepareExport(effective, run);
        if (prepared.IsErr()) {
            return R::Err(prepared.Error());
        }
        return R::Ok(std::move(run));
    }
    auto files = ResolveFiles(effective, run.options.root_package);
    if (files.IsErr()) {
        return R::Err(files.Error());
    }
    run.objects = files.Value().objects;
    run.skipped = files.Value().skipped;
    run.scope = files.Value().scope;
    run.options.language = files.Value().language;
    run.options.repo_root = files.Value().repo_root;
    return R::Ok(std::move(run));
}

AbapGitRunReport RunAbapGit(const PreparedAbapGitRun& run, IAdtSession& session,
                            IAdtSession& classrun_session, const ClassrunHelperOptions& helper,
                            const std::string& system) {
    // Best effort: remove our own helpers that interrupted runs left behind
    // more than two hours ago (never someone else's, never a fresh one).
    auto swept = CleanupStaleHelpers(session, session.LogonUserName(), std::chrono::minutes(120));
    for (const auto& name : swept.deleted) {
        LogInfo("abapgit", "Removed stale helper class " + name);
    }
    ClassrunEngineRunner engine(session, classrun_session, helper);
    GitFileHistory history;
    // What this clone last deployed to this system (see IDeployRecord).
    std::error_code ec;
    fs::path folder = run.scope.empty() ? fs::current_path(ec) : run.scope;
    if (!fs::is_directory(folder, ec)) {
        folder = folder.parent_path();
    }
    if (folder.empty()) {
        folder = ".";
    }
    FileDeployRecord record(FileDeployRecord::DefaultDirectory(folder), system);
    AbapGitWorkflow workflow(session, engine, DefaultTypeHandlers(), history, record);

    switch (run.command) {
        case AbapGitCommand::Deploy:
        case AbapGitCommand::Status: {
            auto report = workflow.Deploy(run.objects, run.options);
            report.skipped.insert(report.skipped.end(), run.skipped.begin(), run.skipped.end());
            return report;
        }
        case AbapGitCommand::Delete:
            return workflow.Delete(run.objects, run.options);
        case AbapGitCommand::Export: {
            auto report = workflow.Export(run.objects, run.export_target, run.options);
            if (run.write_dot_abapgit && !run.options.dry_run && !report.Failed()) {
                std::error_code ec;
                fs::create_directories(run.export_target.repo_root, ec);
                std::ofstream(run.export_target.repo_root / ".abapgit.xml", std::ios::binary)
                    << RenderDotAbapGit(run.export_config);
            }
            return report;
        }
    }
    return AbapGitRunReport{};
}

nlohmann::json AbapGitReportJson(const PreparedAbapGitRun& run, const AbapGitRunReport& report) {
    std::map<std::string, int> summary;
    for (const auto& o : report.objects) {
        ++summary[ToString(o.action)];
    }
    nlohmann::json j;
    j["command"] = ToString(run.command);
    j["package"] = run.options.root_package;
    j["dry_run"] = run.options.dry_run;
    j["objects"] = nlohmann::json::array();
    for (const auto& o : report.objects) {
        nlohmann::json e;
        e["type"] = o.key.type;
        e["name"] = o.key.name;
        e["package"] = o.package;
        e["action"] = ToString(o.action);
        e["parts"] = o.parts;
        if (!o.message.empty()) {
            e["message"] = o.message;
        }
        if (!o.notes.empty()) {
            e["notes"] = o.notes;
        }
        if (o.error.has_value()) {
            e["category"] = o.error->CategoryName();
            e["exit_code"] = o.error->ExitCode();
        }
        j["objects"].push_back(e);
    }
    j["skipped"] = report.skipped;
    j["summary"] = summary;
    j["exit_code"] = AbapGitExitCode(run, report);
    return j;
}

int AbapGitExitCode(const PreparedAbapGitRun& run, const AbapGitRunReport& report) {
    const bool status = run.command == AbapGitCommand::Status ||
                        (run.command == AbapGitCommand::Deploy && run.options.dry_run);
    return status ? report.StatusExitCode(run.options.strict) : report.ExitCode();
}

std::string AbapGitReportText(const PreparedAbapGitRun& run, const AbapGitRunReport& report) {
    const bool dry_run = run.options.dry_run;
    std::map<std::string, int> summary;
    std::ostringstream out;
    for (const auto& o : report.objects) {
        ++summary[ToString(o.action)];
        // Only what a real run would do is "would be ..."; unchanged,
        // refused, failed and skipped read the same in a dry run.
        const bool would = dry_run && (o.action == ObjectAction::Created ||
                                       o.action == ObjectAction::Updated ||
                                       o.action == ObjectAction::Deleted);
        out << (would ? "would be " : "") << ToString(o.action) << "  " << o.key.type << " "
            << o.key.name << "  (" << o.package << ")";
        if (!o.parts.empty()) {
            out << "  ";
            for (size_t i = 0; i < o.parts.size(); ++i) {
                out << (i ? ", " : "") << o.parts[i];
            }
        }
        if (!o.message.empty()) {
            out << "\n    " << o.message;
        }
        for (const auto& note : o.notes) {
            out << "\n    note: " << note;
        }
        out << "\n";
    }
    for (const auto& s : report.skipped) {
        out << "skipped  " << s << "\n";
    }
    std::string line;
    for (const auto& [action, count] : summary) {
        line += (line.empty() ? "" : ", ") + std::to_string(count) + " " + action;
    }
    out << (line.empty() ? "nothing to do" : line) << (dry_run ? " (dry run)" : "");
    return out.str();
}

}  // namespace erpl_adt
