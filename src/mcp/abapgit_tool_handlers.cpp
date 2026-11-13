#include <erpl_adt/mcp/abapgit_tool_handlers.hpp>

#include <erpl_adt/app/abapgit_command.hpp>
#include <erpl_adt/mcp/tool_metadata.hpp>
#include <erpl_adt/workflow/classrun_helper.hpp>

#include <nlohmann/json.hpp>

#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace erpl_adt {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

Error PathRefused(const std::string& message) {
    Error err;
    err.operation = "abapgit_path";
    err.message = message;
    err.category = ErrorCategory::Authorization;
    err.hint = "Pass a path relative to the abapGit root of this MCP server, or start the "
               "server with --abapgit-root <dir> (YAML: mcp.abapgit_root).";
    return err;
}

// `path` equals `root` or lies below it (both canonical).
bool IsWithin(const fs::path& root, const fs::path& path) {
    const auto rel = path.lexically_relative(root);
    if (rel.empty()) {
        return false;
    }
    const auto first = *rel.begin();
    return first != "..";
}

ToolResult ErrorResult(const Error& error) {
    ToolResult result;
    result.is_error = true;
    result.content = json::array({{{"type", "text"}, {"text", error.ToJson()}}});
    return result;
}

ToolResult ParamError(const std::string& message) {
    Error err;
    err.operation = "abapgit";
    err.message = message;
    err.category = ErrorCategory::Internal;
    return ErrorResult(err);
}

json StringProp(const std::string& description) {
    return {{"type", "string"}, {"description", description}};
}
json BoolProp(const std::string& description) {
    return {{"type", "boolean"}, {"description", description}};
}

struct ToolSpec {
    const char* name;
    AbapGitCommand command;
    const char* description;
    std::vector<std::string> args;  // accepted besides path and package
};

json SchemaFor(const ToolSpec& spec) {
    json props = {
        {"path", StringProp(spec.command == AbapGitCommand::Export
                                ? "Repository folder to write, relative to the server's abapGit "
                                  "root (created if missing)"
                                : "abapGit repository folder (with .abapgit.xml) or one object "
                                  "file, relative to the server's abapGit root")},
        {"package", StringProp("Target (root) package, e.g. $TMP or ZDEMO")},
    };
    for (const auto& a : spec.args) {
        if (a == "transport") {
            props[a] = StringProp("Workbench request; required for transportable packages");
        } else if (a == "dry_run") {
            props[a] = BoolProp("Only report what would change");
        } else if (a == "force") {
            props[a] = BoolProp(spec.command == AbapGitCommand::Status
                                    ? "Report drifted objects as they would be overwritten"
                                    : "Overwrite / delete objects changed in the system "
                                      "outside git");
        } else if (a == "strict") {
            props[a] = BoolProp("A missing transport is a refusal (exit 9), not a note");
        } else if (a == "objects") {
            props[a] = {{"type", "array"},
                        {"items", {{"type", "string"}}},
                        {"description",
                         "Objects to export as TYPE:NAME (e.g. PROG:ZDEMO); default: refresh "
                         "the objects already in path"}};
        } else if (a == "master_language") {
            props[a] = StringProp("Master language for a new repository (default E)");
        }
    }
    return {{"type", "object"},
            {"properties", props},
            {"required", json::array({"path", "package"})},
            {"additionalProperties", false}};
}

Result<AbapGitRequest, std::string> ParseRequest(const ToolSpec& spec, const json& params) {
    using R = Result<AbapGitRequest, std::string>;
    if (!params.is_object()) {
        return R::Err("Arguments must be an object");
    }
    std::set<std::string> allowed(spec.args.begin(), spec.args.end());
    allowed.insert("path");
    allowed.insert("package");
    for (const auto& [key, value] : params.items()) {
        if (allowed.count(key) == 0) {
            // A typo in a safety argument (dryrun) must not run destructively.
            return R::Err("Unknown argument '" + key + "' for " + spec.name +
                          ". Nothing was done.");
        }
    }
    AbapGitRequest request;
    for (const char* key : {"path", "package"}) {
        if (!params.contains(key) || !params[key].is_string() ||
            params[key].get<std::string>().empty()) {
            return R::Err(std::string("Missing required parameter: ") + key);
        }
    }
    request.package = params["package"].get<std::string>();
    auto str = [&](const char* key) -> Result<std::string, std::string> {
        if (!params.contains(key)) {
            return Result<std::string, std::string>::Ok("");
        }
        if (!params[key].is_string()) {
            return Result<std::string, std::string>::Err(std::string(key) + " must be a string");
        }
        return Result<std::string, std::string>::Ok(params[key].get<std::string>());
    };
    auto flag = [&](const char* key) -> Result<bool, std::string> {
        if (!params.contains(key)) {
            return Result<bool, std::string>::Ok(false);
        }
        if (!params[key].is_boolean()) {
            return Result<bool, std::string>::Err(std::string(key) + " must be a boolean");
        }
        return Result<bool, std::string>::Ok(params[key].get<bool>());
    };
    auto transport = str("transport");
    auto language = str("master_language");
    auto dry_run = flag("dry_run");
    auto force = flag("force");
    auto strict = flag("strict");
    for (const auto* r : {&transport, &language}) {
        if (r->IsErr()) return R::Err(r->Error());
    }
    for (const auto* r : {&dry_run, &force, &strict}) {
        if (r->IsErr()) return R::Err(r->Error());
    }
    request.transport = transport.Value();
    request.master_language = language.Value();
    request.dry_run = dry_run.Value();
    request.force = force.Value();
    request.strict = strict.Value();
    if (params.contains("objects")) {
        const auto& objects = params["objects"];
        if (objects.is_string()) {
            std::stringstream ss(objects.get<std::string>());
            std::string item;
            while (std::getline(ss, item, ',')) {
                request.objects.push_back(item);
            }
        } else if (objects.is_array()) {
            for (const auto& o : objects) {
                if (!o.is_string()) {
                    return R::Err("objects must be strings like PROG:ZDEMO");
                }
                request.objects.push_back(o.get<std::string>());
            }
        } else {
            return R::Err("objects must be an array of TYPE:NAME strings");
        }
    }
    return R::Ok(std::move(request));
}

ToolResult RunTool(const ToolSpec& spec, IAdtSession& session, const AbapGitToolOptions& options,
                   const json& params) {
    auto request = ParseRequest(spec, params);
    if (request.IsErr()) {
        return ParamError(request.Error());
    }
    auto req = std::move(request).Value();
    auto resolved = ResolvePathUnderRoot(options.root, params["path"].get<std::string>());
    if (resolved.IsErr()) {
        return ErrorResult(resolved.Error());
    }
    req.path = resolved.Value();
    req.boundary = fs::weakly_canonical(fs::absolute(options.root));
    // Before the files are scanned: no link in the requested folder (or the
    // folder of the requested file) may lead out of the root.
    std::error_code ec;
    auto early = CheckNoSymlinkEscape(
        options.root, fs::is_directory(req.path, ec) ? req.path : req.path.parent_path());
    if (early.IsErr()) {
        return ErrorResult(early.Error());
    }

    auto prepared = PrepareAbapGitRun(spec.command, req);
    if (prepared.IsErr()) {
        return ErrorResult(prepared.Error());
    }
    const auto& run = prepared.Value();
    // Everything the run reads or writes lies in its scope; no link there may
    // lead out of the root.
    auto contained = CheckNoSymlinkEscape(options.root, run.scope.empty() ? req.path : run.scope);
    if (contained.IsErr()) {
        return ErrorResult(contained.Error());
    }
    for (const auto& object : run.objects) {
        for (const auto& [part, file] : object.files) {
            auto inside = ResolvePathUnderRoot(options.root, file.string());
            if (inside.IsErr()) {
                return ErrorResult(inside.Error());
            }
        }
    }

    if (!options.make_classrun_session) {
        return ParamError("abapgit tools need a classrun session factory");
    }
    auto classrun_session = options.make_classrun_session();
    ClassrunHelperOptions helper;
    helper.helper_class = MakeHelperClassName();
    helper.description = "Temporary erpl-adt abapgit helper (safe to delete)";
    const auto report = RunAbapGit(run, session, *classrun_session, helper);
    const auto j = AbapGitReportJson(run, report);
    // Same document as `erpl-adt --json abapgit ...`; isError mirrors a
    // non-zero exit code.
    return ToolResult(report.ExitCode() != 0, json::array({{{"type", "text"}, {"text", j.dump()}}}),
                      j);
}

}  // namespace

Result<fs::path, Error> ResolvePathUnderRoot(const fs::path& root, const std::string& requested) {
    using R = Result<fs::path, Error>;
    std::error_code ec;
    const auto canonical_root = fs::canonical(fs::absolute(root, ec), ec);
    if (ec) {
        return R::Err(PathRefused("abapGit root " + root.string() + " does not exist"));
    }
    const fs::path asked(requested);
    const fs::path candidate = asked.is_absolute() ? asked : canonical_root / asked;
    const auto resolved = fs::weakly_canonical(candidate, ec);
    if (ec) {
        return R::Err(PathRefused("Cannot resolve " + requested + ": " + ec.message()));
    }
    if (!IsWithin(canonical_root, resolved)) {
        return R::Err(PathRefused("Path " + requested + " resolves to " + resolved.string() +
                                  ", outside the abapGit root " + canonical_root.string()));
    }
    return R::Ok(resolved);
}

Result<void, Error> CheckNoSymlinkEscape(const fs::path& root, const fs::path& dir) {
    using R = Result<void, Error>;
    std::error_code ec;
    const auto canonical_root = fs::canonical(fs::absolute(root, ec), ec);
    if (ec) {
        return R::Err(PathRefused("abapGit root " + root.string() + " does not exist"));
    }
    if (!fs::is_directory(dir, ec)) {
        return R::Ok();
    }
    fs::recursive_directory_iterator it(dir, fs::directory_options::none, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_symlink(ec)) {
            continue;
        }
        const auto target = fs::weakly_canonical(it->path(), ec);
        if (ec || !IsWithin(canonical_root, target)) {
            return R::Err(PathRefused("Symlink " + it->path().string() + " points outside the "
                                      "abapGit root " + canonical_root.string()));
        }
    }
    return R::Ok();
}

void RegisterAbapGitTools(ToolRegistry& registry, IAdtSession& session,
                          AbapGitToolOptions options) {
    const std::string how =
        " Classic objects (DOMA, DTEL, FUGR, PROG with text pool, dynpros and GUI status, "
        "TRAN) are read and written through SAP's workbench function modules from a "
        "temporary helper class in $TMP. Only customer objects (Y*, Z*, registered "
        "namespaces) are touched. The result is the JSON report of `erpl-adt --json abapgit "
        "...`: objects[] with action, parts, message, category, exit_code, and a top-level "
        "exit_code (0 ok, 1 authorization/guard rail, 2 not found, 5 activation, 6 lock or "
        "drift, 8 part not writable, 9 transport, 10 timeout, 99 usage). Paths are relative "
        "to the server's abapGit root.";
    const std::vector<ToolSpec> specs = {
        {"abapgit_status", AbapGitCommand::Status,
         "Show per object whether a deploy of abapGit files would create, update or leave it "
         "unchanged, and what it would refuse. Changes no object.",
         {"transport", "strict", "force"}},
        {"abapgit_deploy", AbapGitCommand::Deploy,
         "Create or update classic SAP objects from abapGit files and activate them; objects "
         "already equal to the files are left alone.",
         {"transport", "dry_run", "strict", "force"}},
        {"abapgit_export", AbapGitCommand::Export,
         "Write objects from the system as abapGit files (byte-identical to abapGit), "
         "overwriting the files in path and removing files of parts the system no longer has.",
         {"objects", "dry_run", "master_language"}},
        {"abapgit_delete", AbapGitCommand::Delete,
         "Delete the objects described by abapGit files from the system.",
         {"transport", "dry_run", "force"}},
    };
    auto shared = std::make_shared<AbapGitToolOptions>(std::move(options));
    for (const auto& spec : specs) {
        registry.Register(spec.name, std::string(spec.description) + how, SchemaFor(spec),
                          [spec, shared, &session](const json& params) {
                              return RunTool(spec, session, *shared, params);
                          });
    }
    ApplyToolMetadata(registry);
    ApplyToolOutputSchemas(registry);
}

}  // namespace erpl_adt
