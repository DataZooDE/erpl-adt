#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/core/log.hpp>

#include "abapgit_types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
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

// Bytes exactly as abapGit would write them: XML re-rendered, ABAP with LF.
Result<std::string, Error> NormalizeFile(const std::string& name, const std::string& bytes) {
    if (EndsWith(name, ".xml")) {
        auto parsed = ParseAbapGitXml(bytes);
        if (parsed.IsErr()) {
            return Result<std::string, Error>::Err(parsed.Error());
        }
        return Result<std::string, Error>::Ok(RenderAbapGitXml(parsed.Value()));
    }
    return Result<std::string, Error>::Ok(ToLf(bytes));
}

// Guard rails that must pass before anything touches the system. A
// namespaced object or package passes here if its namespace is well formed;
// the namespace itself is checked against the namespaces changeable in the
// system (CheckAgainstSystem) once the system facts are read.
std::optional<Error> CheckGuardRails(const AbapGitObjectKey& key, const std::string& package,
                                     bool needs_transport, const AbapGitRunOptions& options) {
    std::vector<std::string> provisional;
    for (const auto& name : {key.name, package}) {
        if (auto ns = AbapNamespaceOf(name)) {
            provisional.push_back(*ns);
        }
    }
    auto customer = CheckCustomerObject(key, provisional);
    if (customer.IsErr()) {
        return customer.Error();
    }
    auto target = CheckTargetPackage(package, provisional);
    if (target.IsErr()) {
        return target.Error();
    }
    if (needs_transport && !IsLocalPackage(package) && options.transport.empty()) {
        return MakeError("Package " + package +
                             " is transportable: pass --transport <request> so the changes "
                             "are recorded like in SE80 (or deploy into a local $ package).",
                         ErrorCategory::TransportError);
    }
    return std::nullopt;
}

bool SameUser(std::string a, std::string b) {
    auto up = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    };
    return up(std::move(a)) == up(std::move(b));
}

// Arguments for the export command that pulls exactly this object into git.
std::string ExportHint(const AbapGitObjectFiles& object) {
    return "<repo> --package " + object.package + " --object " + object.key.type + ":" +
           object.key.name;
}

// TADIR package of the object and its parent packages (TDEVC-PARENTCL).
EngineOp TadirPackageOp(const AbapGitObjectKey& key) {
    return {"tadir_package( object = " + AbapQuote(key.type) + " name = " + AbapQuote(key.name) +
                " ).",
            ""};
}

// Folder (relative to the starting folder, "" or "a/b/") of the first package
// in `chain` (TADIR package, parent, grandparent, ...) when `root` is in the
// chain; Err otherwise.
Result<std::string, Error> FolderInRepo(const std::vector<std::string>& chain,
                                        const std::string& root,
                                        AbapGitLayout::FolderLogic logic) {
    auto at = std::find(chain.begin(), chain.end(), root);
    if (at == chain.end()) {
        return Result<std::string, Error>::Err(MakeError(
            "it is in package " + (chain.empty() ? std::string("(none)") : chain.front()) +
                ", which is not " + root + " or one of its sub-packages; export it into the "
                "repository of that package",
            ErrorCategory::PackageError));
    }
    std::string folder;
    for (auto child = at; child != chain.begin(); --child) {
        auto segment = FolderForPackage(*(child - 1), *child, logic);
        if (segment.IsErr()) {
            return segment;
        }
        folder += segment.Value();
    }
    return Result<std::string, Error>::Ok(folder);
}

std::string StartingFolder(const AbapGitLayout& layout) {
    std::string folder = layout.starting_folder;
    while (!folder.empty() && folder.front() == '/') {
        folder.erase(folder.begin());
    }
    return folder;
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '/';
}

// Whether `files` mention `name` as a whole word (case-insensitive): a DTEL
// names its DOMNAME, a TRAN its PGMNA, a program the types it uses.
bool Mentions(const ObjectFileSet& files, const std::string& name) {
    std::string upper_name = name;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const auto& [file, bytes] : files) {
        std::string text = bytes;
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        for (auto at = text.find(upper_name); at != std::string::npos;
             at = text.find(upper_name, at + 1)) {
            const bool starts = at == 0 || !IsNameChar(text[at - 1]);
            const size_t end = at + upper_name.size();
            const bool ends = end >= text.size() || !IsNameChar(text[end]);
            if (starts && ends) {
                return true;
            }
        }
    }
    return false;
}

// Path characters that may go into the popen command line (inside double
// quotes): an allowlist, so no shell metacharacter (quotes, $, `, ;, |, &,
// <, >, parentheses, globs, newlines) can ever reach the shell.
bool SafeForShell(const std::string& s) {
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
#ifdef _WIN32
        static const std::string kAllowed = "._-/\\#+@ ,=~:";  // no % (cmd expands it)
#else
        static const std::string kAllowed = "._-/#+@ ,=~:%";
#endif
        if (kAllowed.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// Runs `command` and returns its stdout (empty on any failure).
std::string Capture(const std::string& command) {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "rb");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return "";
    }
    std::string out;
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        out.append(buffer, n);
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    return status == 0 ? out : "";
}

}  // namespace

std::vector<std::string> GitFileHistory::PreviousVersions(const std::filesystem::path& file) const {
    const std::string dir = file.parent_path().string();
    const std::string name = file.filename().string();
    if (dir.empty() || !SafeForShell(dir) || !SafeForShell(name) || max_versions_ <= 0) {
        return {};
    }
#ifdef _WIN32
    const std::string quiet = " 2>NUL";
#else
    const std::string quiet = " 2>/dev/null";
#endif
    // One git process per file: every commit's patch with the whole file as
    // context (-U<huge>), so the new side of each patch is that version's
    // full content. \x01 marks the start of each commit.
    const std::string out =
        Capture("git -C \"" + dir + "\" log -n " + std::to_string(max_versions_) +
                " --no-color --no-ext-diff --no-renames --format=%x01 -p -U100000000 -- \"" +
                name + "\"" + quiet);
    std::vector<std::string> versions;
    size_t pos = 0;
    while ((pos = out.find('\x01', pos)) != std::string::npos) {
        const size_t next = out.find('\x01', pos + 1);
        const std::string chunk =
            out.substr(pos + 1, (next == std::string::npos ? out.size() : next) - pos - 1);
        pos = next == std::string::npos ? out.size() : next;
        std::string content;
        bool in_hunk = false;
        bool has_new_side = false;
        bool last_was_new = false;
        size_t start = 0;
        while (start < chunk.size()) {
            size_t end = chunk.find('\n', start);
            if (end == std::string::npos) {
                end = chunk.size();
            }
            const std::string line = chunk.substr(start, end - start);
            start = end + 1;
            if (!in_hunk) {
                in_hunk = line.rfind("@@", 0) == 0;
                continue;
            }
            if (line.empty()) {
                continue;
            }
            if (line[0] == ' ' || line[0] == '+') {
                content += line.substr(1) + "\n";
                has_new_side = true;
                last_was_new = true;
            } else if (line[0] == '-') {
                last_was_new = false;
            } else if (line[0] == '\\') {  // "\ No newline at end of file"
                if (last_was_new && !content.empty()) {
                    content.pop_back();
                }
            }
        }
        if (has_new_side) {  // a commit that deleted the file has none
            versions.push_back(std::move(content));
        }
    }
    return versions;
}

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
        case ObjectAction::Skipped: return "skipped";
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

namespace {

// Lower = more severe; see AbapGitRunReport::MostSevereError.
int SeverityRank(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::Connection:
        case ErrorCategory::Authentication:
        case ErrorCategory::Authorization:
        case ErrorCategory::CsrfToken: return 0;
        case ErrorCategory::Internal: return 1;
        case ErrorCategory::Timeout: return 2;
        case ErrorCategory::TransportError: return 3;
        case ErrorCategory::LockConflict: return 4;
        case ErrorCategory::ActivationError: return 5;
        case ErrorCategory::CheckError: return 6;
        case ErrorCategory::TestFailure: return 7;
        case ErrorCategory::PullError: return 8;
        case ErrorCategory::CloneError: return 9;
        case ErrorCategory::PackageError:
        case ErrorCategory::NotFound: return 10;
    }
    return 1;
}

}  // namespace

std::optional<Error> AbapGitRunReport::MostSevereError() const {
    std::optional<Error> worst;
    for (const auto& o : objects) {
        if (o.error.has_value() &&
            (!worst || SeverityRank(o.error->category) < SeverityRank(worst->category))) {
            worst = o.error;
        }
    }
    return worst;
}

int AbapGitRunReport::ExitCode() const {
    auto worst = MostSevereError();
    return worst ? worst->ExitCode() : 0;
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
        auto normalized = NormalizeFile(name, bytes.Value());
        if (normalized.IsErr()) {
            auto err = normalized.Error();
            err.message = path.string() + ": " + err.message;
            return Result<ObjectFileSet, Error>::Err(err);
        }
        files[name] = normalized.Value();
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
    std::map<std::string, std::shared_ptr<const IClassicTypeHandler>> handlers,
    const IFileHistory& history)
    : session_(session), engine_(engine), handlers_(std::move(handlers)), history_(history) {}

const IClassicTypeHandler* AbapGitWorkflow::Handler(const std::string& type) const {
    auto it = handlers_.find(type);
    return it == handlers_.end() ? nullptr : it->second.get();
}

AbapGitWorkflow::ReadResult AbapGitWorkflow::ReadStates(const std::vector<AbapGitObjectKey>& keys,
                                                        const AbapGitRunOptions& options,
                                                        bool with_packages) {
    ReadResult result;
    // Op 1 is always the system info (logon user, changeable namespaces),
    // so guard rails cost no extra round trip.
    std::vector<EngineOp> ops{SystemInfoOp()};
    std::vector<std::pair<size_t, size_t>> slices;  // offset, count per key
    std::vector<size_t> package_ops;
    for (const auto& key : keys) {
        auto key_ops = Handler(key.type)->ReadOps(key, options.language);
        slices.emplace_back(ops.size(), key_ops.size());
        ops.insert(ops.end(), key_ops.begin(), key_ops.end());
        if (with_packages) {
            package_ops.push_back(ops.size());
            ops.push_back(TadirPackageOp(key));
        }
    }

    if (keys.empty()) {
        return result;
    }
    auto run = engine_.Run(ops);
    if (run.IsErr()) {
        for (size_t i = 0; i < keys.size(); ++i) {
            result.states.push_back(Result<ObjectState, Error>::Err(run.Error()));
        }
        return result;
    }
    std::vector<EngineOpResult> all = std::move(run).Value();
    result.user = all.at(0).changed_by;
    auto ns = all.at(0).values.find("NS");
    if (ns != all.at(0).values.end()) {
        result.namespaces = ns->second;
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<EngineOpResult> slice(
            all.begin() + static_cast<std::ptrdiff_t>(slices[i].first),
            all.begin() + static_cast<std::ptrdiff_t>(slices[i].first + slices[i].second));
        TypeContext ctx{session_, engine_, options.root_package, options.transport,
                        options.language};
        result.states.push_back(Handler(keys[i].type)->BuildState(keys[i], slice, ctx));
        if (with_packages) {
            std::vector<std::string> chain;
            const auto& r = all.at(package_ops[i]);
            auto it = r.values.find("PACKAGE");
            if (it != r.values.end() && !it->second.empty()) {
                std::istringstream words(it->second.front());
                std::string word;
                while (words >> word) {
                    chain.push_back(word);
                }
            }
            result.package_chains.push_back(std::move(chain));
        }
    }
    return result;
}

std::optional<Error> AbapGitWorkflow::CheckAgainstSystem(
    const AbapGitObjectKey& key, const std::string& package,
    const std::vector<std::string>& namespaces) {
    auto customer = CheckCustomerObject(key, namespaces);
    if (customer.IsErr()) {
        return customer.Error();
    }
    auto target = CheckTargetPackage(package, namespaces);
    if (target.IsErr()) {
        return target.Error();
    }
    return std::nullopt;
}

std::optional<Error> AbapGitWorkflow::DetectDrift(const AbapGitObjectFiles& object,
                                                  const ObjectFileSet& desired,
                                                  const ObjectState& current,
                                                  const std::string& user) const {
    // A differing file is safe to overwrite when
    //   - the deployer is provably the last changer of every differing part, or
    //   - the system holds a version from the file's git history (an earlier
    //     deploy, e.g. by the CI user).
    // Anything else - another user, or a part whose last changer SAP does not
    // record (transactions, dynpros) - is someone else's work until shown
    // otherwise, and is refused.
    const auto who = [&](const std::string& file, const std::string& section) {
        if (!section.empty()) {
            auto it = current.part_changed_by.find(file + "#" + section);
            if (it != current.part_changed_by.end()) {
                return it->second;
            }
        }
        auto it = current.part_changed_by.find(file);
        return it != current.part_changed_by.end() ? it->second : current.changed_by;
    };
    for (const auto& [name, bytes] : current.files) {
        auto wanted = desired.find(name);
        if (wanted != desired.end() && wanted->second == bytes) {
            continue;
        }
        // Who last changed the differing part(s) of this file?
        std::vector<std::string> changers;
        bool by_section = false;
        if (wanted != desired.end() && EndsWith(name, ".xml")) {
            auto have = ParseAbapGitXml(bytes);
            auto want = ParseAbapGitXml(wanted->second);
            if (have.IsOk() && want.IsOk()) {
                std::map<std::string, std::string> have_sections;
                std::map<std::string, std::string> want_sections;
                for (auto& [section, xml] : SplitAsxValues(have.Value().values_xml)) {
                    have_sections[section] = xml;
                }
                for (auto& [section, xml] : SplitAsxValues(want.Value().values_xml)) {
                    want_sections[section] = xml;
                }
                std::map<std::string, bool> names;
                for (const auto& [section, xml] : have_sections) {
                    names[section] = true;
                }
                for (const auto& [section, xml] : want_sections) {
                    names[section] = true;
                }
                for (const auto& [section, unused] : names) {
                    auto h = have_sections.find(section);
                    auto w = want_sections.find(section);
                    const bool same = h != have_sections.end() && w != want_sections.end() &&
                                      h->second == w->second;
                    if (!same) {
                        changers.push_back(who(name, section));
                    }
                }
                by_section = !changers.empty();
            }
        }
        if (!by_section) {
            changers.push_back(who(name, ""));
        }
        std::string other;
        for (const auto& changer : changers) {
            if (changer.empty() || !SameUser(changer, user)) {
                other = changer.empty() ? std::string("an unknown user") : changer;
                break;
            }
        }
        if (other.empty()) {
            continue;  // every differing part was last changed by the deployer
        }
        bool known = false;
        for (const auto& version : history_.PreviousVersions(object.folder / name)) {
            auto normalized = NormalizeFile(name, version);
            if (normalized.IsOk() && normalized.Value() == bytes) {
                known = true;
                break;
            }
        }
        if (!known) {
            const bool unknown = other == "an unknown user";
            return MakeError(
                object.key.type + " " + object.key.name + ": " + name +
                    " was last changed in the system by " + other +
                    (unknown ? " (SAP records no last changer for this part)" : "") +
                    (!unknown && !current.changed_on.empty() ? " on " + current.changed_on : "") +
                    ", and that version is not in the git history of the file. Pull it into "
                    "git first with 'erpl-adt abapgit export " + ExportHint(object) +
                    "' and commit it, or overwrite it with --force.",
                ErrorCategory::LockConflict);
        }
    }
    return std::nullopt;
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
        // In a dry run (status, e.g. a pull-request check) a missing
        // --transport is only an annotation, unless --strict.
        const bool transport_fatal = !options.dry_run || options.strict;
        if (auto refused =
                CheckGuardRails(object.key, object.package, transport_fatal, options)) {
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

    // Objects that failed (or were skipped) in this run; later objects that
    // depend on them are not attempted.
    std::vector<AbapGitObjectKey> broken;
    auto failed_dependency = [&](size_t i) -> std::optional<AbapGitObjectKey> {
        for (const auto& key : broken) {
            if (Mentions(desired[i], key.name)) {
                return key;
            }
        }
        return std::nullopt;
    };

    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& object = *eligible[i];
        const auto& state = states.states[i];
        if (state.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, state.Error()));
            continue;
        }
        const auto& current = state.Value();
        if (auto refused = CheckAgainstSystem(object.key, object.package, states.namespaces)) {
            report.objects.push_back(
                FailedOutcome(object.key, object.package, ObjectAction::Refused, *refused));
            continue;
        }
        if (current.exists && current.files == desired[i] && current.pending.empty()) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Unchanged));
            continue;
        }
        // What the handler cannot write is reported the same way by status
        // and deploy, before anything is written.
        auto valid = Handler(object.key.type)->Validate(object.key, desired[i], current,
                                                        options.language);
        if (valid.IsErr()) {
            report.objects.push_back(
                FailedOutcome(object.key, object.package, ObjectAction::Failed, valid.Error()));
            if (!current.exists) {
                broken.push_back(object.key);
            }
            continue;
        }
        if (current.exists && !options.force) {
            const auto& user = options.user.empty() ? states.user : options.user;
            if (auto drift = DetectDrift(object, desired[i], current, user)) {
                report.objects.push_back(
                    FailedOutcome(object.key, object.package, ObjectAction::Refused, *drift));
                continue;
            }
        }
        const auto action = current.exists ? ObjectAction::Updated : ObjectAction::Created;
        if (options.dry_run) {
            auto outcome = Outcome(object.key, object.package, action);
            if (!IsLocalPackage(object.package) && options.transport.empty()) {
                outcome.notes.push_back("package " + object.package +
                                        " is transportable: the deploy needs --transport "
                                        "<request>");
            }
            report.objects.push_back(std::move(outcome));
            continue;
        }
        if (auto dependency = failed_dependency(i)) {
            auto skipped = Outcome(object.key, object.package, ObjectAction::Skipped);
            skipped.message = "dependency " + dependency->type + " " + dependency->name + " failed";
            report.objects.push_back(std::move(skipped));
            broken.push_back(object.key);
            continue;
        }
        const std::string verb = current.exists ? "updating " : "creating ";
        LogInfo(kComponent, verb + object.key.type + " " + object.key.name);
        if (options.progress) {
            options.progress("[" + std::to_string(i + 1) + "/" + std::to_string(eligible.size()) +
                             "] " + verb + object.key.type + " " + object.key.name);
        }
        TypeContext ctx{session_, engine_, object.package, options.transport, options.language};
        auto applied = Handler(object.key.type)->Apply(object.key, desired[i], current, ctx);
        if (applied.IsErr()) {
            report.objects.push_back(FailedOutcome(object.key, object.package,
                                                   ObjectAction::Failed, applied.Error()));
            broken.push_back(object.key);
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
    std::vector<ObjectFileSet> files_on_disk;
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (it->key.type == "DEVC" || Handler(it->key.type) == nullptr) {
            report.skipped.push_back(it->key.type + " " + it->key.name);
            continue;
        }
        if (auto refused = CheckGuardRails(it->key, it->package, true, options)) {
            report.objects.push_back(
                FailedOutcome(it->key, it->package, ObjectAction::Refused, *refused));
            continue;
        }
        // The files are what the caller means to delete; a system version
        // that differs from them (and from their git history) is someone
        // else's work and is not deleted without --force.
        auto files = LoadObjectFiles(*it);
        if (files.IsErr()) {
            report.objects.push_back(
                FailedOutcome(it->key, it->package, ObjectAction::Failed, files.Error()));
            continue;
        }
        eligible.push_back(&*it);
        files_on_disk.push_back(std::move(files).Value());
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
        const auto& current = states.states[i].Value();
        if (auto refused = CheckAgainstSystem(object.key, object.package, states.namespaces)) {
            report.objects.push_back(
                FailedOutcome(object.key, object.package, ObjectAction::Refused, *refused));
            continue;
        }
        if (!current.exists) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Missing));
            continue;
        }
        if (!options.force && current.files != files_on_disk[i]) {
            const auto& user = options.user.empty() ? states.user : options.user;
            if (auto drift = DetectDrift(object, files_on_disk[i], current, user)) {
                report.objects.push_back(
                    FailedOutcome(object.key, object.package, ObjectAction::Refused, *drift));
                continue;
            }
        }
        if (options.dry_run) {
            report.objects.push_back(Outcome(object.key, object.package, ObjectAction::Deleted));
            continue;
        }
        if (options.progress) {
            options.progress("[" + std::to_string(i + 1) + "/" + std::to_string(eligible.size()) +
                             "] deleting " + object.key.type + " " + object.key.name);
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

AbapGitRunReport AbapGitWorkflow::Export(const std::vector<AbapGitObjectFiles>& objects,
                                         const AbapGitExportTarget& target,
                                         const AbapGitRunOptions& options) {
    AbapGitRunReport report;
    std::vector<const AbapGitObjectFiles*> eligible;
    for (const auto& object : objects) {
        if (object.key.type == "DEVC") {
            continue;
        }
        if (Handler(object.key.type) == nullptr) {
            report.skipped.push_back(object.key.type + " " + object.key.name +
                                     " (object type not supported yet)");
            continue;
        }
        const std::string package = object.package.empty() ? options.root_package : object.package;
        if (auto refused = CheckGuardRails(object.key, package, false, options)) {
            report.objects.push_back(
                FailedOutcome(object.key, package, ObjectAction::Refused, *refused));
            continue;
        }
        eligible.push_back(&object);
    }
    std::vector<AbapGitObjectKey> keys;
    for (const auto* object : eligible) {
        keys.push_back(object->key);
    }
    auto states = ReadStates(keys, options, true);
    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& object = *eligible[i];
        const auto& key = object.key;
        const auto& chain = states.package_chains.empty() ? std::vector<std::string>{}
                                                           : states.package_chains[i];
        std::string package = object.package;
        if (package.empty()) {
            package = chain.empty() ? options.root_package : chain.front();
        }
        if (states.states[i].IsErr()) {
            report.objects.push_back(
                FailedOutcome(key, package, ObjectAction::Failed, states.states[i].Error()));
            continue;
        }
        if (auto refused = CheckAgainstSystem(key, package, states.namespaces)) {
            report.objects.push_back(FailedOutcome(key, package, ObjectAction::Refused, *refused));
            continue;
        }
        const auto& state = states.states[i].Value();
        if (!state.exists) {
            auto o = Outcome(key, package, ObjectAction::Missing);
            o.message = key.type + " " + key.name + " does not exist in the system";
            o.error = MakeError(o.message, ErrorCategory::NotFound);
            report.objects.push_back(std::move(o));
            continue;
        }

        // Where the files go: the scanned folder, or the folder of the
        // object's TADIR package below the repository root.
        std::filesystem::path folder = object.folder;
        if (folder.empty()) {
            auto relative =
                FolderInRepo(chain, options.root_package, target.layout.folder_logic);
            if (relative.IsErr()) {
                auto err = relative.Error();
                err.message = key.type + " " + key.name + " cannot be exported here: " +
                              err.message;
                report.objects.push_back(FailedOutcome(key, package, ObjectAction::Refused, err));
                continue;
            }
            folder = target.repo_root / StartingFolder(target.layout) / relative.Value();
        }

        auto outcome = Outcome(key, package, ObjectAction::Exported);
        for (const auto& [name, bytes] : state.files) {
            outcome.parts.push_back(name);
            if (options.dry_run) {
                continue;
            }
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
            std::ofstream out(folder / name, std::ios::binary | std::ios::trunc);
            out << bytes;
            if (!out) {
                outcome.action = ObjectAction::Failed;
                outcome.message = "Cannot write " + (folder / name).string();
                outcome.error = MakeError(outcome.message, ErrorCategory::Internal);
                break;
            }
        }
        // Parts the system no longer has: the object's other files in the
        // folder (same stem), removed only after every new file is written.
        if (outcome.action == ObjectAction::Exported) {
            const std::string prefix = ObjectFileStem(key) + ".";
            std::error_code ec;
            std::vector<std::filesystem::path> obsolete;
            for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
                 it.increment(ec)) {
                const std::string name = it->path().filename().string();
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (it->is_regular_file() && lower.rfind(prefix, 0) == 0 &&
                    state.files.count(name) == 0) {
                    obsolete.push_back(it->path());
                }
            }
            std::sort(obsolete.begin(), obsolete.end());
            for (const auto& path : obsolete) {
                if (!options.dry_run && !std::filesystem::remove(path, ec)) {
                    outcome.action = ObjectAction::Failed;
                    outcome.message = "Cannot remove " + path.string();
                    outcome.error = MakeError(outcome.message, ErrorCategory::Internal);
                    break;
                }
                outcome.parts.push_back("removed " + path.filename().string());
            }
        }
        report.objects.push_back(std::move(outcome));
    }
    return report;
}

}  // namespace erpl_adt
