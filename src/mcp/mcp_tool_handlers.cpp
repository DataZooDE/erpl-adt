#include <erpl_adt/mcp/mcp_tool_handlers.hpp>

#include <erpl_adt/adt/checks.hpp>
#include <erpl_adt/adt/ddic.hpp>
#include <erpl_adt/adt/discovery.hpp>
#include <erpl_adt/adt/locking.hpp>
#include <erpl_adt/adt/object.hpp>
#include <erpl_adt/adt/packages.hpp>
#include <erpl_adt/adt/search.hpp>
#include <erpl_adt/adt/source.hpp>
#include <erpl_adt/adt/testing.hpp>
#include <erpl_adt/adt/transport.hpp>
#include <erpl_adt/adt/xml_codec.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace erpl_adt {

namespace {

// ---------------------------------------------------------------------------
// Result helpers
// ---------------------------------------------------------------------------

ToolResult MakeOkResult(const nlohmann::json& data) {
    return ToolResult{
        false,
        nlohmann::json::array({{{"type", "text"}, {"text", data.dump()}}})};
}

ToolResult MakeErrorResult(const Error& error) {
    return ToolResult{
        true,
        nlohmann::json::array(
            {{{"type", "text"}, {"text", error.ToJson()}}})};
}

ToolResult MakeParamError(const std::string& msg) {
    return ToolResult{
        true,
        nlohmann::json::array({{{"type", "text"}, {"text", msg}}})};
}

// Get a required string param. Returns nullopt and sets out_error on failure.
std::optional<std::string> RequireString(const nlohmann::json& params,
                                         const std::string& key,
                                         ToolResult& out_error) {
    if (!params.contains(key) || !params[key].is_string() ||
        params[key].get<std::string>().empty()) {
        out_error = MakeParamError("Missing required parameter: " + key);
        return std::nullopt;
    }
    return params[key].get<std::string>();
}

// Get an optional string param with a default value.
std::string OptString(const nlohmann::json& params, const std::string& key,
                      const std::string& default_val = "") {
    if (params.contains(key) && params[key].is_string()) {
        return params[key].get<std::string>();
    }
    return default_val;
}

// Get an optional int param with a default value.
int OptInt(const nlohmann::json& params, const std::string& key,
           int default_val) {
    if (params.contains(key) && params[key].is_number_integer()) {
        return params[key].get<int>();
    }
    return default_val;
}

// ---------------------------------------------------------------------------
// JSON Schema helpers
// ---------------------------------------------------------------------------

nlohmann::json StringProp(const std::string& desc) {
    return {{"type", "string"}, {"description", desc}};
}

nlohmann::json IntProp(const std::string& desc) {
    return {{"type", "integer"}, {"description", desc}};
}

nlohmann::json MakeSchema(const nlohmann::json& properties,
                           const nlohmann::json& required) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required}};
}

// ---------------------------------------------------------------------------
// Tool handlers
// ---------------------------------------------------------------------------

// adt_search
ToolResult HandleSearch(IAdtSession& session, const nlohmann::json& params) {
    ToolResult err;
    auto query = RequireString(params, "query", err);
    if (!query) return err;

    SearchOptions opts;
    opts.query = *query;
    opts.max_results = OptInt(params, "max_results", 100);
    auto obj_type = OptString(params, "object_type");
    if (!obj_type.empty()) {
        opts.object_type = obj_type;
    }

    auto result = SearchObjects(session, opts);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j = nlohmann::json::array();
    for (const auto& r : result.Value()) {
        j.push_back({{"name", r.name},
                      {"type", r.type},
                      {"uri", r.uri},
                      {"description", r.description},
                      {"package", r.package_name}});
    }
    return MakeOkResult(j);
}

// adt_read_object
ToolResult HandleReadObject(IAdtSession& session,
                            const nlohmann::json& params) {
    ToolResult err;
    auto uri_str = RequireString(params, "uri", err);
    if (!uri_str) return err;

    auto uri_result = ObjectUri::Create(*uri_str);
    if (uri_result.IsErr()) return MakeParamError("Invalid URI: " + uri_result.Error());

    auto result = GetObjectStructure(session, uri_result.Value());
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& obj = result.Value();
    nlohmann::json j;
    j["name"] = obj.info.name;
    j["type"] = obj.info.type;
    j["uri"] = obj.info.uri;
    j["description"] = obj.info.description;
    j["source_uri"] = obj.info.source_uri;
    j["version"] = obj.info.version;
    j["responsible"] = obj.info.responsible;
    j["changed_by"] = obj.info.changed_by;
    nlohmann::json includes = nlohmann::json::array();
    for (const auto& inc : obj.includes) {
        includes.push_back({{"name", inc.name},
                            {"type", inc.type},
                            {"include_type", inc.include_type},
                            {"source_uri", inc.source_uri}});
    }
    j["includes"] = includes;
    return MakeOkResult(j);
}

// adt_read_source
ToolResult HandleReadSource(IAdtSession& session,
                            const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;

    auto version = OptString(params, "version", "active");
    auto result = ReadSource(session, *uri, version);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["source"] = result.Value();
    return MakeOkResult(j);
}

// adt_check_syntax
ToolResult HandleCheckSyntax(IAdtSession& session,
                             const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;

    auto result = CheckSyntax(session, *uri);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j = nlohmann::json::array();
    for (const auto& m : result.Value()) {
        j.push_back({{"type", m.type},
                      {"text", m.text},
                      {"uri", m.uri},
                      {"line", m.line},
                      {"offset", m.offset}});
    }
    return MakeOkResult(j);
}

// adt_run_tests
ToolResult HandleRunTests(IAdtSession& session,
                          const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;

    auto result = RunTests(session, *uri);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& tr = result.Value();
    nlohmann::json j;
    j["total_methods"] = tr.TotalMethods();
    j["total_failed"] = tr.TotalFailed();
    j["all_passed"] = tr.AllPassed();
    nlohmann::json classes = nlohmann::json::array();
    for (const auto& c : tr.classes) {
        nlohmann::json methods = nlohmann::json::array();
        for (const auto& m : c.methods) {
            nlohmann::json alerts = nlohmann::json::array();
            for (const auto& a : m.alerts) {
                alerts.push_back({{"kind", a.kind},
                                  {"severity", a.severity},
                                  {"title", a.title},
                                  {"detail", a.detail}});
            }
            methods.push_back({{"name", m.name},
                               {"execution_time_ms", m.execution_time_ms},
                               {"passed", m.Passed()},
                               {"alerts", alerts}});
        }
        classes.push_back({{"name", c.name},
                           {"uri", c.uri},
                           {"methods", methods}});
    }
    j["classes"] = classes;
    return MakeOkResult(j);
}

// adt_run_atc
ToolResult HandleRunAtc(IAdtSession& session,
                        const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;

    auto variant = OptString(params, "check_variant", "DEFAULT");
    auto result = RunAtcCheck(session, *uri, variant);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& atc = result.Value();
    nlohmann::json j;
    j["worklist_id"] = atc.worklist_id;
    j["error_count"] = atc.ErrorCount();
    j["warning_count"] = atc.WarningCount();
    nlohmann::json findings = nlohmann::json::array();
    for (const auto& f : atc.findings) {
        findings.push_back({{"uri", f.uri},
                            {"message", f.message},
                            {"priority", f.priority},
                            {"check_title", f.check_title},
                            {"message_title", f.message_title}});
    }
    j["findings"] = findings;
    return MakeOkResult(j);
}

// adt_list_transports
ToolResult HandleListTransports(IAdtSession& session,
                                const nlohmann::json& params) {
    auto user = OptString(params, "user", "DEVELOPER");
    auto result = ListTransports(session, user);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j = nlohmann::json::array();
    for (const auto& t : result.Value()) {
        j.push_back({{"number", t.number},
                      {"description", t.description},
                      {"owner", t.owner},
                      {"status", t.status},
                      {"target", t.target}});
    }
    return MakeOkResult(j);
}

// adt_read_table
ToolResult HandleReadTable(IAdtSession& session,
                           const nlohmann::json& params) {
    ToolResult err;
    auto name = RequireString(params, "table_name", err);
    if (!name) return err;

    auto result = GetTableDefinition(session, *name);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& table = result.Value();
    nlohmann::json j;
    j["name"] = table.name;
    j["description"] = table.description;
    j["delivery_class"] = table.delivery_class;
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& f : table.fields) {
        fields.push_back({{"name", f.name},
                          {"type", f.type},
                          {"description", f.description},
                          {"key_field", f.key_field}});
    }
    j["fields"] = fields;
    return MakeOkResult(j);
}

// adt_read_cds
ToolResult HandleReadCds(IAdtSession& session,
                         const nlohmann::json& params) {
    ToolResult err;
    auto name = RequireString(params, "cds_name", err);
    if (!name) return err;

    auto result = GetCdsSource(session, *name);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["source"] = result.Value();
    return MakeOkResult(j);
}

// adt_list_package
ToolResult HandleListPackage(IAdtSession& session,
                             const nlohmann::json& params) {
    ToolResult err;
    auto name = RequireString(params, "package_name", err);
    if (!name) return err;

    auto result = ListPackageContents(session, *name);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : result.Value()) {
        j.push_back({{"object_type", e.object_type},
                      {"object_name", e.object_name},
                      {"object_uri", e.object_uri},
                      {"description", e.description}});
    }
    return MakeOkResult(j);
}

// adt_package_tree
ToolResult HandlePackageTree(IAdtSession& session,
                             const nlohmann::json& params) {
    ToolResult err;
    auto root = RequireString(params, "root_package", err);
    if (!root) return err;

    PackageTreeOptions opts;
    opts.root_package = *root;
    auto type_filter = OptString(params, "type_filter");
    if (!type_filter.empty()) {
        opts.type_filter = type_filter;
    }
    opts.max_depth = OptInt(params, "max_depth", 50);

    auto result = ListPackageTree(session, opts);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : result.Value()) {
        j.push_back({{"object_type", e.object_type},
                      {"object_name", e.object_name},
                      {"object_uri", e.object_uri},
                      {"description", e.description},
                      {"package", e.package_name}});
    }
    return MakeOkResult(j);
}

// adt_package_exists
ToolResult HandlePackageExists(IAdtSession& session,
                               const nlohmann::json& params) {
    ToolResult err;
    auto name = RequireString(params, "package_name", err);
    if (!name) return err;

    auto pkg_result = PackageName::Create(*name);
    if (pkg_result.IsErr()) {
        return MakeParamError("Invalid package name: " + pkg_result.Error());
    }

    XmlCodec codec;
    auto result = PackageExists(session, codec, pkg_result.Value());
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["exists"] = result.Value();
    j["package"] = *name;
    return MakeOkResult(j);
}

// adt_discover
ToolResult HandleDiscover(IAdtSession& session,
                          const nlohmann::json& /*params*/) {
    XmlCodec codec;
    auto result = Discover(session, codec);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& disc = result.Value();
    nlohmann::json j;
    nlohmann::json services = nlohmann::json::array();
    for (const auto& s : disc.services) {
        services.push_back({{"title", s.title},
                            {"href", s.href},
                            {"type", s.type}});
    }
    j["services"] = services;
    j["has_abapgit"] = disc.has_abapgit_support;
    j["has_packages"] = disc.has_packages_support;
    j["has_activation"] = disc.has_activation_support;
    return MakeOkResult(j);
}

// adt_lock
ToolResult HandleLock(IAdtSession& session,
                      const nlohmann::json& params) {
    ToolResult err;
    auto uri_str = RequireString(params, "uri", err);
    if (!uri_str) return err;

    auto uri_result = ObjectUri::Create(*uri_str);
    if (uri_result.IsErr()) return MakeParamError("Invalid URI: " + uri_result.Error());

    session.SetStateful(true);
    auto result = LockObject(session, uri_result.Value());
    if (result.IsErr()) {
        session.SetStateful(false);
        return MakeErrorResult(result.Error());
    }

    const auto& lock = result.Value();
    nlohmann::json j;
    j["handle"] = lock.handle.Value();
    j["transport_number"] = lock.transport_number;
    j["transport_owner"] = lock.transport_owner;
    j["transport_text"] = lock.transport_text;
    return MakeOkResult(j);
}

// adt_unlock
ToolResult HandleUnlock(IAdtSession& session,
                        const nlohmann::json& params) {
    ToolResult err;
    auto uri_str = RequireString(params, "uri", err);
    if (!uri_str) return err;
    auto handle_str = RequireString(params, "lock_handle", err);
    if (!handle_str) return err;

    auto uri_result = ObjectUri::Create(*uri_str);
    if (uri_result.IsErr()) return MakeParamError("Invalid URI: " + uri_result.Error());
    auto handle_result = LockHandle::Create(*handle_str);
    if (handle_result.IsErr()) return MakeParamError("Invalid handle: " + handle_result.Error());

    auto result = UnlockObject(session, uri_result.Value(), handle_result.Value());
    session.SetStateful(false);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    return MakeOkResult({{"unlocked", true}});
}

// adt_write_source
ToolResult HandleWriteSource(IAdtSession& session,
                             const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;
    auto source = RequireString(params, "source", err);
    if (!source) return err;

    std::optional<std::string> transport;
    auto transport_str = OptString(params, "transport");
    if (!transport_str.empty()) {
        transport = transport_str;
    }

    auto handle_str = OptString(params, "lock_handle");

    if (!handle_str.empty()) {
        // Explicit handle: use it directly.
        auto handle_result = LockHandle::Create(handle_str);
        if (handle_result.IsErr()) {
            return MakeParamError("Invalid lock_handle: " + handle_result.Error());
        }
        auto result = WriteSource(session, *uri, *source,
                                  handle_result.Value(), transport);
        if (result.IsErr()) return MakeErrorResult(result.Error());
    } else {
        // Auto-lock mode: derive object URI, lock -> write -> unlock.
        auto slash_pos = uri->find("/source/");
        if (slash_pos == std::string::npos) {
            return MakeParamError(
                "Cannot derive object URI from source URI "
                "(expected /source/ segment): " + *uri);
        }
        auto obj_uri_str = uri->substr(0, slash_pos);
        auto obj_uri = ObjectUri::Create(obj_uri_str);
        if (obj_uri.IsErr()) {
            return MakeParamError("Invalid object URI: " + obj_uri.Error());
        }

        session.SetStateful(true);
        auto lock_result = LockObject(session, obj_uri.Value());
        if (lock_result.IsErr()) {
            session.SetStateful(false);
            return MakeErrorResult(lock_result.Error());
        }
        auto write_result = WriteSource(session, *uri, *source,
                                        lock_result.Value().handle, transport);
        (void)UnlockObject(session, obj_uri.Value(),
                           lock_result.Value().handle);
        session.SetStateful(false);
        if (write_result.IsErr()) return MakeErrorResult(write_result.Error());
    }

    nlohmann::json j;
    j["written"] = true;
    j["uri"] = *uri;
    return MakeOkResult(j);
}

// adt_create_object
ToolResult HandleCreateObject(IAdtSession& session,
                              const nlohmann::json& params) {
    ToolResult err;
    auto obj_type = RequireString(params, "object_type", err);
    if (!obj_type) return err;
    auto name = RequireString(params, "name", err);
    if (!name) return err;
    auto pkg = RequireString(params, "package_name", err);
    if (!pkg) return err;

    CreateObjectParams cp;
    cp.object_type = *obj_type;
    cp.name = *name;
    cp.package_name = *pkg;
    cp.description = OptString(params, "description");
    auto transport_str = OptString(params, "transport");
    if (!transport_str.empty()) {
        cp.transport_number = transport_str;
    }

    auto result = CreateObject(session, cp);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["uri"] = result.Value().Value();
    return MakeOkResult(j);
}

// adt_delete_object
ToolResult HandleDeleteObject(IAdtSession& session,
                              const nlohmann::json& params) {
    ToolResult err;
    auto uri_str = RequireString(params, "uri", err);
    if (!uri_str) return err;

    auto uri_result = ObjectUri::Create(*uri_str);
    if (uri_result.IsErr()) return MakeParamError("Invalid URI: " + uri_result.Error());

    std::optional<std::string> transport;
    auto transport_str = OptString(params, "transport");
    if (!transport_str.empty()) {
        transport = transport_str;
    }

    auto handle_str = OptString(params, "lock_handle");

    if (!handle_str.empty()) {
        auto handle_result = LockHandle::Create(handle_str);
        if (handle_result.IsErr()) {
            return MakeParamError("Invalid lock_handle: " + handle_result.Error());
        }
        auto result = DeleteObject(session, uri_result.Value(),
                                   handle_result.Value(), transport);
        if (result.IsErr()) return MakeErrorResult(result.Error());
    } else {
        // Auto-lock mode.
        session.SetStateful(true);
        auto lock_result = LockObject(session, uri_result.Value());
        if (lock_result.IsErr()) {
            session.SetStateful(false);
            return MakeErrorResult(lock_result.Error());
        }
        auto del_result = DeleteObject(session, uri_result.Value(),
                                       lock_result.Value().handle, transport);
        (void)UnlockObject(session, uri_result.Value(),
                           lock_result.Value().handle);
        session.SetStateful(false);
        if (del_result.IsErr()) return MakeErrorResult(del_result.Error());
    }

    nlohmann::json j;
    j["deleted"] = true;
    j["uri"] = *uri_str;
    return MakeOkResult(j);
}

// adt_create_transport
ToolResult HandleCreateTransport(IAdtSession& session,
                                 const nlohmann::json& params) {
    ToolResult err;
    auto desc = RequireString(params, "description", err);
    if (!desc) return err;
    auto pkg = RequireString(params, "target_package", err);
    if (!pkg) return err;

    auto result = CreateTransport(session, *desc, *pkg);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["transport_number"] = result.Value();
    return MakeOkResult(j);
}

// adt_release_transport
ToolResult HandleReleaseTransport(IAdtSession& session,
                                  const nlohmann::json& params) {
    ToolResult err;
    auto number = RequireString(params, "transport_number", err);
    if (!number) return err;

    auto result = ReleaseTransport(session, *number);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    nlohmann::json j;
    j["released"] = true;
    j["transport_number"] = *number;
    return MakeOkResult(j);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RegisterAdtTools
// ---------------------------------------------------------------------------
void RegisterAdtTools(ToolRegistry& registry, IAdtSession& session) {
    // === Read-only tools ===

    registry.Register(
        "adt_search",
        "Search the ABAP repository for objects by name pattern. "
        "Use wildcards (*). Returns object URIs needed for all other operations.",
        MakeSchema(
            {{"query", StringProp("Search pattern with wildcards (e.g., ZCL_*)")},
             {"max_results", IntProp("Maximum number of results (default: 100)")},
             {"object_type", StringProp("Filter by type: CLAS, PROG, TABL, INTF, FUGR")}},
            {"query"}),
        [&session](const nlohmann::json& params) {
            return HandleSearch(session, params);
        });

    registry.Register(
        "adt_read_object",
        "Read metadata and structure of an ABAP object. "
        "Returns name, type, source URIs, includes, and version info.",
        MakeSchema(
            {{"uri", StringProp("Object URI (e.g., /sap/bc/adt/oo/classes/zcl_example)")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleReadObject(session, params);
        });

    registry.Register(
        "adt_read_source",
        "Read the source code of an ABAP object. Returns plain text source.",
        MakeSchema(
            {{"uri", StringProp("Source URI (e.g., /sap/bc/adt/oo/classes/zcl_test/source/main)")},
             {"version", StringProp("Version: 'active' (default) or 'inactive'")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleReadSource(session, params);
        });

    registry.Register(
        "adt_check_syntax",
        "Run a syntax check on an ABAP source object. "
        "Returns errors and warnings with line numbers.",
        MakeSchema(
            {{"uri", StringProp("Source URI to check")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleCheckSyntax(session, params);
        });

    registry.Register(
        "adt_run_tests",
        "Run ABAP Unit tests for an object or package. "
        "Returns structured pass/fail results with assertion messages. "
        "The primary feedback loop for code changes.",
        MakeSchema(
            {{"uri", StringProp("Object or package URI")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleRunTests(session, params);
        });

    registry.Register(
        "adt_run_atc",
        "Run ABAP Test Cockpit quality checks. "
        "Returns findings with severity and line numbers.",
        MakeSchema(
            {{"uri", StringProp("Object or package URI")},
             {"check_variant", StringProp("ATC check variant (default: DEFAULT)")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleRunAtc(session, params);
        });

    registry.Register(
        "adt_list_transports",
        "List transport requests for a user. "
        "Returns transport numbers, descriptions, and status.",
        MakeSchema(
            {{"user", StringProp("SAP username (default: DEVELOPER)")}},
            nlohmann::json::array()),
        [&session](const nlohmann::json& params) {
            return HandleListTransports(session, params);
        });

    registry.Register(
        "adt_read_table",
        "Get a database table definition including fields, types, and key info.",
        MakeSchema(
            {{"table_name", StringProp("Table name (e.g., SFLIGHT)")}},
            {"table_name"}),
        [&session](const nlohmann::json& params) {
            return HandleReadTable(session, params);
        });

    registry.Register(
        "adt_read_cds",
        "Read the source code of a CDS view definition.",
        MakeSchema(
            {{"cds_name", StringProp("CDS view name")}},
            {"cds_name"}),
        [&session](const nlohmann::json& params) {
            return HandleReadCds(session, params);
        });

    registry.Register(
        "adt_list_package",
        "List objects inside a package (non-recursive, one level).",
        MakeSchema(
            {{"package_name", StringProp("Package name (e.g., ZTEST)")}},
            {"package_name"}),
        [&session](const nlohmann::json& params) {
            return HandleListPackage(session, params);
        });

    registry.Register(
        "adt_package_tree",
        "Recursively list all objects in a package hierarchy. "
        "Use this for exhaustive enumeration when search maxResults is not sufficient.",
        MakeSchema(
            {{"root_package", StringProp("Root package name")},
             {"type_filter", StringProp("Filter by object type: CLAS, PROG, TABL")},
             {"max_depth", IntProp("Maximum recursion depth (default: 50)")}},
            {"root_package"}),
        [&session](const nlohmann::json& params) {
            return HandlePackageTree(session, params);
        });

    registry.Register(
        "adt_package_exists",
        "Check if a package exists in the ABAP system.",
        MakeSchema(
            {{"package_name", StringProp("Package name (e.g., ZTEST)")}},
            {"package_name"}),
        [&session](const nlohmann::json& params) {
            return HandlePackageExists(session, params);
        });

    registry.Register(
        "adt_discover",
        "Discover available ADT services and capabilities. "
        "Returns service list and feature flags (abapGit, packages, activation).",
        MakeSchema({}, nlohmann::json::array()),
        [&session](const nlohmann::json& params) {
            return HandleDiscover(session, params);
        });

    // === Mutating tools ===

    registry.Register(
        "adt_lock",
        "Lock an ABAP object for editing. "
        "Returns a lock handle. The session becomes stateful. "
        "Call adt_unlock when done.",
        MakeSchema(
            {{"uri", StringProp("Object URI to lock")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleLock(session, params);
        });

    registry.Register(
        "adt_unlock",
        "Unlock a previously locked ABAP object.",
        MakeSchema(
            {{"uri", StringProp("Object URI to unlock")},
             {"lock_handle", StringProp("Lock handle from adt_lock")}},
            {"uri", "lock_handle"}),
        [&session](const nlohmann::json& params) {
            return HandleUnlock(session, params);
        });

    registry.Register(
        "adt_write_source",
        "Write source code to an ABAP object. "
        "Automatically handles lock/unlock cycle unless lock_handle is provided. "
        "Provide complete source, not a diff.",
        MakeSchema(
            {{"uri", StringProp("Source URI (e.g., /sap/bc/adt/oo/classes/zcl_test/source/main)")},
             {"source", StringProp("Complete ABAP source code to write")},
             {"lock_handle", StringProp("Lock handle (skips auto-lock if provided)")},
             {"transport", StringProp("Transport request number")}},
            {"uri", "source"}),
        [&session](const nlohmann::json& params) {
            return HandleWriteSource(session, params);
        });

    registry.Register(
        "adt_create_object",
        "Create a new ABAP object (class, program, etc.).",
        MakeSchema(
            {{"object_type", StringProp("Object type (e.g., CLAS/OC, PROG/P)")},
             {"name", StringProp("Object name (e.g., ZCL_MY_CLASS)")},
             {"package_name", StringProp("Target package")},
             {"description", StringProp("Object description")},
             {"transport", StringProp("Transport request number")}},
            {"object_type", "name", "package_name"}),
        [&session](const nlohmann::json& params) {
            return HandleCreateObject(session, params);
        });

    registry.Register(
        "adt_delete_object",
        "Delete an ABAP object. "
        "Automatically handles lock/unlock unless lock_handle is provided.",
        MakeSchema(
            {{"uri", StringProp("Object URI to delete")},
             {"lock_handle", StringProp("Lock handle (skips auto-lock if provided)")},
             {"transport", StringProp("Transport request number")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleDeleteObject(session, params);
        });

    registry.Register(
        "adt_create_transport",
        "Create a new transport request.",
        MakeSchema(
            {{"description", StringProp("Transport description")},
             {"target_package", StringProp("Target package")}},
            {"description", "target_package"}),
        [&session](const nlohmann::json& params) {
            return HandleCreateTransport(session, params);
        });

    registry.Register(
        "adt_release_transport",
        "Release a transport request for import.",
        MakeSchema(
            {{"transport_number", StringProp("Transport number (e.g., NPLK900001)")}},
            {"transport_number"}),
        [&session](const nlohmann::json& params) {
            return HandleReleaseTransport(session, params);
        });
}

} // namespace erpl_adt
