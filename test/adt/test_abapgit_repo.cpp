#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit_repo.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace erpl_adt;
namespace fs = std::filesystem;

namespace {

fs::path FixtureRepo(const std::string& name) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.find_last_of("/\\");
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return fs::path(test_root) / "testdata" / "abapgit" / name;
}

// A scratch folder tree that removes itself.
class TempTree {
public:
    TempTree() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = fs::temp_directory_path() /
                ("erpl_adt_abapgit_repo_" + std::to_string(stamp) + "_" +
                 std::to_string(counter++));
        fs::create_directories(root_);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    const fs::path& Root() const { return root_; }

    fs::path Write(const std::string& relative, const std::string& content = "x") const {
        const fs::path p = root_ / relative;
        fs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary);
        out << content;
        return p;
    }

    void MakeDir(const std::string& relative) const {
        fs::create_directories(root_ / relative);
    }

private:
    fs::path root_;
};

AbapGitLayout PrefixLayout() {
    AbapGitLayout layout;
    layout.starting_folder = "/src/";
    layout.folder_logic = AbapGitLayout::FolderLogic::Prefix;
    return layout;
}

AbapGitLayout FullLayout() {
    AbapGitLayout layout = PrefixLayout();
    layout.folder_logic = AbapGitLayout::FolderLogic::Full;
    return layout;
}

AbapGitScan ScanOk(const fs::path& root, const AbapGitLayout& layout,
                   const std::string& package) {
    auto result = ScanAbapGitFolder(root, layout, package);
    if (result.IsErr()) {
        FAIL("scan failed: " << result.Error().message);
    }
    return result.Value();
}

std::vector<std::string> Keys(const AbapGitScan& scan) {
    std::vector<std::string> out;
    for (const auto& o : scan.objects) {
        out.push_back(o.key.type + " " + o.key.name);
    }
    return out;
}

std::vector<std::string> Parts(const AbapGitObjectFiles& object) {
    std::vector<std::string> out;
    for (const auto& [part, path] : object.files) {
        out.push_back(part);
    }
    return out;
}

const AbapGitObjectFiles* Find(const AbapGitScan& scan, const std::string& type,
                               const std::string& name) {
    for (const auto& o : scan.objects) {
        if (o.key.type == type && o.key.name == name) {
            return &o;
        }
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// Fixture repositories (real abapGit serializations)
// ===========================================================================

TEST_CASE("ScanAbapGitFolder: every fixture repo scans cleanly", "[abapgit_repo]") {
    for (const char* repo : {"DOMA_fixed_values", "DTEL_with_DOMA", "FUGR_with_parameters",
                             "PROG", "PROG_dynp_flow", "PROG_dynp_modi",
                             "PROG_with_text_elmnt", "TRAN", "TRAN_Parameter",
                             "TRAN_Report"}) {
        INFO(repo);
        auto scan = ScanOk(FixtureRepo(repo), PrefixLayout(), "ZROOT");
        CHECK(scan.ignored.empty());   // .abapgit.xml is config, not "ignored"
        REQUIRE_FALSE(scan.objects.empty());
        // package.devc.xml -> the root package itself, ranked first
        CHECK(scan.objects.front().key == AbapGitObjectKey{"DEVC", "ZROOT"});
        CHECK(scan.objects.front().package == "ZROOT");
        CHECK(scan.objects.front().files.count("xml") == 1);
        for (const auto& o : scan.objects) {
            CHECK(o.package == "ZROOT");
            CHECK(o.folder.is_absolute());
            CHECK(fs::equivalent(o.folder, FixtureRepo(repo) / "src"));
            for (const auto& [part, path] : o.files) {
                CHECK(fs::exists(path));
            }
        }
    }
}

TEST_CASE("ScanAbapGitFolder: PROG_dynp_flow has two programs with screens",
          "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("PROG_dynp_flow"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{
                            "DEVC ZROOT",
                            "PROG ZDEMO_DYNPRO_SPLITTER_CONTROL",
                            "PROG ZDEMO_SPLITTER_SUBSCREEN"});
    const auto* control = Find(scan, "PROG", "ZDEMO_DYNPRO_SPLITTER_CONTROL");
    REQUIRE(control != nullptr);
    CHECK(Parts(*control) == std::vector<std::string>{
                                 "abap", "screen_0100.abap", "screen_0110.abap",
                                 "screen_0120.abap", "xml"});
    CHECK(control->files.at("screen_0110.abap").filename() ==
          "zdemo_dynpro_splitter_control.prog.screen_0110.abap");
    const auto* sub = Find(scan, "PROG", "ZDEMO_SPLITTER_SUBSCREEN");
    REQUIRE(sub != nullptr);
    CHECK(Parts(*sub) == std::vector<std::string>{
                             "abap", "screen_0100.abap", "screen_0110.abap",
                             "screen_0120.abap", "screen_0200.abap", "xml"});
    CHECK(scan.unsupported.empty());
}

TEST_CASE("ScanAbapGitFolder: FUGR_with_parameters groups includes and FMs",
          "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("FUGR_with_parameters"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT",
                                                 "FUGR ZAGTEST_FUNCTION_GROUP"});
    const auto* fugr = Find(scan, "FUGR", "ZAGTEST_FUNCTION_GROUP");
    REQUIRE(fugr != nullptr);
    CHECK(Parts(*fugr) == std::vector<std::string>{
                              "lzagtest_function_grouptop.abap",
                              "lzagtest_function_grouptop.xml",
                              "saplzagtest_function_group.abap",
                              "saplzagtest_function_group.xml",
                              "xml",
                              "zagtest_function_module.abap"});
}

TEST_CASE("ScanAbapGitFolder: TRAN_Report deploys the program before the transaction",
          "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("TRAN_Report"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT", "PROG ZTRANSACTION_REPORT",
                                                 "TRAN ZTRANSACTION_REPORT"});
    CHECK(Parts(*Find(scan, "TRAN", "ZTRANSACTION_REPORT")) ==
          std::vector<std::string>{"xml"});
    CHECK(Parts(*Find(scan, "PROG", "ZTRANSACTION_REPORT")) ==
          std::vector<std::string>{"abap", "xml"});
}

TEST_CASE("ScanAbapGitFolder: TRAN_Parameter orders by rank, then name",
          "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("TRAN_Parameter"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{
                            "DEVC ZROOT", "PROG ZTRANSACTION_REPORT",
                            "TRAN ZTRANSACTION_PARAM", "TRAN ZTRANSACTION_PARAM2",
                            "TRAN ZTRANSACTION_PARAM3", "TRAN ZTRANSACTION_REPORT"});
}

TEST_CASE("ScanAbapGitFolder: DTEL_with_DOMA deploys the domain first", "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("DTEL_with_DOMA"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT", "DOMA ZAGT_TESTING_DOMA",
                                                 "DTEL ZAGT_TESTING_DTEL"});
}

TEST_CASE("ScanAbapGitFolder: PROG_dynp_modi reports the table as unsupported",
          "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("PROG_dynp_modi"), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT", "DTEL ZTEST_FLAG",
                                                 "PROG ZTEST_ISSUE1"});
    CHECK(scan.unsupported == std::vector<std::string>{"/src/ztest_db_tab.tabl.xml"});
    CHECK(Parts(*Find(scan, "PROG", "ZTEST_ISSUE1")) ==
          std::vector<std::string>{"abap", "screen_0100.abap", "xml"});
}

TEST_CASE("ScanAbapGitFolder: root package is upper-cased", "[abapgit_repo]") {
    auto scan = ScanOk(FixtureRepo("PROG"), PrefixLayout(), "zroot");
    CHECK(scan.objects.front().key == AbapGitObjectKey{"DEVC", "ZROOT"});
    for (const auto& o : scan.objects) {
        CHECK(o.package == "ZROOT");
    }
}

TEST_CASE("ScanAbapGitFolder: missing repo root is NotFound", "[abapgit_repo]") {
    TempTree tree;
    auto result = ScanAbapGitFolder(tree.Root() / "nope", PrefixLayout(), "ZROOT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::NotFound);
}

// ===========================================================================
// Folder logic
// ===========================================================================

TEST_CASE("ScanAbapGitFolder: PREFIX sub-folders become prefixed sub-packages",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/package.devc.xml");
    tree.Write("src/zroot_prog.prog.abap");
    tree.Write("src/sub/package.devc.xml");
    tree.Write("src/sub/zsub_prog.prog.abap");
    tree.Write("src/sub/deeper/zdeep.prog.abap");

    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT", "DEVC ZROOT_SUB",
                                                 "PROG ZROOT_PROG", "PROG ZSUB_PROG",
                                                 "PROG ZDEEP"});
    CHECK(Find(scan, "PROG", "ZSUB_PROG")->package == "ZROOT_SUB");
    CHECK(Find(scan, "PROG", "ZDEEP")->package == "ZROOT_SUB_DEEPER");
    CHECK(Find(scan, "DEVC", "ZROOT_SUB")->package == "ZROOT_SUB");
    CHECK(fs::equivalent(Find(scan, "PROG", "ZDEEP")->folder,
                         tree.Root() / "src" / "sub" / "deeper"));
}

TEST_CASE("ScanAbapGitFolder: PREFIX with namespaced and local root packages",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/foo/#acme#prog.prog.abap");
    CHECK(Find(ScanOk(tree.Root(), PrefixLayout(), "/ACME/ROOT"), "PROG", "/ACME/PROG")
              ->package == "/ACME/ROOT_FOO");
    CHECK(Find(ScanOk(tree.Root(), PrefixLayout(), "$ZDEMO"), "PROG", "/ACME/PROG")
              ->package == "$ZDEMO_FOO");
}

TEST_CASE("ScanAbapGitFolder: FULL folder names are package names", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zroot_a/zprog_a.prog.abap");
    tree.Write("src/zroot_a/zother_b/zprog_b.prog.abap");
    tree.Write("src/#acme#pkg/zprog_c.prog.abap");

    auto scan = ScanOk(tree.Root(), FullLayout(), "ZROOT");
    CHECK(Find(scan, "PROG", "ZPROG_A")->package == "ZROOT_A");
    CHECK(Find(scan, "PROG", "ZPROG_B")->package == "ZOTHER_B");
    CHECK(Find(scan, "PROG", "ZPROG_C")->package == "/ACME/PKG");
}

TEST_CASE("ScanAbapGitFolder: FULL under a local root prepends '$'", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/demo_sub/zprog.prog.abap");
    auto scan = ScanOk(tree.Root(), FullLayout(), "$ZDEMO");
    CHECK(Find(scan, "PROG", "ZPROG")->package == "$DEMO_SUB");
}

TEST_CASE("ScanAbapGitFolder: package names over 30 characters are refused",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/a_very_long_folder_name_here/zprog.prog.abap");
    auto result = ScanAbapGitFolder(tree.Root(), PrefixLayout(), "ZROOT_PACKAGE");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::PackageError);
    CHECK(result.Error().message.find("30") != std::string::npos);
}

TEST_CASE("ScanAbapGitFolder: FULL sub-package with its parent's name is refused",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zroot/zprog.prog.abap");
    auto result = ScanAbapGitFolder(tree.Root(), FullLayout(), "ZROOT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::PackageError);
}

TEST_CASE("ScanAbapGitFolder: starting folder without slashes is normalized",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("abap/zprog.prog.abap");
    AbapGitLayout layout = PrefixLayout();
    layout.starting_folder = "abap";
    auto scan = ScanOk(tree.Root(), layout, "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZPROG"});
}

TEST_CASE("ScanAbapGitFolder: starting folder '/' takes the whole repo", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("zprog.prog.abap");
    tree.Write("sub/zsub.prog.abap");
    AbapGitLayout layout = PrefixLayout();
    layout.starting_folder = "/";
    auto scan = ScanOk(tree.Root(), layout, "ZROOT");
    CHECK(Find(scan, "PROG", "ZPROG")->package == "ZROOT");
    CHECK(Find(scan, "PROG", "ZSUB")->package == "ZROOT_SUB");
}

// ===========================================================================
// File names
// ===========================================================================

TEST_CASE("ScanAbapGitFolder: namespaced file names decode '#' to '/'", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/#ns#obj.prog.xml");
    tree.Write("src/#ns#obj.prog.abap");
    tree.Write("src/#ns#tc.tran.xml");
    tree.Write("src/#ns#fg.fugr.#ns#fm.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"FUGR /NS/FG", "PROG /NS/OBJ",
                                                 "TRAN /NS/TC"});
    CHECK(Parts(*Find(scan, "FUGR", "/NS/FG")) == std::vector<std::string>{"#ns#fm.abap"});
}

TEST_CASE("ScanAbapGitFolder: percent-escaped characters are decoded", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zfoo%3dbar.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZFOO=BAR"});
}

TEST_CASE("ScanAbapGitFolder: an .abap file without .xml is still an object",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zonly_source.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    REQUIRE(scan.objects.size() == 1);
    CHECK(scan.objects[0].key == AbapGitObjectKey{"PROG", "ZONLY_SOURCE"});
    CHECK(Parts(scan.objects[0]) == std::vector<std::string>{"abap"});
}

TEST_CASE("ScanAbapGitFolder: unsupported types are reported, not fatal", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zcl_x.clas.abap");
    tree.Write("src/zcl_x.clas.xml");
    tree.Write("src/zcl_x.clas.locals_imp.abap");
    tree.Write("src/zif_x.intf.abap");
    tree.Write("src/zprog.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZPROG"});
    CHECK(scan.unsupported == std::vector<std::string>{
                                  "/src/zcl_x.clas.abap", "/src/zcl_x.clas.locals_imp.abap",
                                  "/src/zcl_x.clas.xml", "/src/zif_x.intf.abap"});
}

TEST_CASE("ScanAbapGitFolder: non-object files inside the starting folder are ignored",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/README.md");
    tree.Write("src/notes.txt.bak");
    tree.Write("src/.gitkeep");
    tree.Write("src/zprog.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZPROG"});
    CHECK(scan.ignored == std::vector<std::string>{"/src/.gitkeep", "/src/README.md",
                                                   "/src/notes.txt.bak"});
}

TEST_CASE("ScanAbapGitFolder: files outside the starting folder are ignored",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write(".abapgit.xml");
    tree.Write("README.md");
    tree.Write("LICENSE");
    tree.Write("docs/zdoc.prog.abap");
    tree.Write("src/zprog.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZPROG"});
    CHECK(scan.ignored == std::vector<std::string>{"/LICENSE", "/README.md",
                                                   "/docs/zdoc.prog.abap"});
}

TEST_CASE("ScanAbapGitFolder: the .git directory is not walked", "[abapgit_repo]") {
    TempTree tree;
    tree.Write(".git/HEAD");
    tree.Write(".git/objects/ab/cdef");
    tree.Write("src/zprog.prog.abap");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(scan.ignored.empty());
}

TEST_CASE("ScanAbapGitFolder: ignore list follows abapGit CP patterns", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zkeep.prog.abap");
    tree.Write("src/zskip.prog.abap");
    tree.Write("src/zskip.prog.xml");
    tree.Write("src/generated/zgen1.prog.abap");
    tree.Write("src/generated/zgen2.prog.abap");
    AbapGitLayout layout = PrefixLayout();
    layout.ignore = {"/src/ZSKIP.prog.*", "/src/generated/*", "!/src/generated/zgen2.prog.++++",
                     ""};
    auto scan = ScanOk(tree.Root(), layout, "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"PROG ZKEEP", "PROG ZGEN2"});
    CHECK(scan.ignored == std::vector<std::string>{"/src/generated/zgen1.prog.abap",
                                                   "/src/zskip.prog.abap",
                                                   "/src/zskip.prog.xml"});
}

TEST_CASE("ScanAbapGitFolder: an empty starting folder yields nothing", "[abapgit_repo]") {
    TempTree tree;
    tree.MakeDir("src");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(scan.objects.empty());
    CHECK(scan.ignored.empty());
    CHECK(scan.unsupported.empty());
}

TEST_CASE("ScanAbapGitFolder: one object in two folders is refused", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zdup.prog.xml");
    tree.Write("src/sub/zdup.prog.abap");
    auto result = ScanAbapGitFolder(tree.Root(), PrefixLayout(), "ZROOT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::PackageError);
    CHECK(result.Error().message.find("ZDUP") != std::string::npos);
}

TEST_CASE("ScanAbapGitFolder: case variants of one file are refused", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zdup.prog.abap");
    tree.Write("src/ZDUP.PROG.ABAP");
    if (fs::exists(tree.Root() / "src" / "zdup.prog.abap") &&
        !fs::equivalent(tree.Root() / "src" / "zdup.prog.abap",
                        tree.Root() / "src" / "ZDUP.PROG.ABAP")) {
        auto result = ScanAbapGitFolder(tree.Root(), PrefixLayout(), "ZROOT");
        REQUIRE(result.IsErr());
        CHECK(result.Error().category == ErrorCategory::PackageError);
    }
}

TEST_CASE("ScanAbapGitFolder: a DEVC file not named 'package' is refused",
          "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/zroot.devc.xml");
    auto result = ScanAbapGitFolder(tree.Root(), PrefixLayout(), "ZROOT");
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::PackageError);
}

TEST_CASE("ScanAbapGitFolder: ordering spans types and packages", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("src/b/ztran.tran.xml");
    tree.Write("src/a/zprog.prog.abap");
    tree.Write("src/zfg.fugr.xml");
    tree.Write("src/b/zdtel.dtel.xml");
    tree.Write("src/a/zdtel2.dtel.xml");
    tree.Write("src/zdoma.doma.xml");
    tree.Write("src/b/package.devc.xml");
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"DEVC ZROOT_B", "DOMA ZDOMA", "DTEL ZDTEL2",
                                                 "DTEL ZDTEL", "FUGR ZFG", "PROG ZPROG",
                                                 "TRAN ZTRAN"});
}

// ===========================================================================
// Single-file mode
// ===========================================================================

TEST_CASE("ScanAbapGitObject: collects the object's files from its folder",
          "[abapgit_repo]") {
    auto result = ScanAbapGitObject(
        FixtureRepo("PROG_dynp_flow") / "src" / "zdemo_splitter_subscreen.prog.xml", "zdemo");
    REQUIRE(result.IsOk());
    const auto& object = result.Value();
    CHECK(object.key == AbapGitObjectKey{"PROG", "ZDEMO_SPLITTER_SUBSCREEN"});
    CHECK(object.package == "ZDEMO");
    CHECK(fs::equivalent(object.folder, FixtureRepo("PROG_dynp_flow") / "src"));
    CHECK(Parts(object) == std::vector<std::string>{"abap", "screen_0100.abap",
                                                    "screen_0110.abap", "screen_0120.abap",
                                                    "screen_0200.abap", "xml"});
}

TEST_CASE("ScanAbapGitObject: any part file identifies the object", "[abapgit_repo]") {
    auto result = ScanAbapGitObject(FixtureRepo("FUGR_with_parameters") / "src" /
                                        "zagtest_function_group.fugr.zagtest_function_module.abap",
                                    "ZROOT");
    REQUIRE(result.IsOk());
    CHECK(result.Value().key == AbapGitObjectKey{"FUGR", "ZAGTEST_FUNCTION_GROUP"});
    CHECK(result.Value().files.size() == 6);
}

TEST_CASE("ScanAbapGitObject: same name, other type is a different object",
          "[abapgit_repo]") {
    auto result = ScanAbapGitObject(
        FixtureRepo("TRAN_Report") / "src" / "ztransaction_report.tran.xml", "ZROOT");
    REQUIRE(result.IsOk());
    CHECK(Parts(result.Value()) == std::vector<std::string>{"xml"});
}

TEST_CASE("ScanAbapGitObject: package.devc.xml names the root package", "[abapgit_repo]") {
    auto result =
        ScanAbapGitObject(FixtureRepo("TRAN_Report") / "src" / "package.devc.xml", "ZROOT");
    REQUIRE(result.IsOk());
    CHECK(result.Value().key == AbapGitObjectKey{"DEVC", "ZROOT"});
}

TEST_CASE("ScanAbapGitObject: missing or non-object files are refused", "[abapgit_repo]") {
    TempTree tree;
    tree.Write("README.md");
    CHECK(ScanAbapGitObject(tree.Root() / "zmissing.prog.xml", "ZROOT").IsErr());
    auto bad = ScanAbapGitObject(tree.Root() / "README.md", "ZROOT");
    REQUIRE(bad.IsErr());
    CHECK(bad.Error().message.find("README.md") != std::string::npos);
}

// ===========================================================================
// Ranks, stems, inverse folder logic
// ===========================================================================

TEST_CASE("DeployRank and IsSupportedType", "[abapgit_repo]") {
    CHECK(DeployRank("DEVC") == 0);
    CHECK(DeployRank("DOMA") == 10);
    CHECK(DeployRank("DTEL") == 20);
    CHECK(DeployRank("TABL") == 30);
    CHECK(DeployRank("TTYP") == 35);
    CHECK(DeployRank("FUGR") == 40);
    CHECK(DeployRank("CLAS") == 45);
    CHECK(DeployRank("INTF") == 45);
    CHECK(DeployRank("PROG") == 50);
    CHECK(DeployRank("TRAN") == 60);
    CHECK(DeployRank("SICF") == -1);
    CHECK(DeployRank("prog") == 50);

    for (const char* t : {"DEVC", "DOMA", "DTEL", "FUGR", "PROG", "TRAN"}) {
        CHECK(IsSupportedType(t));
    }
    for (const char* t : {"TABL", "TTYP", "CLAS", "INTF", "SICF", ""}) {
        CHECK_FALSE(IsSupportedType(t));
    }
}

TEST_CASE("ObjectFileStem mirrors the scanner", "[abapgit_repo]") {
    CHECK(ObjectFileStem({"PROG", "ZFOO"}) == "zfoo.prog");
    CHECK(ObjectFileStem({"TRAN", "/NS/TC"}) == "#ns#tc.tran");
    CHECK(ObjectFileStem({"DEVC", "ZWHATEVER"}) == "package.devc");
    CHECK(ObjectFileStem({"PROG", "ZFOO=BAR"}) == "zfoo%3dbar.prog");

    // Round trip: a file written with the stem scans back to the key.
    TempTree tree;
    for (const AbapGitObjectKey& key :
         {AbapGitObjectKey{"PROG", "/NS/OBJ"}, AbapGitObjectKey{"FUGR", "ZFG"},
          AbapGitObjectKey{"PROG", "ZA.B"}}) {
        tree.Write("src/" + ObjectFileStem(key) + ".xml");
    }
    auto scan = ScanOk(tree.Root(), PrefixLayout(), "ZROOT");
    CHECK(Keys(scan) == std::vector<std::string>{"FUGR ZFG", "PROG /NS/OBJ", "PROG ZA.B"});
}

TEST_CASE("FolderForPackage: PREFIX", "[abapgit_repo]") {
    const auto prefix = AbapGitLayout::FolderLogic::Prefix;
    CHECK(FolderForPackage("ZROOT", "ZROOT", prefix).Value() == "");
    CHECK(FolderForPackage("ZROOT_SUB", "ZROOT", prefix).Value() == "sub/");
    CHECK(FolderForPackage("zroot_sub", "ZROOT", prefix).Value() == "sub/");
    CHECK(FolderForPackage("/ACME/ROOT_FOO", "/ACME/ROOT", prefix).Value() == "foo/");
    CHECK(FolderForPackage("$ZDEMO_X", "$ZDEMO", prefix).Value() == "x/");
    CHECK(FolderForPackage("ZOTHER", "ZROOT", prefix).IsErr());
    CHECK(FolderForPackage("ZROOT_", "ZROOT", prefix).IsErr());
    CHECK(FolderForPackage("", "ZROOT", prefix).IsErr());
    CHECK(PackageFolderName("ZROOT_SUB_DEEP", "ZROOT_SUB", prefix).Value() == "deep");
}

TEST_CASE("FolderForPackage: FULL", "[abapgit_repo]") {
    const auto full = AbapGitLayout::FolderLogic::Full;
    CHECK(FolderForPackage("ZROOT", "ZROOT", full).Value() == "");
    CHECK(FolderForPackage("ZOTHER", "ZROOT", full).Value() == "zother/");
    CHECK(FolderForPackage("/ACME/PKG", "ZROOT", full).Value() == "#acme#pkg/");
    CHECK(FolderForPackage("$DEMO_SUB", "$ZDEMO", full).Value() == "demo_sub/");
    // a local child cannot sit under a transportable root and vice versa
    CHECK(FolderForPackage("$DEMO_SUB", "ZROOT", full).IsErr());
    CHECK(FolderForPackage("ZSUB", "$ZDEMO", full).IsErr());
}

TEST_CASE("FolderForPackage is the inverse of scanning", "[abapgit_repo]") {
    struct Case {
        AbapGitLayout::FolderLogic logic;
        std::string root;
        std::string package;
    };
    const auto prefix = AbapGitLayout::FolderLogic::Prefix;
    const auto full = AbapGitLayout::FolderLogic::Full;
    for (const auto& c : {Case{prefix, "ZROOT", "ZROOT_SUB"},
                          Case{prefix, "/ACME/ROOT", "/ACME/ROOT_FOO"},
                          Case{prefix, "$ZDEMO", "$ZDEMO_X"},
                          Case{prefix, "ZROOT", "ZROOT"}, Case{full, "ZROOT", "ZOTHER"},
                          Case{full, "ZROOT", "/ACME/PKG"},
                          Case{full, "$ZDEMO", "$DEMO_SUB"}}) {
        INFO(c.root << " -> " << c.package);
        TempTree tree;
        auto folder = FolderForPackage(c.package, c.root, c.logic);
        REQUIRE(folder.IsOk());
        tree.Write("src/" + folder.Value() + ObjectFileStem({"PROG", "ZPROG"}) + ".abap");
        AbapGitLayout layout = PrefixLayout();
        layout.folder_logic = c.logic;
        auto scan = ScanOk(tree.Root(), layout, c.root);
        REQUIRE(scan.objects.size() == 1);
        CHECK(scan.objects[0].package == c.package);
    }
}

// ===========================================================================
// Guard rails
// ===========================================================================

TEST_CASE("CheckCustomerObject: customer name range", "[abapgit_repo]") {
    const std::vector<std::string> none;
    CHECK(CheckCustomerObject({"PROG", "ZFOO"}, none).IsOk());
    CHECK(CheckCustomerObject({"PROG", "YFOO"}, none).IsOk());
    CHECK(CheckCustomerObject({"TRAN", "ZT1"}, none).IsOk());

    for (const char* name : {"RSABAPPROGRAM", "SAPMV45A", "/SAPAPO/X", "", "/ACME/X"}) {
        INFO(name);
        auto r = CheckCustomerObject({"PROG", name}, none);
        REQUIRE(r.IsErr());
        CHECK(r.Error().category == ErrorCategory::Authorization);
    }
    auto sap = CheckCustomerObject({"PROG", "RSABAPPROGRAM"}, none);
    CHECK(sap.Error().message.find("PROG RSABAPPROGRAM") != std::string::npos);
    CHECK(sap.Error().message.find("Y") != std::string::npos);

    auto ns = CheckCustomerObject({"PROG", "/SAPAPO/X"}, {"/ACME/"});
    REQUIRE(ns.IsErr());
    CHECK(ns.Error().message.find("/SAPAPO/") != std::string::npos);
}

TEST_CASE("CheckCustomerObject: customer namespaces", "[abapgit_repo]") {
    CHECK(CheckCustomerObject({"PROG", "/ACME/X"}, {"/ACME/"}).IsOk());
    CHECK(CheckCustomerObject({"PROG", "/ACME/X"}, {"ACME"}).IsOk());
    CHECK(CheckCustomerObject({"PROG", "/ACME/X"}, {"/acme/"}).IsOk());
    CHECK(CheckCustomerObject({"PROG", "/ACME/X"}, {"/OTHER/"}).IsErr());
    CHECK(CheckCustomerObject({"PROG", "/ACMEX"}, {"/ACME/"}).IsErr());   // malformed
    CHECK(CheckCustomerObject({"PROG", "/ACME/"}, {"/ACME/"}).IsErr());   // no name
    CHECK(CheckCustomerObject({"PROG", "//X"}, {"//"}).IsErr());
}

TEST_CASE("CheckCustomerObject: DEVC keys follow the package rules", "[abapgit_repo]") {
    CHECK(CheckCustomerObject({"DEVC", "$ZDEMO"}, {}).IsOk());
    CHECK(CheckCustomerObject({"DEVC", "SAPGUI"}, {}).IsErr());
}

TEST_CASE("CheckTargetPackage", "[abapgit_repo]") {
    const std::vector<std::string> acme{"/ACME/"};
    CHECK(CheckTargetPackage("$TMP", {}).IsOk());
    CHECK(CheckTargetPackage("$ZDEMO", {}).IsOk());
    CHECK(CheckTargetPackage("ZDEMO", {}).IsOk());
    CHECK(CheckTargetPackage("YDEMO", {}).IsOk());
    CHECK(CheckTargetPackage("/ACME/DEMO", acme).IsOk());

    for (const auto& [package, namespaces] :
         std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"/ACME/DEMO", {}},
             {"SAPGUI", acme},
             {"BASIS", {}},
             {"", {}},
             {"$", {}},
             {"/ACME/", acme}}) {
        INFO(package);
        auto r = CheckTargetPackage(package, namespaces);
        REQUIRE(r.IsErr());
        CHECK(r.Error().category == ErrorCategory::Authorization);
    }
    CHECK(CheckTargetPackage("SAPGUI", {}).Error().message.find("SAPGUI") != std::string::npos);
}

TEST_CASE("IsLocalPackage", "[abapgit_repo]") {
    CHECK(IsLocalPackage("$TMP"));
    CHECK(IsLocalPackage("$ZDEMO"));
    CHECK_FALSE(IsLocalPackage("ZDEMO"));
    CHECK_FALSE(IsLocalPackage(""));
}
