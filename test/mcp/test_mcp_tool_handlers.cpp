#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/mcp/mcp_server.hpp>
#include <erpl_adt/mcp/mcp_tool_handlers.hpp>
#include <erpl_adt/mcp/tool_registry.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.rfind('/');
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.rfind('/'));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Helper: make a registry with all tools registered against a mock session.
ToolRegistry MakeRegistry(MockAdtSession& mock) {
    ToolRegistry registry;
    RegisterAdtTools(registry, mock);
    return registry;
}

// Helper: execute a tool by name with given arguments.
ToolResult CallTool(ToolRegistry& registry, const std::string& name,
                    const nlohmann::json& args) {
    return registry.Execute(name, args);
}

// Helper: parse the text content from a successful ToolResult.
nlohmann::json ParseContent(const ToolResult& result) {
    REQUIRE_FALSE(result.is_error);
    REQUIRE(result.content.size() == 1);
    return nlohmann::json::parse(result.content[0]["text"].get<std::string>());
}

} // anonymous namespace

// ===========================================================================
// Registration
// ===========================================================================

TEST_CASE("RegisterAdtTools: registers 20 tools", "[mcp][handlers]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);
    CHECK(registry.Tools().size() == 20);
}

TEST_CASE("RegisterAdtTools: all tools have schemas", "[mcp][handlers]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);
    for (const auto& tool : registry.Tools()) {
        CHECK_FALSE(tool.name.empty());
        CHECK_FALSE(tool.description.empty());
        CHECK(tool.input_schema.contains("type"));
        CHECK(tool.input_schema["type"] == "object");
    }
}

// ===========================================================================
// adt_search
// ===========================================================================

TEST_CASE("adt_search: happy path", "[mcp][handlers][search]") {
    MockAdtSession mock;
    auto xml = LoadFixture("search/search_results.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_search", {{"query", "ZCL_*"}});
    auto j = ParseContent(result);

    REQUIRE(j.is_array());
    REQUIRE(j.size() == 3);
    CHECK(j[0]["name"] == "ZCL_EXAMPLE");
    CHECK(j[0]["type"] == "CLAS/OC");
    CHECK(j[0]["uri"] == "/sap/bc/adt/oo/classes/zcl_example");
    CHECK(j[0]["package"] == "ZTEST_PKG");
}

TEST_CASE("adt_search: empty results", "[mcp][handlers][search]") {
    MockAdtSession mock;
    auto xml = LoadFixture("search/search_empty.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_search", {{"query", "NONEXISTENT"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
    CHECK(j.empty());
}

TEST_CASE("adt_search: missing query param", "[mcp][handlers][search]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_search", nlohmann::json::object());
    CHECK(result.is_error);
}

TEST_CASE("adt_search: ADT error propagates", "[mcp][handlers][search]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(
        Error{"Get", "/sap/bc/adt/repository/informationsystem/search",
              401, "Unauthorized", std::nullopt, ErrorCategory::Authentication}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_search", {{"query", "ZCL_*"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_read_object
// ===========================================================================

TEST_CASE("adt_read_object: happy path", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/class_metadata.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_object",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_example"}});
    auto j = ParseContent(result);

    CHECK(j["name"] == "ZCL_EXAMPLE");
    CHECK(j["type"] == "CLAS/OC");
    CHECK(j["description"] == "Example class");
    CHECK(j["version"] == "active");
    CHECK(j["includes"].is_array());
    CHECK(j["includes"].size() == 2);
}

TEST_CASE("adt_read_object: missing uri param", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_object",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

TEST_CASE("adt_read_object: invalid URI", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_object",
                           {{"uri", "not-a-valid-uri"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_read_source
// ===========================================================================

TEST_CASE("adt_read_source: happy path", "[mcp][handlers][source]") {
    MockAdtSession mock;
    std::string source = "CLASS zcl_test DEFINITION.\nENDCLASS.";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, source}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_source",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"}});
    auto j = ParseContent(result);
    CHECK(j["source"] == source);
}

TEST_CASE("adt_read_source: missing uri param", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_source",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_check_syntax
// ===========================================================================

TEST_CASE("adt_check_syntax: clean code", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto xml = LoadFixture("source/check_clean.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_check_syntax",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
}

TEST_CASE("adt_check_syntax: with errors", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto xml = LoadFixture("source/check_errors.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_check_syntax",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
    REQUIRE(j.size() > 0);
    CHECK(j[0].contains("type"));
    CHECK(j[0].contains("text"));
    CHECK(j[0].contains("line"));
}

TEST_CASE("adt_check_syntax: missing uri param", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_check_syntax",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_run_tests
// ===========================================================================

TEST_CASE("adt_run_tests: all passing", "[mcp][handlers][testing]") {
    MockAdtSession mock;
    auto xml = LoadFixture("testing/test_pass.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_run_tests",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    auto j = ParseContent(result);

    CHECK(j["all_passed"] == true);
    CHECK(j["total_failed"] == 0);
    CHECK(j["total_methods"].get<int>() > 0);
    CHECK(j["classes"].is_array());
}

TEST_CASE("adt_run_tests: with failures", "[mcp][handlers][testing]") {
    MockAdtSession mock;
    auto xml = LoadFixture("testing/test_failures.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_run_tests",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    auto j = ParseContent(result);

    CHECK(j["all_passed"] == false);
    CHECK(j["total_failed"].get<int>() > 0);
}

TEST_CASE("adt_run_tests: missing uri param", "[mcp][handlers][testing]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_run_tests",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_run_atc
// ===========================================================================

TEST_CASE("adt_run_atc: with findings", "[mcp][handlers][checks]") {
    MockAdtSession mock;
    // ATC workflow: POST create worklist (returns ID in body), POST run, GET results.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "wl_001"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto worklist_xml = LoadFixture("checks/atc_worklist.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, worklist_xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_run_atc",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    auto j = ParseContent(result);

    CHECK(j.contains("findings"));
    CHECK(j["findings"].is_array());
    CHECK(j["worklist_id"] == "wl_001");
}

TEST_CASE("adt_run_atc: missing uri param", "[mcp][handlers][checks]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_run_atc",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_list_transports
// ===========================================================================

TEST_CASE("adt_list_transports: happy path", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    auto xml = LoadFixture("transport/transport_list.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_list_transports",
                           nlohmann::json::object());
    auto j = ParseContent(result);

    REQUIRE(j.is_array());
    REQUIRE(j.size() == 3);
    CHECK(j[0]["number"] == "NPLK900001");
    CHECK(j[0]["description"] == "Implement feature X");
}

TEST_CASE("adt_list_transports: with user param", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    auto xml = LoadFixture("transport/transport_list.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_list_transports",
                           {{"user", "ADMIN"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
    // Verify the user param was passed to the session.
    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path.find("ADMIN") != std::string::npos);
}

// ===========================================================================
// adt_read_table
// ===========================================================================

TEST_CASE("adt_read_table: happy path", "[mcp][handlers][ddic]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/table_sflight.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_table",
                           {{"table_name", "SFLIGHT"}});
    auto j = ParseContent(result);

    CHECK(j.contains("name"));
    CHECK(j.contains("fields"));
    CHECK(j["fields"].is_array());
}

TEST_CASE("adt_read_table: missing table_name", "[mcp][handlers][ddic]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_table",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_read_cds
// ===========================================================================

TEST_CASE("adt_read_cds: happy path", "[mcp][handlers][ddic]") {
    MockAdtSession mock;
    std::string cds_source = "@AbapCatalog.sqlViewName: 'ZVIEW'\ndefine view zcds_view as select from sflight { * }";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, cds_source}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_cds",
                           {{"cds_name", "ZCDS_VIEW"}});
    auto j = ParseContent(result);
    CHECK(j["source"] == cds_source);
}

TEST_CASE("adt_read_cds: missing cds_name", "[mcp][handlers][ddic]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_read_cds",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_list_package
// ===========================================================================

TEST_CASE("adt_list_package: happy path", "[mcp][handlers][package]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/package_contents.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_list_package",
                           {{"package_name", "ZTEST"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
}

TEST_CASE("adt_list_package: missing package_name", "[mcp][handlers][package]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_list_package",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_package_tree
// ===========================================================================

TEST_CASE("adt_package_tree: happy path", "[mcp][handlers][package]") {
    MockAdtSession mock;
    auto xml = LoadFixture("ddic/package_contents.xml");
    // Root package has a DEVC/K sub-package, so BFS traverses it.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    // Sub-package returns empty (terminates BFS).
    std::string empty_xml =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\">"
        "<asx:values><DATA><TREE_CONTENT/></DATA></asx:values></asx:abap>";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, empty_xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_package_tree",
                           {{"root_package", "ZTEST"}});
    auto j = ParseContent(result);
    CHECK(j.is_array());
    CHECK(j.size() == 2);  // ZCL_EXAMPLE + ZTEST_REPORT (DEVC/K filtered out).
}

TEST_CASE("adt_package_tree: missing root_package", "[mcp][handlers][package]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_package_tree",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_lock
// ===========================================================================

TEST_CASE("adt_lock: happy path", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    auto xml = LoadFixture("object/lock_response.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_lock",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    auto j = ParseContent(result);

    CHECK(j["handle"] == "lock_handle_abc123");
    CHECK(j["transport_number"] == "NPLK900001");
    CHECK(mock.IsStateful());
}

TEST_CASE("adt_lock: missing uri", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_lock",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

TEST_CASE("adt_lock: invalid URI", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_lock",
                           {{"uri", "invalid"}});
    CHECK(result.is_error);
}

TEST_CASE("adt_lock: ADT error resets stateful", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    mock.EnqueuePost(Result<HttpResponse, Error>::Err(
        Error{"LockObject", "/sap/bc/adt/oo/classes/zcl_test",
              423, "Object locked by another user", std::nullopt,
              ErrorCategory::LockConflict}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_lock",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    CHECK(result.is_error);
    CHECK_FALSE(mock.IsStateful());
}

// ===========================================================================
// adt_unlock
// ===========================================================================

TEST_CASE("adt_unlock: happy path", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_unlock",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"},
                            {"lock_handle", "lock_handle_abc123"}});
    auto j = ParseContent(result);
    CHECK(j["unlocked"] == true);
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("adt_unlock: missing handle", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_unlock",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"}});
    CHECK(result.is_error);
}

TEST_CASE("adt_unlock: missing uri", "[mcp][handlers][locking]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_unlock",
                           {{"lock_handle", "some_handle"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_write_source (auto-lock mode)
// ===========================================================================

TEST_CASE("adt_write_source: auto-lock mode", "[mcp][handlers][source]") {
    MockAdtSession mock;
    // 1. CSRF fetch for lock
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    // 2. Lock POST
    auto lock_xml = LoadFixture("object/lock_response.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, lock_xml}));
    // 3. Write PUT
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    // 4. CSRF fetch for unlock
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token456"));
    // 5. Unlock POST
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_write_source",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"},
                            {"source", "CLASS zcl_test DEFINITION.\nENDCLASS."}});
    auto j = ParseContent(result);

    CHECK(j["written"] == true);
    CHECK(j["uri"] == "/sap/bc/adt/oo/classes/zcl_test/source/main");
    // Session should be non-stateful after auto-lock cycle.
    CHECK_FALSE(mock.IsStateful());
}

TEST_CASE("adt_write_source: with explicit handle", "[mcp][handlers][source]") {
    MockAdtSession mock;
    // Only the write PUT.
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_write_source",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"},
                            {"source", "CLASS zcl_test DEFINITION.\nENDCLASS."},
                            {"lock_handle", "lock_handle_abc123"}});
    auto j = ParseContent(result);
    CHECK(j["written"] == true);
    // No lock/unlock calls.
    CHECK(mock.PostCallCount() == 0);
}

TEST_CASE("adt_write_source: missing source param", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_write_source",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test/source/main"}});
    CHECK(result.is_error);
}

TEST_CASE("adt_write_source: missing uri param", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_write_source",
                           {{"source", "some code"}});
    CHECK(result.is_error);
}

TEST_CASE("adt_write_source: URI without /source/ segment", "[mcp][handlers][source]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_write_source",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_test"},
                            {"source", "some code"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_create_object
// ===========================================================================

TEST_CASE("adt_create_object: happy path", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto xml = LoadFixture("object/create_class_response.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {201,
         {{"Location", "/sap/bc/adt/oo/classes/zcl_new"}},
         xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_create_object",
                           {{"object_type", "CLAS/OC"},
                            {"name", "ZCL_NEW"},
                            {"package_name", "ZTEST"}});
    auto j = ParseContent(result);
    CHECK(j.contains("uri"));
}

TEST_CASE("adt_create_object: missing required params", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    // Missing name.
    auto result = CallTool(registry, "adt_create_object",
                           {{"object_type", "CLAS/OC"},
                            {"package_name", "ZTEST"}});
    CHECK(result.is_error);

    // Missing object_type.
    result = CallTool(registry, "adt_create_object",
                      {{"name", "ZCL_NEW"},
                       {"package_name", "ZTEST"}});
    CHECK(result.is_error);

    // Missing package_name.
    result = CallTool(registry, "adt_create_object",
                      {{"object_type", "CLAS/OC"},
                       {"name", "ZCL_NEW"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_delete_object (auto-lock mode)
// ===========================================================================

TEST_CASE("adt_delete_object: auto-lock mode", "[mcp][handlers][object]") {
    MockAdtSession mock;
    // 1. CSRF for lock
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    // 2. Lock
    auto lock_xml = LoadFixture("object/lock_response.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, lock_xml}));
    // 3. Delete
    mock.EnqueueDelete(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    // 4. CSRF for unlock
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token456"));
    // 5. Unlock
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_delete_object",
                           {{"uri", "/sap/bc/adt/oo/classes/zcl_old"}});
    auto j = ParseContent(result);

    CHECK(j["deleted"] == true);
    CHECK(j["uri"] == "/sap/bc/adt/oo/classes/zcl_old");
}

TEST_CASE("adt_delete_object: missing uri", "[mcp][handlers][object]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_delete_object",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_create_transport
// ===========================================================================

TEST_CASE("adt_create_transport: happy path", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok(
        {200, {}, "<RESULT><REQ_HEADER TRKORR=\"NPLK900099\"/></RESULT>"}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_create_transport",
                           {{"description", "Feature X"},
                            {"target_package", "ZTEST"}});
    auto j = ParseContent(result);
    CHECK(j.contains("transport_number"));
}

TEST_CASE("adt_create_transport: missing params", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_create_transport",
                           {{"description", "Feature X"}});
    CHECK(result.is_error);

    result = CallTool(registry, "adt_create_transport",
                      {{"target_package", "ZTEST"}});
    CHECK(result.is_error);
}

// ===========================================================================
// adt_release_transport
// ===========================================================================

TEST_CASE("adt_release_transport: happy path", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok("token123"));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_release_transport",
                           {{"transport_number", "NPLK900001"}});
    auto j = ParseContent(result);
    CHECK(j["released"] == true);
    CHECK(j["transport_number"] == "NPLK900001");
}

TEST_CASE("adt_release_transport: missing transport_number", "[mcp][handlers][transport]") {
    MockAdtSession mock;
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_release_transport",
                           nlohmann::json::object());
    CHECK(result.is_error);
}

// ===========================================================================
// adt_discover
// ===========================================================================

TEST_CASE("adt_discover: happy path", "[mcp][handlers][discover]") {
    MockAdtSession mock;
    auto xml = LoadFixture("discovery_response.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_discover",
                           nlohmann::json::object());
    auto j = ParseContent(result);

    CHECK(j.contains("workspaces"));
    CHECK(j["workspaces"].is_array());
    CHECK(j["workspaces"].size() == 5);
    CHECK(j["workspaces"][0]["title"] == "Discovery");
    CHECK(j["workspaces"][0]["services"].is_array());
    CHECK(j.contains("has_abapgit"));
    CHECK(j.contains("has_packages"));
    CHECK(j.contains("has_activation"));
}

TEST_CASE("adt_discover: workspace filter", "[mcp][handlers][discover]") {
    MockAdtSession mock;
    auto xml = LoadFixture("discovery_response.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));
    auto registry = MakeRegistry(mock);

    auto result = CallTool(registry, "adt_discover",
                           {{"workspace", "Sources"}});
    auto j = ParseContent(result);

    CHECK(j["workspaces"].size() == 1);
    CHECK(j["workspaces"][0]["title"] == "Sources");
    CHECK(j["workspaces"][0]["services"].size() == 4);
}

// ===========================================================================
// Integration: McpServer + tool handlers end-to-end
// ===========================================================================

TEST_CASE("MCP end-to-end: tools/list returns all ADT tools", "[mcp][handlers][e2e]") {
    MockAdtSession mock;
    ToolRegistry registry;
    RegisterAdtTools(registry, mock);

    // Use McpServer to handle tools/list.
    std::istringstream in;
    std::ostringstream out;
    McpServer server(std::move(registry), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/list"}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());

    auto& tools = (*response)["result"]["tools"];
    CHECK(tools.size() == 20);

    // Verify expected tool names are present.
    std::set<std::string> names;
    for (const auto& t : tools) {
        names.insert(t["name"].get<std::string>());
    }
    CHECK(names.count("adt_search") == 1);
    CHECK(names.count("adt_read_source") == 1);
    CHECK(names.count("adt_write_source") == 1);
    CHECK(names.count("adt_run_tests") == 1);
    CHECK(names.count("adt_lock") == 1);
    CHECK(names.count("adt_unlock") == 1);
    CHECK(names.count("adt_discover") == 1);
}

TEST_CASE("MCP end-to-end: tools/call adt_search", "[mcp][handlers][e2e]") {
    MockAdtSession mock;
    auto xml = LoadFixture("search/search_results.xml");
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    ToolRegistry registry;
    RegisterAdtTools(registry, mock);

    std::istringstream in;
    std::ostringstream out;
    McpServer server(std::move(registry), in, out);

    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", {
            {"name", "adt_search"},
            {"arguments", {{"query", "ZCL_*"}}}
        }}
    };

    auto response = server.HandleMessage(msg);
    REQUIRE(response.has_value());

    auto& content = (*response)["result"]["content"];
    REQUIRE(content.size() == 1);
    CHECK(content[0]["type"] == "text");

    auto results = nlohmann::json::parse(content[0]["text"].get<std::string>());
    CHECK(results.size() == 3);
    CHECK(results[0]["name"] == "ZCL_EXAMPLE");
}
