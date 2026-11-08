#include <erpl_adt/adt/abapgit_repo.hpp>

#include <erpl_adt/core/types.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

namespace erpl_adt {

namespace {

namespace fs = std::filesystem;
using FolderLogic = AbapGitLayout::FolderLogic;

constexpr const char* kScanOperation = "abapGit repository scan";
constexpr const char* kGuardOperation = "abapGit guard rail";
constexpr std::size_t kMaxPackageNameLength = 30;

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------

std::string Upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string ReplaceChar(std::string s, char from, char to) {
    std::replace(s.begin(), s.end(), from, to);
    return s;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

Error MakeError(const char* operation, std::string message, ErrorCategory category) {
    Error e;
    e.operation = operation;
    e.message = std::move(message);
    e.category = category;
    return e;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Inverse of NameEscape — abapGit uses cl_http_utility=>unescape_url.
// A '%' that does not start a valid escape is kept literally.
std::string PercentUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = HexValue(s[i + 1]);
            const int lo = HexValue(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// ZCL_ABAPGIT_FILENAME_LOGIC=>NAME_ESCAPE. abapGit escapes '#' before '%',
// which would double-escape a '#'; '%' goes first here so that every escape
// round-trips. ('#' cannot occur in an ABAP object name, so the two agree on
// every real name.)
std::string NameEscape(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        switch (c) {
            case '%': out += "%25"; break;
            case '#': out += "%23"; break;
            case '.': out += "%2e"; break;
            case '<': out += "%3c"; break;
            case '=': out += "%3d"; break;
            case '>': out += "%3e"; break;
            case '?': out += "%3f"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// ABAP `CP` (covers pattern): '*' any sequence, '+' exactly one character,
// case-insensitive. abapGit escapes '#' in the pattern, so '#' is literal.
bool CoversPattern(std::string_view text, std::string_view pattern) {
    std::size_t t = 0;
    std::size_t p = 0;
    std::optional<std::size_t> star_p;
    std::size_t star_t = 0;
    auto eq = [](char a, char b) {
        return std::toupper(static_cast<unsigned char>(a)) ==
               std::toupper(static_cast<unsigned char>(b));
    };
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '+' || eq(pattern[p], text[t]))) {
            ++t;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_t = t;
        } else if (star_p) {
            p = *star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

std::string NormalizeStartingFolder(std::string folder) {
    folder = ReplaceChar(std::move(folder), '\\', '/');
    if (folder.empty() || folder.front() != '/') {
        folder.insert(folder.begin(), '/');
    }
    if (folder.back() != '/') {
        folder.push_back('/');
    }
    return folder;
}

// ZCL_ABAPGIT_DOT_ABAPGIT->IS_IGNORED, minus the data-config folder exception.
bool IsIgnored(const std::string& repo_path, const AbapGitLayout& layout,
               const std::string& starting_folder) {
    bool ignored = starting_folder != "/" && !StartsWith(repo_path, starting_folder);
    for (const auto& pattern : layout.ignore) {
        if (pattern.empty()) {
            continue;
        }
        if (CoversPattern(repo_path, pattern)) {
            ignored = true;
        }
        if (pattern.front() == '!' && CoversPattern(repo_path, pattern.substr(1))) {
            ignored = false;
        }
    }
    return ignored;
}

// ZCL_ABAPGIT_FILENAME_LOGIC=>FILE_TO_OBJECT:
//   SPLIT filename AT '.' INTO name type ext  (ext keeps the remainder)
//   '#' -> '/', unescape %xx, upper-case name and type.
// An object definition needs a 4-character type and a non-empty remainder.
struct ParsedFileName {
    AbapGitObjectKey key;
    std::string part;   // lower-cased remainder after "<name>.<type>."
};

std::optional<ParsedFileName> ParseFileName(const std::string& filename) {
    const auto first = filename.find('.');
    if (first == std::string::npos || first == 0) {
        return std::nullopt;
    }
    const auto second = filename.find('.', first + 1);
    if (second == std::string::npos || second + 1 >= filename.size()) {
        return std::nullopt;
    }
    const std::string raw_type = filename.substr(first + 1, second - first - 1);
    if (raw_type.size() != 4 ||
        !std::all_of(raw_type.begin(), raw_type.end(),
                     [](unsigned char c) { return std::isalnum(c) != 0; })) {
        return std::nullopt;
    }
    ParsedFileName parsed;
    parsed.key.type = Upper(raw_type);
    parsed.key.name = Upper(PercentUnescape(ReplaceChar(filename.substr(0, first), '#', '/')));
    parsed.part = Lower(filename.substr(second + 1));
    return parsed;
}

Result<std::string, Error> ValidateRootPackage(const std::string& root_package) {
    const std::string root = Upper(root_package);
    auto valid = PackageName::Create(root);
    if (valid.IsErr()) {
        return Result<std::string, Error>::Err(MakeError(
            kScanOperation, "Root package '" + root_package + "': " + valid.Error(),
            ErrorCategory::PackageError));
    }
    return Result<std::string, Error>::Ok(root);
}

// ZCL_ABAPGIT_FOLDER_LOGIC->PATH_TO_PACKAGE for the folder segments below the
// starting folder.
Result<std::string, Error> PathToPackage(const std::vector<std::string>& segments,
                                         const std::string& root, FolderLogic logic,
                                         const std::string& folder_for_messages) {
    std::string package = root;
    std::set<std::string> seen{root};
    for (const auto& segment : segments) {
        std::string next;
        if (logic == FolderLogic::Full) {
            next = ReplaceChar(segment, '#', '/');
            if (root.front() == '$') {
                next.insert(next.begin(), '$');
            }
        } else {
            next = package + "_" + segment;
        }
        next = Upper(next);
        if (next.size() > kMaxPackageNameLength) {
            return Result<std::string, Error>::Err(MakeError(
                kScanOperation,
                "Package " + next + " (folder " + folder_for_messages +
                    ") exceeds the ABAP 30-character name limit",
                ErrorCategory::PackageError));
        }
        auto valid = PackageName::Create(next);
        if (valid.IsErr()) {
            return Result<std::string, Error>::Err(MakeError(
                kScanOperation,
                "Folder " + folder_for_messages + " maps to invalid package " + next + ": " +
                    valid.Error(),
                ErrorCategory::PackageError));
        }
        if (!seen.insert(next).second) {
            return Result<std::string, Error>::Err(MakeError(
                kScanOperation,
                "Package " + next + " (folder " + folder_for_messages +
                    ") has a sub-package with the same name",
                ErrorCategory::PackageError));
        }
        package = next;
    }
    return Result<std::string, Error>::Ok(package);
}

std::vector<std::string> SplitFolder(std::string_view relative) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start < relative.size()) {
        auto slash = relative.find('/', start);
        if (slash == std::string_view::npos) {
            slash = relative.size();
        }
        if (slash > start) {
            segments.emplace_back(relative.substr(start, slash - start));
        }
        start = slash + 1;
    }
    return segments;
}

std::string KeyText(const AbapGitObjectKey& key) {
    return key.type + " " + key.name;
}

Result<void, Error> AddPart(AbapGitObjectFiles& object, const std::string& part,
                            const fs::path& file) {
    auto [it, inserted] = object.files.emplace(part, file);
    if (!inserted) {
        return Result<void, Error>::Err(MakeError(
            kScanOperation,
            "Object " + KeyText(object.key) + " has contradictory files for part '" + part +
                "': " + it->second.filename().string() + " and " + file.filename().string(),
            ErrorCategory::PackageError));
    }
    return Result<void, Error>::Ok();
}

bool IsRepoConfigFile(const std::string& repo_path) {
    return repo_path == "/.abapgit.xml" || repo_path == "/.apack-manifest.xml";
}

// "/NS/NAME" -> "/NS/"; nullopt when malformed (no closing slash, empty
// namespace or nothing after it).

std::string NormalizeNamespace(const std::string& ns) {
    std::string out = Upper(ns);
    if (out.empty() || out.front() != '/') {
        out.insert(out.begin(), '/');
    }
    if (out.back() != '/' || out.size() == 1) {
        out.push_back('/');
    }
    return out;
}

bool IsCustomerNamespace(const std::string& ns,
                         const std::vector<std::string>& customer_namespaces) {
    return std::any_of(customer_namespaces.begin(), customer_namespaces.end(),
                       [&](const std::string& candidate) {
                           const std::string normalized = NormalizeNamespace(candidate);
                           return normalized.size() > 2 && normalized == ns;
                       });
}

std::string JoinNamespaces(const std::vector<std::string>& customer_namespaces) {
    if (customer_namespaces.empty()) {
        return "none";
    }
    std::string out;
    for (const auto& ns : customer_namespaces) {
        if (!out.empty()) {
            out += ", ";
        }
        out += NormalizeNamespace(ns);
    }
    return out;
}

// Shared Y/Z/namespace rule; `what` names the subject in messages.
Result<void, Error> CheckCustomerName(const std::string& name, const std::string& what,
                                      const std::vector<std::string>& customer_namespaces) {
    if (name.front() == '/') {
        const auto ns = AbapNamespaceOf(name);
        if (!ns) {
            return Result<void, Error>::Err(MakeError(
                kGuardOperation,
                what + " has a malformed namespace (expected /NAMESPACE/NAME)",
                ErrorCategory::Authorization));
        }
        if (!IsCustomerNamespace(*ns, customer_namespaces)) {
            return Result<void, Error>::Err(MakeError(
                kGuardOperation,
                what + " is in namespace " + *ns +
                    ", which is not a customer namespace changeable in this system "
                    "(customer namespaces: " +
                    JoinNamespaces(customer_namespaces) + ")",
                ErrorCategory::Authorization));
        }
        return Result<void, Error>::Ok();
    }
    if (name.front() == 'Y' || name.front() == 'Z') {
        return Result<void, Error>::Ok();
    }
    return Result<void, Error>::Err(MakeError(
        kGuardOperation,
        what + " is not in the customer name range: names must start with Y or Z, "
               "or with a customer namespace /NS/",
        ErrorCategory::Authorization));
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

bool operator==(const AbapGitObjectKey& lhs, const AbapGitObjectKey& rhs) {
    return lhs.type == rhs.type && lhs.name == rhs.name;
}

bool operator!=(const AbapGitObjectKey& lhs, const AbapGitObjectKey& rhs) {
    return !(lhs == rhs);
}

int DeployRank(std::string_view type) {
    const std::string t = Upper(type);
    if (t == "DEVC") return 0;
    if (t == "DOMA") return 10;
    if (t == "DTEL") return 20;
    if (t == "TABL") return 30;
    if (t == "TTYP") return 35;
    if (t == "FUGR") return 40;
    if (t == "CLAS" || t == "INTF") return 45;
    if (t == "PROG") return 50;
    if (t == "TRAN") return 60;
    return -1;
}

bool IsSupportedType(std::string_view type) {
    const std::string t = Upper(type);
    return t == "DEVC" || t == "DOMA" || t == "DTEL" || t == "FUGR" || t == "PROG" ||
           t == "TRAN";
}

std::string ObjectFileStem(const AbapGitObjectKey& key) {
    const std::string type = Lower(key.type);
    if (type == "devc") {
        // ZCL_ABAPGIT_OBJECT_DEVC->MAP_OBJECT_TO_FILENAME: fixed name.
        return "package.devc";
    }
    return ReplaceChar(Lower(NameEscape(key.name)), '/', '#') + "." + type;
}

Result<std::string, Error> PackageFolderName(const std::string& package,
                                             const std::string& parent_package,
                                             FolderLogic logic) {
    const std::string child = Upper(package);
    const std::string parent = Upper(parent_package);
    auto fail = [&](const std::string& why) {
        return Result<std::string, Error>::Err(
            MakeError(kScanOperation,
                      "Cannot derive a folder for package '" + package + "' under '" +
                          parent_package + "': " + why,
                      ErrorCategory::PackageError));
    };
    if (child.empty() || parent.empty()) {
        return fail("package name is empty");
    }
    if (child == parent) {
        return fail("it is the parent package itself");
    }

    // ZCL_ABAPGIT_FOLDER_LOGIC->PACKAGE_TO_PATH
    std::size_t len = 0;
    if (logic == FolderLogic::Full) {
        const bool child_local = child.front() == '$';
        const bool parent_local = parent.front() == '$';
        if (child_local != parent_local) {
            return fail("local and transportable packages cannot be mixed under FULL folder logic");
        }
        len = child_local ? 1 : 0;
    } else {
        if (!StartsWith(child, parent)) {
            return fail("PREFIX folder logic needs sub-packages named " + parent +
                        "_<folder>; try FULL folder logic");
        }
        len = parent.size();
    }
    std::string rest = child.substr(len);
    if (!rest.empty() && rest.front() == '_') {
        rest.erase(rest.begin());
    }
    if (rest.empty()) {
        return fail("the folder name would be empty");
    }
    return Result<std::string, Error>::Ok(Lower(ReplaceChar(rest, '/', '#')));
}

Result<std::string, Error> FolderForPackage(const std::string& package,
                                            const std::string& root_package,
                                            FolderLogic logic) {
    if (!package.empty() && Upper(package) == Upper(root_package)) {
        return Result<std::string, Error>::Ok(std::string{});
    }
    auto name = PackageFolderName(package, root_package, logic);
    if (name.IsErr()) {
        return name;
    }
    return Result<std::string, Error>::Ok(name.Value() + "/");
}

Result<AbapGitScan, Error> ScanAbapGitFolder(const fs::path& repo_root,
                                             const AbapGitLayout& layout,
                                             const std::string& root_package) {
    std::error_code ec;
    if (!fs::is_directory(repo_root, ec)) {
        return Result<AbapGitScan, Error>::Err(MakeError(
            kScanOperation, "Repository folder " + repo_root.string() + " does not exist",
            ErrorCategory::NotFound));
    }
    auto root_result = ValidateRootPackage(root_package);
    if (root_result.IsErr()) {
        return Result<AbapGitScan, Error>::Err(root_result.Error());
    }
    const std::string root = root_result.Value();
    const fs::path base = fs::absolute(repo_root, ec).lexically_normal();
    const std::string starting_folder = NormalizeStartingFolder(layout.starting_folder);

    // Collect files (sorted for a deterministic result), never entering .git.
    std::vector<std::pair<std::string, fs::path>> files;   // repo path, absolute path
    fs::recursive_directory_iterator it(base, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code type_ec;
        if (entry.is_directory(type_ec)) {
            if (entry.path().filename() == ".git") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(type_ec)) {
            continue;
        }
        const fs::path absolute = entry.path().lexically_normal();
        files.emplace_back("/" + absolute.lexically_relative(base).generic_string(), absolute);
    }
    if (ec) {
        return Result<AbapGitScan, Error>::Err(MakeError(
            kScanOperation, "Cannot read " + base.string() + ": " + ec.message(),
            ErrorCategory::Internal));
    }
    std::sort(files.begin(), files.end());

    AbapGitScan scan;
    std::map<std::pair<std::string, std::string>, std::size_t> index;   // (type, name)
    std::map<std::string, std::string> folder_packages;                 // folder -> package

    for (const auto& [repo_path, absolute] : files) {
        if (IsRepoConfigFile(repo_path)) {
            continue;
        }
        if (IsIgnored(repo_path, layout, starting_folder) ||
            !StartsWith(repo_path, starting_folder)) {
            scan.ignored.push_back(repo_path);
            continue;
        }
        auto parsed = ParseFileName(absolute.filename().string());
        if (!parsed) {
            scan.ignored.push_back(repo_path);
            continue;
        }
        if (!IsSupportedType(parsed->key.type)) {
            scan.unsupported.push_back(repo_path);
            continue;
        }

        const std::string folder = repo_path.substr(0, repo_path.rfind('/') + 1);
        auto known = folder_packages.find(folder);
        if (known == folder_packages.end()) {
            auto package = PathToPackage(SplitFolder(folder.substr(starting_folder.size())),
                                         root, layout.folder_logic, folder);
            if (package.IsErr()) {
                return Result<AbapGitScan, Error>::Err(package.Error());
            }
            known = folder_packages.emplace(folder, package.Value()).first;
        }
        const std::string& package = known->second;

        AbapGitObjectKey key = parsed->key;
        if (key.type == "DEVC") {
            // ZCL_ABAPGIT_OBJECT_DEVC->MAP_FILENAME_TO_OBJECT: always "package".
            if (key.name != "PACKAGE") {
                return Result<AbapGitScan, Error>::Err(MakeError(
                    kScanOperation,
                    "Unexpected package file " + repo_path + ": expected package.devc.xml",
                    ErrorCategory::PackageError));
            }
            key.name = package;
        }

        const auto map_key = std::make_pair(key.type, key.name);
        auto found = index.find(map_key);
        if (found == index.end()) {
            AbapGitObjectFiles object;
            object.key = key;
            object.package = package;
            object.folder = absolute.parent_path();
            scan.objects.push_back(std::move(object));
            found = index.emplace(map_key, scan.objects.size() - 1).first;
        }
        AbapGitObjectFiles& object = scan.objects[found->second];
        if (object.folder != absolute.parent_path()) {
            return Result<AbapGitScan, Error>::Err(MakeError(
                kScanOperation,
                "Object " + KeyText(key) + " is serialized in two folders: " +
                    object.folder.lexically_relative(base).generic_string() + " and " +
                    absolute.parent_path().lexically_relative(base).generic_string(),
                ErrorCategory::PackageError));
        }
        auto added = AddPart(object, parsed->part, absolute);
        if (added.IsErr()) {
            return Result<AbapGitScan, Error>::Err(added.Error());
        }
    }

    std::stable_sort(scan.objects.begin(), scan.objects.end(),
                     [](const AbapGitObjectFiles& a, const AbapGitObjectFiles& b) {
                         return std::make_tuple(DeployRank(a.key.type), std::cref(a.package),
                                                std::cref(a.key.name)) <
                                std::make_tuple(DeployRank(b.key.type), std::cref(b.package),
                                                std::cref(b.key.name));
                     });
    return Result<AbapGitScan, Error>::Ok(std::move(scan));
}

Result<AbapGitObjectFiles, Error> ScanAbapGitObject(const fs::path& any_file_of_object,
                                                    const std::string& root_package) {
    std::error_code ec;
    if (!fs::is_regular_file(any_file_of_object, ec)) {
        return Result<AbapGitObjectFiles, Error>::Err(MakeError(
            kScanOperation, "File " + any_file_of_object.string() + " does not exist",
            ErrorCategory::NotFound));
    }
    const std::string filename = any_file_of_object.filename().string();
    auto parsed = ParseFileName(filename);
    if (!parsed) {
        return Result<AbapGitObjectFiles, Error>::Err(MakeError(
            kScanOperation,
            filename + " is not an abapGit object file (expected <name>.<type>.<ext>)",
            ErrorCategory::NotFound));
    }
    auto root_result = ValidateRootPackage(root_package);
    if (root_result.IsErr()) {
        return Result<AbapGitObjectFiles, Error>::Err(root_result.Error());
    }

    AbapGitObjectFiles object;
    object.key = parsed->key;
    object.package = root_result.Value();
    object.folder = fs::absolute(any_file_of_object, ec).lexically_normal().parent_path();
    if (object.key.type == "DEVC") {
        object.key.name = object.package;
    }

    std::vector<fs::path> siblings;
    for (fs::directory_iterator it(object.folder, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_regular_file(type_ec)) {
            siblings.push_back(it->path());
        }
    }
    if (ec) {
        return Result<AbapGitObjectFiles, Error>::Err(MakeError(
            kScanOperation, "Cannot read " + object.folder.string() + ": " + ec.message(),
            ErrorCategory::Internal));
    }
    std::sort(siblings.begin(), siblings.end());
    for (const auto& sibling : siblings) {
        auto other = ParseFileName(sibling.filename().string());
        if (!other || other->key != parsed->key) {
            continue;
        }
        auto added = AddPart(object, other->part, sibling);
        if (added.IsErr()) {
            return Result<AbapGitObjectFiles, Error>::Err(added.Error());
        }
    }
    return Result<AbapGitObjectFiles, Error>::Ok(std::move(object));
}

bool IsLocalPackage(std::string_view package) {
    return !package.empty() && package.front() == '$';
}

std::optional<std::string> AbapNamespaceOf(const std::string& name) {
    if (name.empty() || name.front() != '/') {
        return std::nullopt;
    }
    const auto second = name.find('/', 1);
    if (second == std::string::npos || second == 1 || second + 1 >= name.size()) {
        return std::nullopt;
    }
    return name.substr(0, second + 1);
}

Result<void, Error> CheckTargetPackage(const std::string& package,
                                       const std::vector<std::string>& customer_namespaces) {
    const std::string name = Upper(package);
    if (name.empty()) {
        return Result<void, Error>::Err(MakeError(
            kGuardOperation, "Target package name is empty", ErrorCategory::Authorization));
    }
    if (IsLocalPackage(name)) {
        if (name.size() == 1) {
            return Result<void, Error>::Err(MakeError(
                kGuardOperation, "Target package '$' is not a package name",
                ErrorCategory::Authorization));
        }
        return Result<void, Error>::Ok();
    }
    return CheckCustomerName(name, "Package " + name, customer_namespaces);
}

Result<void, Error> CheckCustomerObject(const AbapGitObjectKey& key,
                                        const std::vector<std::string>& customer_namespaces) {
    const std::string type = Upper(key.type);
    const std::string name = Upper(key.name);
    if (type == "DEVC") {
        return CheckTargetPackage(name, customer_namespaces);
    }
    if (name.empty()) {
        return Result<void, Error>::Err(MakeError(
            kGuardOperation, "Object " + type + " has an empty name",
            ErrorCategory::Authorization));
    }
    return CheckCustomerName(name, "Object " + type + " " + name, customer_namespaces);
}

}  // namespace erpl_adt
