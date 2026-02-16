#include <erpl_adt/mcp/mcp_tool_handlers.hpp>

#include <erpl_adt/adt/bw_activation.hpp>
#include <erpl_adt/adt/bw_discovery.hpp>
#include <erpl_adt/adt/bw_jobs.hpp>
#include <erpl_adt/adt/bw_object.hpp>
#include <erpl_adt/adt/bw_locks.hpp>
#include <erpl_adt/adt/bw_repo_utils.hpp>
#include <erpl_adt/adt/bw_reporting.hpp>
#include <erpl_adt/adt/bw_search.hpp>
#include <erpl_adt/adt/bw_system.hpp>
#include <erpl_adt/adt/bw_nodes.hpp>
#include <erpl_adt/adt/bw_transport.hpp>
#include <erpl_adt/adt/bw_transport_collect.hpp>
#include <erpl_adt/adt/bw_validation.hpp>
#include <erpl_adt/adt/bw_valuehelp.hpp>
#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_xref.hpp>
#include <erpl_adt/adt/activation.hpp>
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
                          const nlohmann::json& params) {
    XmlCodec codec;
    auto result = Discover(session, codec);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& disc = result.Value();
    auto workspace_filter = OptString(params, "workspace");

    nlohmann::json j;
    nlohmann::json workspaces = nlohmann::json::array();
    for (const auto& ws : disc.workspaces) {
        if (!workspace_filter.empty() && ws.title != workspace_filter) {
            continue;
        }
        nlohmann::json ws_json;
        ws_json["title"] = ws.title;
        nlohmann::json services = nlohmann::json::array();
        for (const auto& s : ws.services) {
            nlohmann::json svc = {{"title", s.title},
                                  {"href", s.href},
                                  {"type", s.type}};
            if (!s.media_types.empty()) {
                svc["media_types"] = s.media_types;
            }
            if (!s.category_term.empty()) {
                svc["category_term"] = s.category_term;
                svc["category_scheme"] = s.category_scheme;
            }
            services.push_back(std::move(svc));
        }
        ws_json["services"] = services;
        workspaces.push_back(std::move(ws_json));
    }
    j["workspaces"] = workspaces;
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

// adt_activate
ToolResult HandleActivateObject(IAdtSession& session,
                                const nlohmann::json& params) {
    ToolResult err;
    auto uri = RequireString(params, "uri", err);
    if (!uri) return err;

    ActivateObjectParams act_params;
    act_params.uri = *uri;

    auto type_str = OptString(params, "type");
    if (!type_str.empty()) act_params.type = type_str;

    auto name_str = OptString(params, "name");
    if (!name_str.empty()) act_params.name = name_str;

    auto result = ActivateObject(session, act_params);
    if (result.IsErr()) return MakeErrorResult(result.Error());

    const auto& act = result.Value();
    nlohmann::json j;
    j["activated"] = act.activated;
    j["failed"] = act.failed;
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& m : act.error_messages) {
        msgs.push_back(m);
    }
    j["error_messages"] = msgs;
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
        "Discover available ADT services and capabilities grouped by workspace. "
        "Returns workspace-grouped service list and feature flags (abapGit, packages, activation).",
        MakeSchema(
            {{"workspace", StringProp("Filter by workspace name (e.g., \"Sources\")")}},
            nlohmann::json::array()),
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

    registry.Register(
        "adt_activate",
        "Activate an inactive ABAP object after writing source code. "
        "Optionally provide type and name for more reliable activation.",
        MakeSchema(
            {{"uri", StringProp("Object URI (e.g., /sap/bc/adt/oo/classes/ZCL_MY_CLASS)")},
             {"type", StringProp("Object type (e.g., CLAS/OC)")},
             {"name", StringProp("Object name (e.g., ZCL_MY_CLASS)")}},
            {"uri"}),
        [&session](const nlohmann::json& params) {
            return HandleActivateObject(session, params);
        });

    // === BW Modeling tools ===

    registry.Register(
        "bw_discover",
        "List available BW Modeling services. Returns scheme/term pairs and URIs "
        "for all BW endpoints the system supports.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwDiscover(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());
            nlohmann::json j = nlohmann::json::array();
            for (const auto& s : result.Value().services) {
                j.push_back({{"scheme", s.scheme},
                             {"term", s.term},
                             {"href", s.href},
                             {"content_type", s.content_type}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_search",
        "Search the BW repository for modeling objects (ADSO, IOBJ, HCPR, TRFN, etc.). "
        "Use wildcards (*). Returns object names, types, statuses, and URIs.",
        MakeSchema(
            {{"query", StringProp("Search pattern with wildcards (e.g., Z*, *SALES*)")},
             {"object_type", StringProp("Filter by type: ADSO, HCPR, IOBJ, TRFN, DTPA, RSDS")},
             {"object_sub_type", StringProp("Filter by subtype (e.g., REP, SOB, RKF)")},
             {"max_results", IntProp("Maximum results (default: 100)")},
             {"status", StringProp("Filter by status: ACT, INA, OFF")},
             {"changed_by", StringProp("Filter by last changed user")},
             {"changed_on_from", StringProp("Changed on or after date")},
             {"changed_on_to", StringProp("Changed on or before date")},
             {"created_by", StringProp("Filter by creator")},
             {"created_on_from", StringProp("Created on or after date")},
             {"created_on_to", StringProp("Created on or before date")},
             {"depends_on_name", StringProp("Filter by dependency object name")},
             {"depends_on_type", StringProp("Filter by dependency object type")},
             {"search_in_description", {{"type", "boolean"},
                                         {"description", "Also search in descriptions"}}}},
            {"query"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto query = RequireString(params, "query", err);
            if (!query) return err;

            BwSearchOptions opts;
            opts.query = *query;
            opts.max_results = OptInt(params, "max_results", 100);
            auto obj_type = OptString(params, "object_type");
            if (!obj_type.empty()) opts.object_type = obj_type;
            auto sub_type = OptString(params, "object_sub_type");
            if (!sub_type.empty()) opts.object_sub_type = sub_type;
            auto status = OptString(params, "status");
            if (!status.empty()) opts.object_status = status;
            auto changed_by = OptString(params, "changed_by");
            if (!changed_by.empty()) opts.changed_by = changed_by;
            auto changed_from = OptString(params, "changed_on_from");
            if (!changed_from.empty()) opts.changed_on_from = changed_from;
            auto changed_to = OptString(params, "changed_on_to");
            if (!changed_to.empty()) opts.changed_on_to = changed_to;
            auto created_by = OptString(params, "created_by");
            if (!created_by.empty()) opts.created_by = created_by;
            auto created_from = OptString(params, "created_on_from");
            if (!created_from.empty()) opts.created_on_from = created_from;
            auto created_to = OptString(params, "created_on_to");
            if (!created_to.empty()) opts.created_on_to = created_to;
            auto dep_name = OptString(params, "depends_on_name");
            if (!dep_name.empty()) opts.depends_on_name = dep_name;
            auto dep_type = OptString(params, "depends_on_type");
            if (!dep_type.empty()) opts.depends_on_type = dep_type;
            if (params.contains("search_in_description") &&
                params["search_in_description"].is_boolean() &&
                params["search_in_description"].get<bool>()) {
                opts.search_in_description = true;
            }

            auto result = BwSearchObjects(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& r : result.Value()) {
                nlohmann::json obj = {{"name", r.name},
                                      {"type", r.type},
                                      {"description", r.description},
                                      {"version", r.version},
                                      {"status", r.status},
                                      {"uri", r.uri}};
                if (!r.technical_name.empty()) obj["technical_name"] = r.technical_name;
                if (!r.last_changed.empty()) obj["last_changed"] = r.last_changed;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_read_object",
        "Read a BW object definition. Returns metadata including name, type, "
        "description, status, package. Use version='m' for inactive version. "
        "Pass uri (from bw_search results) to read objects whose type doesn't "
        "map directly to a REST path segment.",
        MakeSchema(
            {{"object_type", StringProp("Object type code (ADSO, IOBJ, TRFN, etc.). Used for content negotiation.")},
             {"object_name", StringProp("Object name")},
             {"version", StringProp("Version: a (active, default), m (modified), d (delivery)")},
             {"uri", StringProp("Direct URI from search results (overrides type/name path construction)")}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            auto uri_val = OptString(params, "uri");
            bool has_uri = !uri_val.empty();

            BwReadOptions opts;
            opts.object_type = OptString(params, "object_type");
            opts.object_name = OptString(params, "object_name");

            if (!has_uri && (opts.object_type.empty() || opts.object_name.empty())) {
                return MakeParamError("Either 'uri' or both 'object_type' and 'object_name' are required");
            }

            opts.version = OptString(params, "version", "a");
            if (has_uri) {
                opts.uri = uri_val;
            }

            // Resolve content type from discovery (best-effort)
            if (!opts.object_type.empty()) {
                auto disc = BwDiscover(session);
                if (disc.IsOk()) {
                    auto ct = BwResolveContentType(disc.Value(), opts.object_type);
                    if (!ct.empty()) {
                        opts.content_type = ct;
                    }
                }
            }

            auto result = BwReadObject(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& meta = result.Value();
            nlohmann::json j;
            j["name"] = meta.name;
            j["type"] = meta.type;
            j["description"] = meta.description;
            j["version"] = meta.version;
            j["status"] = meta.status;
            j["package"] = meta.package_name;
            j["last_changed_by"] = meta.last_changed_by;
            j["last_changed_at"] = meta.last_changed_at;
            if (!meta.sub_type.empty()) j["sub_type"] = meta.sub_type;
            if (!meta.long_description.empty()) j["long_description"] = meta.long_description;
            if (!meta.short_description.empty()) j["short_description"] = meta.short_description;
            if (!meta.content_state.empty()) j["content_state"] = meta.content_state;
            if (!meta.info_area.empty()) j["info_area"] = meta.info_area;
            if (!meta.responsible.empty()) j["responsible"] = meta.responsible;
            if (!meta.created_at.empty()) j["created_at"] = meta.created_at;
            if (!meta.language.empty()) j["language"] = meta.language;
            if (!meta.properties.empty()) {
                nlohmann::json props = nlohmann::json::object();
                for (const auto& kv : meta.properties) {
                    props[kv.first] = kv.second;
                }
                j["properties"] = props;
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_create_object",
        "Create a BW object. Depending on object type you may provide copy-from parameters or XML content.",
        MakeSchema(
            {{"object_type", StringProp("Object type (ADSO, IOBJ, etc.)")},
             {"object_name", StringProp("Object name")},
             {"package_name", StringProp("Package name")},
             {"copy_from_name", StringProp("Copy source object name")},
             {"copy_from_type", StringProp("Copy source object type")},
             {"content", StringProp("Optional XML payload")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;

            BwCreateOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            auto package_name = OptString(params, "package_name");
            if (!package_name.empty()) opts.package_name = package_name;
            auto copy_from_name = OptString(params, "copy_from_name");
            if (!copy_from_name.empty()) opts.copy_from_name = copy_from_name;
            auto copy_from_type = OptString(params, "copy_from_type");
            if (!copy_from_type.empty()) opts.copy_from_type = copy_from_type;
            auto content = OptString(params, "content");
            if (!content.empty()) opts.content = content;

            auto result = BwCreateObject(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j;
            j["uri"] = result.Value().uri;
            j["http_status"] = result.Value().http_status;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_lock_object",
        "Lock a BW object for editing. Returns lock handle and transport info. "
        "Requires stateful session.",
        MakeSchema(
            {{"object_type", StringProp("Object type (ADSO, IOBJ, etc.)")},
             {"object_name", StringProp("Object name")},
             {"activity", StringProp("Activity: CHAN (default), DELE, MAIN")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;
            auto activity = OptString(params, "activity", "CHAN");

            session.SetStateful(true);
            auto result = BwLockObject(session, *obj_type, *obj_name, activity);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& lock = result.Value();
            nlohmann::json j;
            j["lock_handle"] = lock.lock_handle;
            j["transport"] = lock.transport_number;
            j["timestamp"] = lock.timestamp;
            j["package"] = lock.package_name;
            j["is_local"] = lock.is_local;
            if (!lock.transport_text.empty()) j["transport_text"] = lock.transport_text;
            if (!lock.transport_owner.empty()) j["transport_owner"] = lock.transport_owner;
            if (!lock.foreign_object_locks.empty()) {
                j["foreign_object_locks"] = lock.foreign_object_locks;
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_unlock_object",
        "Release a lock on a BW object.",
        MakeSchema(
            {{"object_type", StringProp("Object type")},
             {"object_name", StringProp("Object name")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;

            auto result = BwUnlockObject(session, *obj_type, *obj_name);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            return MakeOkResult({{"status", "unlocked"}});
        });

    registry.Register(
        "bw_save_object",
        "Save a modified BW object. Requires prior lock.",
        MakeSchema(
            {{"object_type", StringProp("Object type")},
             {"object_name", StringProp("Object name")},
             {"content", StringProp("Modified XML content")},
             {"lock_handle", StringProp("Lock handle from bw_lock_object")},
             {"transport", StringProp("Transport request number")}},
            {"object_type", "object_name", "content", "lock_handle"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;
            auto content = RequireString(params, "content", err);
            if (!content) return err;
            auto handle = RequireString(params, "lock_handle", err);
            if (!handle) return err;

            BwSaveOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            opts.content = *content;
            opts.lock_handle = *handle;
            opts.transport = OptString(params, "transport");

            auto result = BwSaveObject(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            return MakeOkResult({{"status", "saved"}});
        });

    registry.Register(
        "bw_delete_object",
        "Delete a BW object. Requires lock handle.",
        MakeSchema(
            {{"object_type", StringProp("Object type")},
             {"object_name", StringProp("Object name")},
             {"lock_handle", StringProp("Lock handle")},
             {"transport", StringProp("Transport request number")}},
            {"object_type", "object_name", "lock_handle"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;
            auto handle = RequireString(params, "lock_handle", err);
            if (!handle) return err;

            auto result = BwDeleteObject(session, *obj_type, *obj_name,
                                          *handle, OptString(params, "transport"));
            if (result.IsErr()) return MakeErrorResult(result.Error());

            return MakeOkResult({{"status", "deleted"}});
        });

    registry.Register(
        "bw_activate",
        "Activate BW objects. Supports validate (pre-check), simulate (dry run), "
        "and background modes. Can activate multiple objects at once.",
        MakeSchema(
            {{"objects", {{"type", "array"},
                          {"items", {{"type", "object"},
                                     {"properties", {{"name", StringProp("Object name")},
                                                     {"type", StringProp("Object type")}}},
                                     {"required", nlohmann::json::array({"name", "type"})}}},
                          {"description", "List of objects to activate"}}},
             {"mode", StringProp("Mode: activate (default), validate, simulate, background")},
             {"force", {{"type", "boolean"}, {"description", "Force activation with warnings"}}},
             {"transport", StringProp("Transport request number")}},
            {"objects"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            if (!params.contains("objects") || !params["objects"].is_array()) {
                return MakeParamError("Missing required parameter: objects");
            }

            BwActivateOptions opts;
            for (const auto& obj : params["objects"]) {
                BwActivationObject ao;
                ao.name = obj.value("name", "");
                ao.type = obj.value("type", "");
                if (ao.name.empty() || ao.type.empty()) continue;
                ao.uri = "/sap/bw/modeling/" + ao.type + "/" + ao.name + "/m";
                opts.objects.push_back(std::move(ao));
            }

            auto mode = OptString(params, "mode", "activate");
            if (mode == "validate") opts.mode = BwActivationMode::Validate;
            else if (mode == "simulate") opts.mode = BwActivationMode::Simulate;
            else if (mode == "background") opts.mode = BwActivationMode::Background;

            if (params.contains("force") && params["force"].is_boolean()) {
                opts.force = params["force"].get<bool>();
            }
            auto transport = OptString(params, "transport");
            if (!transport.empty()) opts.transport = transport;

            auto result = BwActivateObjects(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& act = result.Value();
            nlohmann::json j;
            j["success"] = act.success;
            if (!act.job_guid.empty()) j["job_guid"] = act.job_guid;
            nlohmann::json msgs = nlohmann::json::array();
            for (const auto& m : act.messages) {
                msgs.push_back({{"severity", m.severity},
                                {"text", m.text},
                                {"object_name", m.object_name},
                                {"object_type", m.object_type}});
            }
            j["messages"] = msgs;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_transport_check",
        "Check BW transport state. Shows changeability, transport requests, "
        "and object lock states.",
        MakeSchema(
            {{"own_only", {{"type", "boolean"},
                           {"description", "Show only own transport requests"}}}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            bool own_only = params.contains("own_only") &&
                            params["own_only"].is_boolean() &&
                            params["own_only"].get<bool>();

            auto result = BwTransportCheck(session, own_only);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& tr = result.Value();
            nlohmann::json j;
            j["writing_enabled"] = tr.writing_enabled;

            nlohmann::json reqs = nlohmann::json::array();
            for (const auto& r : tr.requests) {
                nlohmann::json rj;
                rj["number"] = r.number;
                rj["function_type"] = r.function_type;
                rj["status"] = r.status;
                rj["description"] = r.description;
                nlohmann::json tasks = nlohmann::json::array();
                for (const auto& t : r.tasks) {
                    tasks.push_back({{"number", t.number},
                                     {"function_type", t.function_type},
                                     {"status", t.status},
                                     {"owner", t.owner}});
                }
                rj["tasks"] = tasks;
                reqs.push_back(std::move(rj));
            }
            j["requests"] = reqs;

            nlohmann::json objs = nlohmann::json::array();
            for (const auto& o : tr.objects) {
                nlohmann::json oj = {{"name", o.name},
                                     {"type", o.type},
                                     {"operation", o.operation},
                                     {"lock_request", o.lock_request}};
                if (!o.uri.empty()) oj["uri"] = o.uri;
                if (!o.tadir_status.empty()) oj["tadir_status"] = o.tadir_status;
                objs.push_back(std::move(oj));
            }
            if (!objs.empty()) j["objects"] = objs;

            nlohmann::json chgs = nlohmann::json::array();
            for (const auto& c : tr.changeability) {
                chgs.push_back({{"tlogo", c.tlogo},
                                {"transportable", c.transportable},
                                {"changeable", c.changeable}});
            }
            if (!chgs.empty()) j["changeability"] = chgs;

            if (!tr.messages.empty()) {
                j["messages"] = tr.messages;
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_transport_write",
        "Write a BW object to a transport request.",
        MakeSchema(
            {{"object_type", StringProp("Object type")},
             {"object_name", StringProp("Object name")},
             {"transport", StringProp("Transport request number")},
             {"package", StringProp("Package name")}},
            {"object_type", "object_name", "transport"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;
            auto transport = RequireString(params, "transport", err);
            if (!transport) return err;

            BwTransportWriteOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            opts.transport = *transport;
            opts.package_name = OptString(params, "package");

            auto result = BwTransportWrite(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& wr = result.Value();
            nlohmann::json j;
            j["success"] = wr.success;
            j["messages"] = wr.messages;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_job_status",
        "Get status of a BW background job. Status values: N (new), R (running), "
        "E (error), W (warning), S (success).",
        MakeSchema(
            {{"job_guid", StringProp("25-character job GUID")}},
            {"job_guid"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto guid = RequireString(params, "job_guid", err);
            if (!guid) return err;

            auto result = BwGetJobStatus(session, *guid);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& st = result.Value();
            nlohmann::json j;
            j["guid"] = st.guid;
            j["status"] = st.status;
            j["job_type"] = st.job_type;
            j["description"] = st.description;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_job_restart",
        "Restart a failed BW background job. Job must be in error state (status E).",
        MakeSchema(
            {{"job_guid", StringProp("25-character job GUID")}},
            {"job_guid"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto guid = RequireString(params, "job_guid", err);
            if (!guid) return err;

            auto result = BwRestartJob(session, *guid);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j;
            j["status"] = "restarted";
            j["guid"] = *guid;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_job_cleanup",
        "Cleanup resources of a completed or failed BW background job. "
        "Removes temporary data, logs, and artifacts.",
        MakeSchema(
            {{"job_guid", StringProp("25-character job GUID")}},
            {"job_guid"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto guid = RequireString(params, "job_guid", err);
            if (!guid) return err;

            auto result = BwCleanupJob(session, *guid);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j;
            j["status"] = "cleaned_up";
            j["guid"] = *guid;
            return MakeOkResult(j);
        });

    // === Lock monitoring tools ===

    registry.Register(
        "bw_list_locks",
        "List active BW object locks. Shows who has locked which objects. "
        "Useful for diagnosing lock conflicts.",
        MakeSchema(
            {{"user", StringProp("Filter by lock owner user")},
             {"search", StringProp("Search pattern for object names")},
             {"max_results", IntProp("Maximum results (default: 100)")}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            BwListLocksOptions opts;
            auto user = OptString(params, "user");
            if (!user.empty()) opts.user = user;
            auto search = OptString(params, "search");
            if (!search.empty()) opts.search = search;
            opts.max_results = OptInt(params, "max_results", 100);

            auto result = BwListLocks(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& l : result.Value()) {
                nlohmann::json lj = {{"user", l.user},
                                     {"client", l.client},
                                     {"mode", l.mode},
                                     {"object", l.object},
                                     {"table_name", l.table_name},
                                     {"timestamp", l.timestamp},
                                     {"arg", l.arg},
                                     {"owner1", l.owner1},
                                     {"owner2", l.owner2}};
                if (!l.table_desc.empty()) lj["table_desc"] = l.table_desc;
                if (l.upd_count != 0) lj["upd_count"] = l.upd_count;
                if (l.dia_count != 0) lj["dia_count"] = l.dia_count;
                j.push_back(std::move(lj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_delete_lock",
        "Delete a stuck BW object lock (admin operation). Use values from "
        "bw_list_locks output to identify the lock to delete.",
        MakeSchema(
            {{"user", StringProp("Lock owner user")},
             {"table_name", StringProp("Table name from lock entry (e.g., RSBWOBJ_ENQUEUE)")},
             {"arg", StringProp("Base64-encoded argument from lock entry")},
             {"lock_mode", StringProp("Lock mode (default: E)")},
             {"scope", StringProp("Lock scope (default: 1)")},
             {"owner1", StringProp("Base64-encoded owner1 from lock entry")},
             {"owner2", StringProp("Base64-encoded owner2 from lock entry")}},
            {"user", "table_name", "arg"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto user = RequireString(params, "user", err);
            if (!user) return err;
            auto table_name = RequireString(params, "table_name", err);
            if (!table_name) return err;
            auto arg = RequireString(params, "arg", err);
            if (!arg) return err;

            BwDeleteLockOptions opts;
            opts.user = *user;
            opts.table_name = *table_name;
            opts.arg = *arg;
            opts.lock_mode = OptString(params, "lock_mode", "E");
            opts.scope = OptString(params, "scope", "1");
            opts.owner1 = OptString(params, "owner1");
            opts.owner2 = OptString(params, "owner2");

            auto result = BwDeleteLock(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j;
            j["status"] = "deleted";
            j["user"] = *user;
            return MakeOkResult(j);
        });

    // === System info tools ===

    registry.Register(
        "bw_dbinfo",
        "Get HANA database connection info (host, port, schema, database type).",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetDbInfo(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& info = result.Value();
            nlohmann::json j;
            j["host"] = info.host;
            j["port"] = info.port;
            j["schema"] = info.schema;
            j["database_type"] = info.database_type;
            if (!info.database_name.empty()) j["database_name"] = info.database_name;
            if (!info.instance.empty()) j["instance"] = info.instance;
            if (!info.user.empty()) j["user"] = info.user;
            if (!info.version.empty()) j["version"] = info.version;
            if (!info.patchlevel.empty()) j["patchlevel"] = info.patchlevel;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_sysinfo",
        "Get BW system properties (system configuration key-value pairs).",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetSystemInfo(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& p : result.Value()) {
                j.push_back({{"key", p.key},
                             {"value", p.value},
                             {"description", p.description}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_changeability",
        "Get per-TLOGO changeability and transport settings. Shows which BW object "
        "types can be modified and whether they require transport requests.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetChangeability(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& e : result.Value()) {
                j.push_back({{"object_type", e.object_type},
                             {"changeable", e.changeable},
                             {"transportable", e.transportable},
                             {"description", e.description}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_adturi",
        "Get BW-to-ADT URI mappings. Shows how BW modeling URIs map to "
        "standard ADT URIs for cross-navigation.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetAdtUriMappings(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& m : result.Value()) {
                j.push_back({{"bw_type", m.bw_type},
                             {"adt_type", m.adt_type},
                             {"bw_uri_template", m.bw_uri_template},
                             {"adt_uri_template", m.adt_uri_template}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_xref",
        "Get cross-references (dependencies) for a BW object. Shows which objects "
        "use or are used by the specified object. Useful for impact analysis and "
        "data lineage exploration.",
        MakeSchema(
            {{"object_type", StringProp("Object type (ADSO, IOBJ, TRFN, DTPA, etc.)")},
             {"object_name", StringProp("Object name")},
             {"object_version", StringProp("Version: A (active), M (modified)")},
             {"association", StringProp("Filter by association code (001=Used by, 002=Uses, 003=Depends on)")},
             {"associated_object_type", StringProp("Filter by related type (IOBJ, ADSO, etc.)")},
             {"max_results", IntProp("Maximum number of results to return")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;

            BwXrefOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            auto version = OptString(params, "object_version");
            if (!version.empty()) opts.object_version = version;
            auto assoc = OptString(params, "association");
            if (!assoc.empty()) opts.association = assoc;
            auto assoc_type = OptString(params, "associated_object_type");
            if (!assoc_type.empty()) opts.associated_object_type = assoc_type;
            opts.max_results = OptInt(params, "max_results", 0);

            auto result = BwGetXrefs(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& r : result.Value()) {
                j.push_back({{"name", r.name},
                             {"type", r.type},
                             {"association_type", r.association_type},
                             {"association_label", r.association_label},
                             {"version", r.version},
                             {"status", r.status},
                             {"description", r.description},
                             {"uri", r.uri}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_nodes",
        "Get child node structure of a BW object. Lists component objects "
        "(transformations, DTPs, etc.) belonging to the specified object.",
        MakeSchema(
            {{"object_type", StringProp("Object type (ADSO, IOBJ, etc.)")},
             {"object_name", StringProp("Object name")},
             {"datasource", {{"type", "boolean"},
                              {"description", "Use DataSource structure path"}}},
             {"child_name", StringProp("Filter by child name")},
             {"child_type", StringProp("Filter by child type")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;

            BwNodesOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            opts.datasource = params.contains("datasource") &&
                              params["datasource"].is_boolean() &&
                              params["datasource"].get<bool>();
            auto child_name = OptString(params, "child_name");
            if (!child_name.empty()) opts.child_name = child_name;
            auto child_type = OptString(params, "child_type");
            if (!child_type.empty()) opts.child_type = child_type;

            auto result = BwGetNodes(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& r : result.Value()) {
                j.push_back({{"name", r.name},
                             {"type", r.type},
                             {"subtype", r.subtype},
                             {"description", r.description},
                             {"version", r.version},
                             {"status", r.status},
                             {"uri", r.uri}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_search_metadata",
        "Get BW search metadata definitions (filters, fields, and search domains).",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetSearchMetadata(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& e : result.Value()) {
                j.push_back({{"name", e.name},
                             {"value", e.value},
                             {"description", e.description},
                             {"category", e.category}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_list_favorites",
        "List BW backend favorites entries.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwListBackendFavorites(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& f : result.Value()) {
                j.push_back({{"name", f.name},
                             {"type", f.type},
                             {"description", f.description},
                             {"uri", f.uri}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_clear_favorites",
        "Delete all BW backend favorites.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwDeleteAllBackendFavorites(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j;
            j["status"] = "cleared";
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_nodepath",
        "Resolve the repository node path for a BW object URI.",
        MakeSchema(
            {{"object_uri", StringProp("BW object URI (for example /sap/bw/modeling/adso/ZSALES/a)")}},
            {"object_uri"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto object_uri = RequireString(params, "object_uri", err);
            if (!object_uri) return err;

            auto result = BwGetNodePath(session, *object_uri);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& n : result.Value()) {
                j.push_back({{"name", n.name},
                             {"type", n.type},
                             {"uri", n.uri}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_application_log",
        "Query BW repository application log entries.",
        MakeSchema(
            {{"username", StringProp("Filter by username")},
             {"start_timestamp", StringProp("Filter start timestamp")},
             {"end_timestamp", StringProp("Filter end timestamp")}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            BwApplicationLogOptions opts;
            auto username = OptString(params, "username");
            if (!username.empty()) opts.username = username;
            auto start = OptString(params, "start_timestamp");
            if (!start.empty()) opts.start_timestamp = start;
            auto end = OptString(params, "end_timestamp");
            if (!end.empty()) opts.end_timestamp = end;

            auto result = BwGetApplicationLog(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& e : result.Value()) {
                j.push_back({{"identifier", e.identifier},
                             {"user", e.user},
                             {"timestamp", e.timestamp},
                             {"severity", e.severity},
                             {"text", e.text}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_message_text",
        "Resolve formatted BW message text for a message identifier and text type.",
        MakeSchema(
            {{"identifier", StringProp("Message identifier")},
             {"text_type", StringProp("Text type")},
             {"msgv1", StringProp("Message variable 1")},
             {"msgv2", StringProp("Message variable 2")},
             {"msgv3", StringProp("Message variable 3")},
             {"msgv4", StringProp("Message variable 4")}},
            {"identifier", "text_type"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto identifier = RequireString(params, "identifier", err);
            if (!identifier) return err;
            auto text_type = RequireString(params, "text_type", err);
            if (!text_type) return err;

            BwMessageTextOptions opts;
            opts.identifier = *identifier;
            opts.text_type = *text_type;
            auto msgv1 = OptString(params, "msgv1");
            if (!msgv1.empty()) opts.msgv1 = msgv1;
            auto msgv2 = OptString(params, "msgv2");
            if (!msgv2.empty()) opts.msgv2 = msgv2;
            auto msgv3 = OptString(params, "msgv3");
            if (!msgv3.empty()) opts.msgv3 = msgv3;
            auto msgv4 = OptString(params, "msgv4");
            if (!msgv4.empty()) opts.msgv4 = msgv4;

            auto result = BwGetMessageText(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& msg = result.Value();
            nlohmann::json j;
            j["identifier"] = msg.identifier;
            j["text_type"] = msg.text_type;
            j["text"] = msg.text;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_validate",
        "Run BW validation endpoint for an object.",
        MakeSchema(
            {{"object_type", StringProp("Object type")},
             {"object_name", StringProp("Object name")},
             {"action", StringProp("Validation action (default: validate)")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto object_type = RequireString(params, "object_type", err);
            if (!object_type) return err;
            auto object_name = RequireString(params, "object_name", err);
            if (!object_name) return err;

            BwValidationOptions opts;
            opts.object_type = *object_type;
            opts.object_name = *object_name;
            opts.action = OptString(params, "action", "validate");

            auto result = BwValidateObject(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& m : result.Value()) {
                j.push_back({{"severity", m.severity},
                             {"text", m.text},
                             {"object_type", m.object_type},
                             {"object_name", m.object_name},
                             {"code", m.code}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_move_requests",
        "List BW move request entries.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwListMoveRequests(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& e : result.Value()) {
                j.push_back({{"request", e.request},
                             {"owner", e.owner},
                             {"status", e.status},
                             {"description", e.description}});
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_valuehelp",
        "Read BW value-help endpoints under /sap/bw/modeling/is/values/*.",
        MakeSchema(
            {{"domain", StringProp("Value-help domain (for example infoareas, infoobject)")},
             {"query", StringProp("Raw query string k=v&k2=v2")},
             {"max_rows", IntProp("Maximum rows")},
             {"pattern", StringProp("Pattern filter")},
             {"object_type", StringProp("Object type filter")},
             {"infoprovider", StringProp("InfoProvider filter")}},
            {"domain"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto domain = RequireString(params, "domain", err);
            if (!domain) return err;

            BwValueHelpOptions opts;
            opts.domain = *domain;
            auto query = OptString(params, "query");
            if (!query.empty()) opts.raw_query = query;
            if (params.contains("max_rows") && params["max_rows"].is_number_integer()) {
                opts.max_rows = params["max_rows"].get<int>();
            }
            auto pattern = OptString(params, "pattern");
            if (!pattern.empty()) opts.pattern = pattern;
            auto object_type = OptString(params, "object_type");
            if (!object_type.empty()) opts.object_type = object_type;
            auto infoprovider = OptString(params, "infoprovider");
            if (!infoprovider.empty()) opts.infoprovider = infoprovider;

            auto result = BwGetValueHelp(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& row : result.Value()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& [k, v] : row.fields) obj[k] = v;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_virtualfolders",
        "Read BW virtual folders endpoint when available.",
        MakeSchema(
            {{"package_name", StringProp("Package filter")},
             {"object_type", StringProp("Object type filter")},
             {"user_name", StringProp("User filter")}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            std::optional<std::string> package_name;
            std::optional<std::string> object_type;
            std::optional<std::string> user_name;
            auto p = OptString(params, "package_name");
            if (!p.empty()) package_name = p;
            auto t = OptString(params, "object_type");
            if (!t.empty()) object_type = t;
            auto u = OptString(params, "user_name");
            if (!u.empty()) user_name = u;

            auto result = BwGetVirtualFolders(session, package_name, object_type, user_name);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& row : result.Value()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& [k, v] : row.fields) obj[k] = v;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_datavolumes",
        "Read BW data volumes endpoint when available.",
        MakeSchema(
            {{"infoprovider", StringProp("InfoProvider filter")},
             {"max_rows", IntProp("Maximum rows")}},
            {}),
        [&session](const nlohmann::json& params) -> ToolResult {
            std::optional<std::string> infoprovider;
            std::optional<int> max_rows;
            auto ip = OptString(params, "infoprovider");
            if (!ip.empty()) infoprovider = ip;
            if (params.contains("max_rows") && params["max_rows"].is_number_integer()) {
                max_rows = params["max_rows"].get<int>();
            }

            auto result = BwGetDataVolumes(session, infoprovider, max_rows);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& row : result.Value()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& [k, v] : row.fields) obj[k] = v;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_reporting",
        "Run BW reporting metadata request (BICS reporting endpoint).",
        MakeSchema(
            {{"compid", StringProp("Query/component id")},
             {"dbgmode", {{"type", "boolean"}, {"description", "Set dbgmode=true"}}},
             {"metadata_only", {{"type", "boolean"}, {"description", "MetadataOnly header"}}},
             {"incl_metadata", {{"type", "boolean"}, {"description", "InclMetadata header"}}},
             {"incl_object_values", {{"type", "boolean"}, {"description", "InclObjectValues header"}}},
             {"incl_except_def", {{"type", "boolean"}, {"description", "InclExceptDef header"}}},
             {"compact_mode", {{"type", "boolean"}, {"description", "CompactMode header"}}},
             {"from_row", IntProp("FromRow header")},
             {"to_row", IntProp("ToRow header")}},
            {"compid"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto compid = RequireString(params, "compid", err);
            if (!compid) return err;

            BwReportingOptions opts;
            opts.compid = *compid;
            opts.dbgmode = params.value("dbgmode", false);
            opts.metadata_only = params.value("metadata_only", false);
            opts.incl_metadata = params.value("incl_metadata", false);
            opts.incl_object_values = params.value("incl_object_values", false);
            opts.incl_except_def = params.value("incl_except_def", false);
            opts.compact_mode = params.value("compact_mode", false);
            if (params.contains("from_row") && params["from_row"].is_number_integer()) {
                opts.from_row = params["from_row"].get<int>();
            }
            if (params.contains("to_row") && params["to_row"].is_number_integer()) {
                opts.to_row = params["to_row"].get<int>();
            }

            auto result = BwGetReportingMetadata(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& row : result.Value()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& [k, v] : row.fields) obj[k] = v;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_query_properties",
        "Read BW query properties rules endpoint.",
        MakeSchema({}, {}),
        [&session](const nlohmann::json&) -> ToolResult {
            auto result = BwGetQueryProperties(session);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            nlohmann::json j = nlohmann::json::array();
            for (const auto& row : result.Value()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& [k, v] : row.fields) obj[k] = v;
                j.push_back(std::move(obj));
            }
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_transport_collect",
        "Collect dependent objects for transport. Gathers objects related to "
        "the specified object with dataflow grouping. Mode: 000=necessary, "
        "001=complete, 003=dataflow above, 004=dataflow below.",
        MakeSchema(
            {{"object_type", StringProp("Object type (ADSO, IOBJ, etc.)")},
             {"object_name", StringProp("Object name")},
             {"mode", StringProp("Collection mode: 000 (necessary), 001 (complete), 003 (above), 004 (below)")},
             {"transport", StringProp("Transport request number (optional)")}},
            {"object_type", "object_name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto obj_type = RequireString(params, "object_type", err);
            if (!obj_type) return err;
            auto obj_name = RequireString(params, "object_name", err);
            if (!obj_name) return err;

            BwTransportCollectOptions opts;
            opts.object_type = *obj_type;
            opts.object_name = *obj_name;
            opts.mode = OptString(params, "mode", "000");
            auto transport = OptString(params, "transport");
            if (!transport.empty()) opts.transport = transport;

            auto result = BwTransportCollect(session, opts);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& cr = result.Value();
            nlohmann::json j;
            nlohmann::json details = nlohmann::json::array();
            for (const auto& d : cr.details) {
                details.push_back({{"name", d.name},
                                   {"type", d.type},
                                   {"description", d.description},
                                   {"status", d.status},
                                   {"uri", d.uri},
                                   {"last_changed_by", d.last_changed_by},
                                   {"last_changed_at", d.last_changed_at}});
            }
            j["details"] = details;

            nlohmann::json deps = nlohmann::json::array();
            for (const auto& d : cr.dependencies) {
                deps.push_back({{"name", d.name},
                                {"type", d.type},
                                {"version", d.version},
                                {"author", d.author},
                                {"package", d.package_name},
                                {"association_type", d.association_type},
                                {"associated_name", d.associated_name},
                                {"associated_type", d.associated_type}});
            }
            j["dependencies"] = deps;
            j["messages"] = cr.messages;
            return MakeOkResult(j);
        });

    // === Structured lineage tools ===

    registry.Register(
        "bw_read_transformation",
        "Read a BW transformation (TRFN) with parsed field mappings. Returns "
        "source/target objects, source and target field lists, and transformation "
        "rules (field-to-field mappings with rule types like StepDirect, StepFormula).",
        MakeSchema(
            {{"name", StringProp("Transformation name")},
             {"version", StringProp("Version: a (active, default), m (modified)")}},
            {"name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto name = RequireString(params, "name", err);
            if (!name) return err;
            auto version = OptString(params, "version", "a");

            // Resolve content type from discovery (best-effort)
            std::string resolved_ct;
            {
                auto disc = BwDiscover(session);
                if (disc.IsOk()) {
                    resolved_ct = BwResolveContentType(disc.Value(), "TRFN");
                }
            }

            auto result = BwReadTransformation(session, *name, version, resolved_ct);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& detail = result.Value();
            nlohmann::json j;
            j["name"] = detail.name;
            j["description"] = detail.description;
            j["source_name"] = detail.source_name;
            j["source_type"] = detail.source_type;
            j["target_name"] = detail.target_name;
            j["target_type"] = detail.target_type;

            nlohmann::json src_fields = nlohmann::json::array();
            for (const auto& f : detail.source_fields) {
                src_fields.push_back({{"name", f.name},
                                      {"type", f.type},
                                      {"aggregation", f.aggregation},
                                      {"key", f.key}});
            }
            j["source_fields"] = src_fields;

            nlohmann::json tgt_fields = nlohmann::json::array();
            for (const auto& f : detail.target_fields) {
                tgt_fields.push_back({{"name", f.name},
                                      {"type", f.type},
                                      {"aggregation", f.aggregation},
                                      {"key", f.key}});
            }
            j["target_fields"] = tgt_fields;

            nlohmann::json rules = nlohmann::json::array();
            for (const auto& r : detail.rules) {
                nlohmann::json rj;
                rj["source_field"] = r.source_field;
                rj["target_field"] = r.target_field;
                rj["rule_type"] = r.rule_type;
                if (!r.formula.empty()) rj["formula"] = r.formula;
                if (!r.constant.empty()) rj["constant"] = r.constant;
                rules.push_back(std::move(rj));
            }
            j["rules"] = rules;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_read_adso",
        "Read a BW Advanced DataStore Object (ADSO) with parsed field list. "
        "Returns metadata and all fields with data types, lengths, and key flags.",
        MakeSchema(
            {{"name", StringProp("ADSO name")},
             {"version", StringProp("Version: a (active, default), m (modified)")}},
            {"name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto name = RequireString(params, "name", err);
            if (!name) return err;
            auto version = OptString(params, "version", "a");

            // Resolve content type from discovery (best-effort)
            std::string resolved_ct;
            {
                auto disc = BwDiscover(session);
                if (disc.IsOk()) {
                    resolved_ct = BwResolveContentType(disc.Value(), "ADSO");
                }
            }

            auto result = BwReadAdsoDetail(session, *name, version, resolved_ct);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& detail = result.Value();
            nlohmann::json j;
            j["name"] = detail.name;
            j["description"] = detail.description;
            j["package"] = detail.package_name;

            nlohmann::json fields = nlohmann::json::array();
            for (const auto& f : detail.fields) {
                fields.push_back({{"name", f.name},
                                  {"data_type", f.data_type},
                                  {"length", f.length},
                                  {"decimals", f.decimals},
                                  {"key", f.key},
                                  {"description", f.description},
                                  {"info_object", f.info_object}});
            }
            j["fields"] = fields;
            return MakeOkResult(j);
        });

    registry.Register(
        "bw_read_dtp",
        "Read a BW Data Transfer Process (DTP) with parsed source/target. "
        "Returns source and target object names, types, and source system info.",
        MakeSchema(
            {{"name", StringProp("DTP name")},
             {"version", StringProp("Version: a (active, default), m (modified)")}},
            {"name"}),
        [&session](const nlohmann::json& params) -> ToolResult {
            ToolResult err;
            auto name = RequireString(params, "name", err);
            if (!name) return err;
            auto version = OptString(params, "version", "a");

            // Resolve content type from discovery (best-effort)
            std::string resolved_ct;
            {
                auto disc = BwDiscover(session);
                if (disc.IsOk()) {
                    resolved_ct = BwResolveContentType(disc.Value(), "DTPA");
                }
            }

            auto result = BwReadDtpDetail(session, *name, version, resolved_ct);
            if (result.IsErr()) return MakeErrorResult(result.Error());

            const auto& detail = result.Value();
            nlohmann::json j;
            j["name"] = detail.name;
            j["description"] = detail.description;
            j["source_name"] = detail.source_name;
            j["source_type"] = detail.source_type;
            j["target_name"] = detail.target_name;
            j["target_type"] = detail.target_type;
            j["source_system"] = detail.source_system;
            return MakeOkResult(j);
        });
}

} // namespace erpl_adt
