#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/app/abapgit_command.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace erpl_adt;

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() /
               ("erpl_adt_abapgit_command_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("abapgit command: status reports itself as status", "[app][abapgit]") {
    CHECK(ToString(AbapGitCommand::Status) == "status");
    CHECK(ToString(AbapGitCommand::Deploy) == "deploy");
    CHECK(ToString(AbapGitCommand::Export) == "export");
    CHECK(ToString(AbapGitCommand::Delete) == "delete");

    PreparedAbapGitRun run;
    run.command = AbapGitCommand::Status;
    run.options.dry_run = true;
    CHECK(AbapGitReportJson(run, AbapGitRunReport{})["command"] == "status");
}

TEST_CASE("abapgit command: status fails on a file it cannot load, even without --strict",
          "[app][abapgit]") {
    // A pull request that breaks an XML file must not pass `abapgit status`.
    PreparedAbapGitRun run;
    run.command = AbapGitCommand::Status;
    run.options.dry_run = true;
    AbapGitRunReport report;
    ObjectOutcome broken;
    broken.key = {"PROG", "ZFOO"};
    broken.action = ObjectAction::Failed;
    Error err;
    err.message = "zfoo.prog.xml: XML parse error";
    err.category = ErrorCategory::Internal;
    broken.error = err;
    report.objects.push_back(broken);
    report.local_error = err;
    CHECK(AbapGitExitCode(run, report) == err.ExitCode());
    CHECK(AbapGitExitCode(run, report) != 0);

    // A refusal (drift, transport, namespace) stays a report.
    AbapGitRunReport refused;
    ObjectOutcome drift;
    drift.key = {"PROG", "ZFOO"};
    drift.action = ObjectAction::Refused;
    Error lock;
    lock.category = ErrorCategory::LockConflict;
    drift.error = lock;
    refused.objects.push_back(drift);
    CHECK(AbapGitExitCode(run, refused) == 0);
}

TEST_CASE("abapgit command: a sub-folder of a repository names the repository root",
          "[app][abapgit]") {
    TempDir dir;
    fs::create_directories(dir.path / "repo" / "src");
    std::ofstream(dir.path / "repo" / ".abapgit.xml")
        << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<asx:abap "
           "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n <asx:values>\n  "
           "<DATA>\n   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n   "
           "<STARTING_FOLDER>/src/</STARTING_FOLDER>\n  </DATA>\n </asx:values>\n</asx:abap>\n";
    AbapGitRequest request;
    request.path = dir.path / "repo" / "src";
    request.package = "$TMP";
    auto prepared = PrepareAbapGitRun(AbapGitCommand::Deploy, request);
    REQUIRE(prepared.IsErr());
    CHECK(prepared.Error().message.find("is inside the abapGit repository") != std::string::npos);
    CHECK(prepared.Error().message.find((dir.path / "repo").string()) != std::string::npos);
}

TEST_CASE("abapgit command: a local error without an object decides the exit code",
          "[app][abapgit]") {
    PreparedAbapGitRun run;
    run.command = AbapGitCommand::Export;
    AbapGitRunReport report;
    Error err;
    err.message = "Cannot write /ro/.abapgit.xml";
    err.category = ErrorCategory::Internal;
    report.local_error = err;
    CHECK(AbapGitExitCode(run, report) == 99);
    CHECK(AbapGitReportJson(run, report)["error"] == err.message);
}
