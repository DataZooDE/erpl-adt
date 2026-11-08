#pragma once

#include <erpl_adt/core/result.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// abapGit repository scanner — maps an abapGit-serialized folder tree to the
// objects it contains, the packages they belong to, and a deploy order.
//
// Pure and SAP-independent: it reads the file system only, never the
// content of the files, and never talks to a system. The rules follow
// abapGit's own implementation:
//   - file name <-> object: ZCL_ABAPGIT_FILENAME_LOGIC
//     https://github.com/abapGit/abapGit/blob/main/src/objects/core/zcl_abapgit_filename_logic.clas.abap
//   - folder <-> package:   ZCL_ABAPGIT_FOLDER_LOGIC
//     https://github.com/abapGit/abapGit/blob/main/src/objects/core/zcl_abapgit_folder_logic.clas.abap
//   - ignored files:        ZCL_ABAPGIT_DOT_ABAPGIT->IS_IGNORED
//     https://github.com/abapGit/abapGit/blob/main/src/repo/zcl_abapgit_dot_abapgit.clas.abap
// ---------------------------------------------------------------------------

// The part of `.abapgit.xml` the scanner needs. Reading the XML is someone
// else's job; the caller passes the already-parsed values in.
struct AbapGitLayout {
    // PREFIX: sub-folder `foo` of package ZROOT is package ZROOT_FOO.
    // FULL:   sub-folder name is the package name ('#' for '/').
    // (abapGit's MIXED logic is not supported.)
    enum class FolderLogic { Prefix, Full };

    std::string starting_folder = "/src/";
    FolderLogic folder_logic = FolderLogic::Prefix;
    // Patterns as in `.abapgit.xml` (abapGit CP semantics: '*' any sequence,
    // '+' one character, case-insensitive; a leading '!' re-includes).
    // Matched against the repo-relative path with a leading '/'.
    std::vector<std::string> ignore;
};

// Object identity, upper case: {"PROG","ZFOO"}, {"TRAN","/NS/TC"}.
struct AbapGitObjectKey {
    std::string type;
    std::string name;
};

bool operator==(const AbapGitObjectKey& lhs, const AbapGitObjectKey& rhs);
bool operator!=(const AbapGitObjectKey& lhs, const AbapGitObjectKey& rhs);

struct AbapGitObjectFiles {
    AbapGitObjectKey key;
    std::string package;            // target package (upper case)
    std::filesystem::path folder;   // absolute folder holding the files
    // All files of the object, keyed by the lower-cased part of the file name
    // after "<name>.<type>.", e.g. "xml", "abap", "screen_0100.abap",
    // "lzfgtop.abap", "lzfgtop.xml", "z_my_fm.abap", "locals_imp.abap".
    std::map<std::string, std::filesystem::path> files;
};

struct AbapGitScan {
    // Sorted by DeployRank, then package, then name.
    std::vector<AbapGitObjectFiles> objects;
    // Repo-relative paths (leading '/') that were skipped: ignore list hits,
    // files outside the starting folder, files that are not object files.
    std::vector<std::string> ignored;
    // Repo-relative paths (leading '/') of object files whose type cannot be
    // deployed (yet). Reported, not fatal.
    std::vector<std::string> unsupported;
};

// Walk `repo_root` and collect the objects below the starting folder.
// Errors: repo_root missing (NotFound); a folder that does not map to a valid
// package, or one object spread over contradictory files (PackageError).
[[nodiscard]] Result<AbapGitScan, Error> ScanAbapGitFolder(
    const std::filesystem::path& repo_root,
    const AbapGitLayout& layout,
    const std::string& root_package);

// Single-file mode: given any file of an object (e.g. zfoo.prog.xml), collect
// that object's files from the same folder. The package is `root_package`.
// The type is not checked against IsSupportedType — the caller decides.
[[nodiscard]] Result<AbapGitObjectFiles, Error> ScanAbapGitObject(
    const std::filesystem::path& any_file_of_object,
    const std::string& root_package);

// Single-file mode inside a repository: scans `repo_root` (the folder of the
// governing .abapgit.xml) and returns the object `any_file_of_object` belongs
// to, so folder logic, starting folder and ignore list apply exactly as for a
// whole-repository run. Err (NotFound) when the file is not one of the
// repository's objects (ignored, outside the starting folder, no object file).
[[nodiscard]] Result<AbapGitObjectFiles, Error> ScanAbapGitObjectInRepo(
    const std::filesystem::path& any_file_of_object,
    const std::filesystem::path& repo_root,
    const AbapGitLayout& layout,
    const std::string& root_package);

// Deploy order: DEVC 0, DOMA 10, DTEL 20, TABL 30, TTYP 35, FUGR 40,
// CLAS/INTF 45, PROG 50, TRAN 60; unknown -> -1.
[[nodiscard]] int DeployRank(std::string_view type);

// Types the deploy can handle: DEVC (informational), DOMA, DTEL, FUGR, PROG, TRAN.
[[nodiscard]] bool IsSupportedType(std::string_view type);

// File name stem of an object: "zfoo.prog", "#ns#tc.tran", "package.devc".
[[nodiscard]] std::string ObjectFileStem(const AbapGitObjectKey& key);

// Folder name (one path segment, no slash) that `package` gets as a direct
// sub-package of `parent_package` — the inverse of the folder logic
// (abapGit's PACKAGE_TO_PATH). Err when the name cannot be derived, e.g. a
// PREFIX child that does not start with its parent's name.
[[nodiscard]] Result<std::string, Error> PackageFolderName(
    const std::string& package,
    const std::string& parent_package,
    AbapGitLayout::FolderLogic logic);

// Folder relative to the starting folder that `package` maps to, assuming it
// is `root_package` itself ("") or a direct sub-package of it ("foo/").
// Deeper hierarchies compose: FolderForPackage(parent, root) +
// FolderForPackage(child, parent).
[[nodiscard]] Result<std::string, Error> FolderForPackage(
    const std::string& package,
    const std::string& root_package,
    AbapGitLayout::FolderLogic logic);

// ---------------------------------------------------------------------------
// Guard rails — customer objects only.
// ---------------------------------------------------------------------------

// Allowed: names starting with Y or Z, or with a namespace "/XYZ/" listed in
// `customer_namespaces` (with or without the slashes, any case). A DEVC key
// is checked with CheckTargetPackage. Refusals are ErrorCategory::Authorization.
[[nodiscard]] Result<void, Error> CheckCustomerObject(
    const AbapGitObjectKey& key,
    const std::vector<std::string>& customer_namespaces);

// Allowed: local packages ("$..."), Y*/Z*, "/NS/..." with NS a customer
// namespace. SAP packages are refused (ErrorCategory::Authorization).
[[nodiscard]] Result<void, Error> CheckTargetPackage(
    const std::string& package,
    const std::vector<std::string>& customer_namespaces);

// "/ACME/D_STATUS" -> "/ACME/"; nullopt for names outside a namespace or with
// a malformed one ("/", "//X", "/ACME/").
[[nodiscard]] std::optional<std::string> AbapNamespaceOf(const std::string& name);

// Local (non-transportable) package: starts with '$'.
[[nodiscard]] bool IsLocalPackage(std::string_view package);

}  // namespace erpl_adt
