#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/abapgit_tool_handlers.hpp>
#include <erpl_adt/mcp/tool_metadata.hpp>
#include <erpl_adt/mcp/tool_registry.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace fs = std::filesystem;

namespace {

// A fresh directory tree: <tmp>/root (the abapGit root) and <tmp>/outside.
struct Sandbox {
    fs::path base;
    fs::path root;
    fs::path outside;

    Sandbox() {
        static std::atomic<int> counter{0};
        base = fs::temp_directory_path() /
               ("erpl_adt_abapgit_root_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "_" + std::to_string(counter++));
        root = base / "root";
        outside = base / "outside";
        fs::create_directories(root / "repo" / "src");
        fs::create_directories(outside);
        std::ofstream(outside / "secret.prog.abap") << "REPORT zsecret.";
        std::ofstream(root / "repo" / ".abapgit.xml") << "<?xml version=\"1.0\"?>";
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
};

struct Tools {
    MockAdtSession session;
    ToolRegistry registry;
    int classrun_sessions = 0;

    explicit Tools(const fs::path& root) {
        AbapGitToolOptions options;
        options.root = root;
        options.make_classrun_session = [this]() -> std::unique_ptr<IAdtSession> {
            ++classrun_sessions;
            return std::make_unique<MockAdtSession>();
        };
        RegisterAbapGitTools(registry, session, std::move(options));
    }

    size_t SapCalls() const {
        return session.GetCallCount() + session.PostCallCount() + session.PutCallCount() +
               session.DeleteCallCount();
    }
};

std::string Text(const ToolResult& r) {
    return r.content.at(0).at("text").get<std::string>();
}

}  // namespace

// ===========================================================================
// ResolvePathUnderRoot
// ===========================================================================

TEST_CASE("ResolvePathUnderRoot: relative paths inside the root resolve",
          "[mcp][abapgit][root]") {
    Sandbox box;
    auto repo = ResolvePathUnderRoot(box.root, "repo");
    REQUIRE(repo.IsOk());
    CHECK(repo.Value() == fs::canonical(box.root / "repo"));
    auto self = ResolvePathUnderRoot(box.root, ".");
    REQUIRE(self.IsOk());
    CHECK(self.Value() == fs::canonical(box.root));
    // A folder an export will create does not exist yet.
    CHECK(ResolvePathUnderRoot(box.root, "new-repo/sub").IsOk());
    // ".." that stays inside is fine.
    CHECK(ResolvePathUnderRoot(box.root, "repo/src/../src").IsOk());
}

TEST_CASE("ResolvePathUnderRoot: '..' escapes are refused", "[mcp][abapgit][root]") {
    Sandbox box;
    for (const char* p : {"..", "../outside", "repo/../../outside/secret.prog.abap"}) {
        INFO(p);
        auto r = ResolvePathUnderRoot(box.root, p);
        REQUIRE(r.IsErr());
        CHECK(r.Error().category == ErrorCategory::Authorization);
        CHECK(r.Error().message.find("outside the abapGit root") != std::string::npos);
    }
}

TEST_CASE("ResolvePathUnderRoot: absolute paths must lie inside the root",
          "[mcp][abapgit][root]") {
    Sandbox box;
    CHECK(ResolvePathUnderRoot(box.root, (box.root / "repo").string()).IsOk());
    CHECK(ResolvePathUnderRoot(box.root, box.outside.string()).IsErr());
    CHECK(ResolvePathUnderRoot(box.root, "/etc").IsErr());
    // A sibling whose name starts with the root's name is not inside it.
    fs::create_directories(box.base / "root2");
    CHECK(ResolvePathUnderRoot(box.root, (box.base / "root2").string()).IsErr());
}

TEST_CASE("ResolvePathUnderRoot: a symlink pointing outside the root is refused",
          "[mcp][abapgit][root]") {
    Sandbox box;
    fs::create_directory_symlink(box.outside, box.root / "escape");
    fs::create_directory_symlink(box.root / "repo", box.root / "alias");
    CHECK(ResolvePathUnderRoot(box.root, "escape").IsErr());
    CHECK(ResolvePathUnderRoot(box.root, "escape/secret.prog.abap").IsErr());
    CHECK(ResolvePathUnderRoot(box.root, "alias").IsOk());
}

TEST_CASE("ResolvePathUnderRoot: a missing root is refused", "[mcp][abapgit][root]") {
    Sandbox box;
    CHECK(ResolvePathUnderRoot(box.base / "no-such-root", "x").IsErr());
}

TEST_CASE("CheckNoSymlinkEscape: links inside the tree must stay inside the root",
          "[mcp][abapgit][root]") {
    Sandbox box;
    CHECK(CheckNoSymlinkEscape(box.root, box.root / "repo").IsOk());
    fs::create_symlink(box.root / "repo" / ".abapgit.xml", box.root / "repo" / "src" / "ok.xml");
    CHECK(CheckNoSymlinkEscape(box.root, box.root / "repo").IsOk());
    fs::create_symlink(box.outside / "secret.prog.abap",
                       box.root / "repo" / "src" / "zsecret.prog.abap");
    auto r = CheckNoSymlinkEscape(box.root, box.root / "repo");
    REQUIRE(r.IsErr());
    CHECK(r.Error().message.find("zsecret.prog.abap") != std::string::npos);
    CHECK(CheckNoSymlinkEscape(box.root, box.root / "not-there").IsOk());
}

// ===========================================================================
// The tools
// ===========================================================================

TEST_CASE("abapgit tools are registered, classified and titled", "[mcp][abapgit][metadata]") {
    Sandbox box;
    Tools tools(box.root);
    std::set<std::string> names;
    for (const auto& tool : tools.registry.Tools()) {
        names.insert(tool.name);
        INFO(tool.name);
        CHECK_FALSE(tool.title.empty());
        CHECK(tool.input_schema["required"] == nlohmann::json::array({"path", "package"}));
        CHECK(tool.input_schema["additionalProperties"] == false);
        CHECK(tool.output_schema.value("type", "") == "object");
        if (tool.name == "abapgit_status") {
            CHECK(tool.annotations.read_only);
            CHECK_FALSE(tool.annotations.destructive);
        } else {
            CHECK_FALSE(tool.annotations.read_only);
            CHECK(tool.annotations.destructive);
        }
    }
    CHECK(names == std::set<std::string>{"abapgit_status", "abapgit_deploy", "abapgit_export",
                                         "abapgit_delete"});
    std::set<std::string> classified;
    for (const auto& entry : ToolMetadataTable()) {
        classified.insert(entry.name);
    }
    for (const auto& n : names) {
        CHECK(classified.count(n) == 1);
    }
}

TEST_CASE("abapgit tools refuse a path outside the root before touching SAP",
          "[mcp][abapgit][root]") {
    Sandbox box;
    Tools tools(box.root);
    for (const char* tool : {"abapgit_status", "abapgit_deploy", "abapgit_export",
                             "abapgit_delete"}) {
        INFO(tool);
        auto r = tools.registry.Execute(
            tool, {{"path", "../outside/secret.prog.abap"}, {"package", "$TMP"}});
        CHECK(r.is_error);
        CHECK(Text(r).find("outside the abapGit root") != std::string::npos);
        auto abs = tools.registry.Execute(tool, {{"path", box.outside.string()},
                                                 {"package", "$TMP"}});
        CHECK(abs.is_error);
    }
    CHECK(tools.SapCalls() == 0);
    CHECK(tools.classrun_sessions == 0);
}

TEST_CASE("abapgit tools refuse a repository with a symlink leading out of the root",
          "[mcp][abapgit][root]") {
    Sandbox box;
    fs::create_symlink(box.outside / "secret.prog.abap",
                       box.root / "repo" / "src" / "zsecret.prog.abap");
    Tools tools(box.root);
    auto r = tools.registry.Execute("abapgit_deploy", {{"path", "repo"}, {"package", "$TMP"}});
    CHECK(r.is_error);
    CHECK(Text(r).find("points outside the abapGit root") != std::string::npos);
    CHECK(tools.SapCalls() == 0);
    CHECK(tools.classrun_sessions == 0);
}

TEST_CASE("abapgit tools refuse unknown and mistyped arguments", "[mcp][abapgit]") {
    Sandbox box;
    Tools tools(box.root);
    auto r = tools.registry.Execute("abapgit_delete",
                                    {{"path", "repo"}, {"package", "$TMP"}, {"dryrun", true}});
    CHECK(r.is_error);
    CHECK(Text(r).find("Unknown argument 'dryrun'") != std::string::npos);
    auto wrong_type = tools.registry.Execute(
        "abapgit_deploy", {{"path", "repo"}, {"package", "$TMP"}, {"dry_run", "yes"}});
    CHECK(wrong_type.is_error);
    auto missing = tools.registry.Execute("abapgit_status", {{"path", "repo"}});
    CHECK(missing.is_error);
    CHECK(Text(missing).find("package") != std::string::npos);
    // status takes no dry_run (it is one); export takes no transport.
    CHECK(tools.registry
              .Execute("abapgit_export",
                       {{"path", "repo"}, {"package", "$TMP"}, {"transport", "A4HK900001"}})
              .is_error);
    CHECK(tools.SapCalls() == 0);
}

TEST_CASE("abapgit tools validate package and transport like the CLI", "[mcp][abapgit]") {
    Sandbox box;
    Tools tools(box.root);
    auto r = tools.registry.Execute(
        "abapgit_deploy", {{"path", "repo"}, {"package", "ZDEMO"}, {"transport", "A4HK9' OR '1"}});
    CHECK(r.is_error);
    CHECK(Text(r).find("Invalid --transport") != std::string::npos);
    CHECK(tools.SapCalls() == 0);
}

// ===========================================================================
// Deploy record location (what this clone last deployed)
// ===========================================================================

TEST_CASE("McpDeployRecordDirectory: a .git inside the root keeps the record there",
          "[mcp][abapgit][root]") {
    Sandbox box;
    fs::create_directories(box.root / "repo" / ".git");
    const auto dir = McpDeployRecordDirectory(box.root, box.root / "repo", box.base / "cache");
    CHECK(dir == fs::weakly_canonical(box.root / "repo" / ".git") / "erpl-adt" / "deployed");
}

TEST_CASE("McpDeployRecordDirectory: a .git pointer leading out of the root is not followed",
          "[mcp][abapgit][root]") {
    Sandbox box;
    std::ofstream(box.root / "repo" / ".git") << "gitdir: " << box.outside.string() << "\n";
    const auto cache = box.base / "cache";
    const auto dir = McpDeployRecordDirectory(box.root, box.root / "repo", cache);
    // A server-side directory, one per repository, never the pointer's target.
    CHECK(dir.lexically_relative(cache).begin()->string() == "mcp-deployed");
    CHECK(dir.string().find(box.outside.string()) == std::string::npos);
    CHECK(dir != McpDeployRecordDirectory(box.root, box.root, cache));
    CHECK(dir == McpDeployRecordDirectory(box.root, box.root / "repo" / "src" / "..", cache));
}

TEST_CASE("McpDeployRecordDirectory: no git work tree uses the server cache, not the user's",
          "[mcp][abapgit][root]") {
    Sandbox box;
    const auto cache = box.base / "cache";
    const auto dir = McpDeployRecordDirectory(box.root, box.root / "repo", cache);
    CHECK(dir.lexically_relative(cache).begin()->string() == "mcp-deployed");
}
