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
