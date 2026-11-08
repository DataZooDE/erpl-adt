#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/core/log.hpp>

#include "abapgit_types.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace erpl_adt {

namespace {

constexpr const char* kComponent = "abapgit";

Error MakeError(const std::string& message, ErrorCategory category) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    err.category = category;
    return err;
}

ObjectOutcome Outcome(const AbapGitObjectKey& key, const std::string& package,
                      ObjectAction action) {
    ObjectOutcome o;
    o.key = key;
    o.package = package;
    o.action = action;
    return o;
}

ObjectOutcome FailedOutcome(const AbapGitObjectKey& key, const std::string& package,
                            ObjectAction action, const Error& error) {
    auto o = Outcome(key, package, action);
    o.message = error.message;
    o.error = error;
    return o;
}

Result<std::string, Error> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::string, Error>::Err(
            MakeError("Cannot read " + path.string(), ErrorCategory::Internal));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return Result<std::string, Error>::Ok(buffer.str());
}

std::string ToLf(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out += text[i];
    }
    return out;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Guard rails that must pass before anything touches the system.
std::optional<Error> CheckGuardRails(const AbapGitObjectFiles& object,
                                     const AbapGitRunOptions& options) {
    auto customer = CheckCustomerObject(object.key, options.customer_namespaces);
    if (customer.IsErr()) {
        return customer.Error();
    }
    auto package = CheckTargetPackage(object.package, options.customer_namespaces);
    if (package.IsErr()) {
        return package.Error();
    }
    if (!IsLocalPackage(object.package) && options.transport.empty()) {
        return MakeError("Package " + object.package +
                             " is transportable: pass --transport <request> so the changes "
                             "are recorded like in SE80 (or deploy into a local $ package).",
                         ErrorCategory::TransportError);
    }
    return std::nullopt;
}

}  // namespace

std::string ToString(ObjectAction action) {
    switch (action) {
        case ObjectAction::Created: return "created";
        case ObjectAction::Updated: return "updated";
        case ObjectAction::Unchanged: return "unchanged";
        case ObjectAction::Deleted: return "deleted";
        case ObjectAction::Missing: return "missing";
        case ObjectAction::Exported: return "exported";
        case ObjectAction::Failed: return "failed";
        case ObjectAction::Refused: return "refused";
    }
    return "unknown";
}

bool AbapGitRunReport::Failed() const {
    return std::any_of(objects.begin(), objects.end(), [](const ObjectOutcome& o) {
        return o.action == ObjectAction::Failed || o.action == ObjectAction::Refused;
    });
}

std::optional<Error> AbapGitRunReport::FirstError() const {
    for (const auto& o : objects) {
        if (o.error.has_value()) {
            return o.error;
        }
    }
    return std::nullopt;
}

Result<ObjectFileSet, Error> LoadObjectFiles(const AbapGitObjectFiles& object) {
    ObjectFileSet files;
    const std::string stem = ObjectFileStem(object.key);
    for (const auto& [part, path] : object.files) {
        auto bytes = ReadFile(path);
        if (bytes.IsErr()) {
            return Result<ObjectFileSet, Error>::Err(std::move(bytes).Error());
        }
        const std::string name = stem + "." + part;
        if (EndsWith(part, "xml")) {
            auto parsed = ParseAbapGitXml(bytes.Value());
            if (parsed.IsErr()) {
                auto err = parsed.Error();
                err.message = path.string() + ": " + err.message;
                return Result<ObjectFileSet, Error>::Err(err);
            }
            files[name] = RenderAbapGitXml(parsed.Value());
        } else {
            files[name] = ToLf(bytes.Value());
        }
    }
    return Result<ObjectFileSet, Error>::Ok(std::move(files));
}

std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> DefaultTypeHandlers() {
    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers;
    for (auto& handler : MakeClassicTypeHandlers()) {
        handlers[handler->Type()] = handler;
    }
    return handlers;
}

AbapGitWorkflow::AbapGitWorkflow(
    IAdtSession& session, IEngineRunner& engine,
    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers)
    : session_(session), engine_(engine), handlers_(std::move(handlers)) {}

const IClassicTypeHandler* AbapGitWorkflow::Handler(const std::string& type) const {
    auto it = handlers_.find(type);
    return it == handlers_.end() ? nullptr : it->second.get();
}

AbapGitWorkflow::ReadResult AbapGitWorkflow::ReadStates(const std::vector<AbapGitObjectKey>& keys,
                                                        const AbapGitRunOptions& options) {
    ReadResult result;
    std::vector<EngineOp> ops;
    std::vector<std::pair<size_t, size_t>> slices;  // offset, count per key
    for (const auto& key : keys) {
        auto key_ops = Handler(key.type)->ReadOps(key, options.language);
        slices.emplace_back(ops.size(), key_ops.size());
        ops.insert(ops.end(), key_ops.begin(), key_ops.end());
    }

    std::vector<EngineOpResult> all;
    if (!ops.empty()) {
        auto run = engine_.Run(ops);
        if (run.IsErr()) {
            for (size_t i = 0; i < keys.size(); ++i) {
                result.states.push_back(Result<ObjectState, Error>::Err(run.Error()));
            }
            return result;
        }
        all = std::move(run).Value();
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<EngineOpResult> slice(
            all.begin() + static_cast<std::ptrdiff_t>(slices[i].first),
            all.begin() + static_cast<std::ptrdiff_t>(slices[i].first + slices[i].second));
        TypeContext ctx{session_, engine_, options.root_package, options.transport,
                        options.language};
        result.states.push_back(Handler(keys[i].type)->BuildState(keys[i], slice, ctx));
    }
    return result;
}

AbapGitRunReport AbapGitWorkflow::Deploy(const std::vector<AbapGitObjectFiles>& objects,
                                         const AbapGitRunOptions& options) {
    AbapGitRunReport report;
    std::vector<const AbapGitObjectFiles*> eligible;
    std::vector<ObjectFileSet> desired;

    for (const auto& object : objects) {
        if (object.key.type == "DEVC") {
            report.skipped.push_back("package " + object.key.name +
                                     " (packages are not created; it must exist)");
            continue;
        }
        if (Handler(object.key.type) == nullptr) {
            report.skipped.push_back(object.key.type + " " + object.key.name +
                                     " (object type not supported yet)");
            continue;
        }
        if (auto refused = CheckGuardRails(object, options)) {
            report.objects.push_back(
                FailedOutcome(object.key, object.package, ObjectAction::Refused, *refused));
            continue;
        }
        auto files = LoadObjectFiles(object);
        if (files.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, files.Error()));
            continue;
        }
        eligible.push_back(&object);
        desired.push_back(std::move(files).Value());
    }
    if (eligible.empty()) {
        return report;
    }

    std::vector<AbapGitObjectKey> keys;
    for (const auto* object : eligible) {
        keys.push_back(object->key);
    }
    auto states = ReadStates(keys, options);

    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& object = *eligible[i];
        const auto& state = states.states[i];
        if (state.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, state.Error()));
            continue;
        }
        const auto& current = state.Value();
        if (current.exists && current.files == desired[i]) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Unchanged));
            continue;
        }
        const auto action = current.exists ? ObjectAction::Updated : ObjectAction::Created;
        if (options.dry_run) {
            report.objects.push_back(Outcome(object.key, object.package, action));
            continue;
        }
        LogInfo(kComponent, (current.exists ? "Updating " : "Creating ") + object.key.type +
                                " " + object.key.name);
        TypeContext ctx{session_, engine_, object.package, options.transport, options.language};
        auto applied = Handler(object.key.type)->Apply(object.key, desired[i], current, ctx);
        if (applied.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, applied.Error()));
            continue;
        }
        auto outcome = Outcome(object.key, object.package, action);
        outcome.parts = applied.Value();
        report.objects.push_back(std::move(outcome));
    }
    return report;
}

AbapGitRunReport AbapGitWorkflow::Delete(const std::vector<AbapGitObjectFiles>& objects,
                                         const AbapGitRunOptions& options) {
    AbapGitRunReport report;
    std::vector<const AbapGitObjectFiles*> eligible;
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (it->key.type == "DEVC" || Handler(it->key.type) == nullptr) {
            report.skipped.push_back(it->key.type + " " + it->key.name);
            continue;
        }
        if (auto refused = CheckGuardRails(*it, options)) {
            report.objects.push_back(
                FailedOutcome(it->key, it->package, ObjectAction::Refused, *refused));
            continue;
        }
        eligible.push_back(&*it);
    }
    std::vector<AbapGitObjectKey> keys;
    for (const auto* object : eligible) {
        keys.push_back(object->key);
    }
    auto states = ReadStates(keys, options);
    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& object = *eligible[i];
        if (states.states[i].IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, states.states[i].Error()));
            continue;
        }
        if (!states.states[i].Value().exists) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Missing));
            continue;
        }
        if (options.dry_run) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Deleted));
            continue;
        }
        TypeContext ctx{session_, engine_, object.package, options.transport, options.language};
        auto deleted = Handler(object.key.type)->Delete(object.key, ctx);
        if (deleted.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, deleted.Error()));
            continue;
        }
        report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Deleted));
    }
    return report;
}

AbapGitRunReport AbapGitWorkflow::Export(const std::vector<AbapGitObjectKey>& keys,
                                         const std::filesystem::path& target_folder,
                                         const AbapGitRunOptions& options) {
    AbapGitRunReport report;
    std::vector<AbapGitObjectKey> eligible;
    for (const auto& key : keys) {
        if (Handler(key.type) == nullptr) {
            report.skipped.push_back(key.type + " " + key.name + " (object type not supported yet)");
            continue;
        }
        eligible.push_back(key);
    }
    auto states = ReadStates(eligible, options);
    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& key = eligible[i];
        if (states.states[i].IsErr()) {
            report.objects.push_back(FailedOutcome(key, options.root_package,
                                                   ObjectAction::Failed, states.states[i].Error()));
            continue;
        }
        const auto& state = states.states[i].Value();
        if (!state.exists) {
            auto o = Outcome(key, options.root_package, ObjectAction::Missing);
            o.message = key.type + " " + key.name + " does not exist in the system";
            o.error = MakeError(o.message, ErrorCategory::NotFound);
            report.objects.push_back(std::move(o));
            continue;
        }
        auto outcome = Outcome(key, options.root_package, ObjectAction::Exported);
        for (const auto& [name, bytes] : state.files) {
            outcome.parts.push_back(name);
            if (options.dry_run) {
                continue;
            }
            std::error_code ec;
            std::filesystem::create_directories(target_folder, ec);
            std::ofstream out(target_folder / name, std::ios::binary | std::ios::trunc);
            out << bytes;
            if (!out) {
                outcome.action = ObjectAction::Failed;
                outcome.message = "Cannot write " + (target_folder / name).string();
                outcome.error = MakeError(outcome.message, ErrorCategory::Internal);
            }
        }
        report.objects.push_back(std::move(outcome));
    }
    return report;
}

}  // namespace erpl_adt
