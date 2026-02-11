#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/login_wizard.hpp>
#include <erpl_adt/cli/output_formatter.hpp>
#include <erpl_adt/core/ansi.hpp>
#include <erpl_adt/core/terminal.hpp>

#include <erpl_adt/adt/adt_session.hpp>
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

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace erpl_adt {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const std::set<std::string> kNewStyleGroups = {
    "search", "object", "source", "test", "check",
    "transport", "ddic", "package", "discover"};

constexpr const char* kCredsFile = ".adt.creds";

struct SavedCredentials {
    std::string host;
    uint16_t port = 50000;
    std::string user;
    std::string password;
    std::string client;
    bool use_https = false;
};

bool SaveCredentials(const SavedCredentials& creds) {
    nlohmann::json j;
    j["host"] = creds.host;
    j["port"] = creds.port;
    j["user"] = creds.user;
    j["password"] = creds.password;
    j["client"] = creds.client;
    j["use_https"] = creds.use_https;

    std::ofstream ofs(kCredsFile);
    if (!ofs) {
        return false;
    }
    ofs << j.dump(2) << "\n";
    ofs.close();

#ifndef _WIN32
    // Set file permissions to owner read/write only (chmod 600).
    chmod(kCredsFile, S_IRUSR | S_IWUSR);
#endif
    return true;
}

std::optional<SavedCredentials> LoadCredentials() {
    std::ifstream ifs(kCredsFile);
    if (!ifs) {
        return std::nullopt;
    }
    try {
        auto j = nlohmann::json::parse(ifs);
        SavedCredentials creds;
        creds.host = j.value("host", "");
        creds.port = j.value("port", static_cast<uint16_t>(50000));
        creds.user = j.value("user", "");
        creds.password = j.value("password", "");
        creds.client = j.value("client", "001");
        creds.use_https = j.value("use_https", false);
        return creds;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool DeleteCredentials() {
    return std::remove(kCredsFile) == 0;
}

std::string GetFlag(const CommandArgs& args, const std::string& key,
                    const std::string& default_val = "") {
    auto it = args.flags.find(key);
    return (it != args.flags.end()) ? it->second : default_val;
}

bool HasFlag(const CommandArgs& args, const std::string& key) {
    return args.flags.count(key) > 0;
}

bool JsonMode(const CommandArgs& args) {
    return GetFlag(args, "json") == "true";
}

bool ColorMode(const CommandArgs& args) {
    if (JsonMode(args)) return false;
    if (GetFlag(args, "no-color") == "true") return false;
    if (NoColorEnvSet()) return false;
    if (GetFlag(args, "color") == "true") return true;
    return IsStdoutTty();
}

Error MakeValidationError(const std::string& message) {
    return Error{"Validation", "", std::nullopt, message, std::nullopt};
}

// Create an AdtSession from CommandArgs flags.
std::unique_ptr<AdtSession> CreateSession(const CommandArgs& args) {
    auto creds = LoadCredentials();

    auto host = GetFlag(args, "host", creds ? creds->host : "localhost");
    auto port_str = GetFlag(args, "port",
                            creds ? std::to_string(creds->port) : "50000");
    auto port = static_cast<uint16_t>(std::stoi(port_str));
    auto use_https = HasFlag(args, "https")
                         ? GetFlag(args, "https") == "true"
                         : (creds ? creds->use_https : false);
    auto user = GetFlag(args, "user", creds ? creds->user : "DEVELOPER");
    auto client_str = GetFlag(args, "client",
                              creds ? creds->client : "001");
    auto password = GetFlag(args, "password");

    // Resolve password: explicit flag > env var > saved creds.
    if (password.empty()) {
        auto env_var = GetFlag(args, "password-env", "SAP_PASSWORD");
        const char* env_val = std::getenv(env_var.c_str());
        if (env_val != nullptr) {
            password = env_val;
        }
    }
    if (password.empty() && creds) {
        password = creds->password;
    }

    auto sap_client = SapClient::Create(client_str).Value();

    AdtSessionOptions opts;
    if (HasFlag(args, "timeout")) {
        opts.read_timeout =
            std::chrono::seconds(std::stoi(GetFlag(args, "timeout")));
    }
    if (use_https) {
        opts.disable_tls_verify = GetFlag(args, "insecure") == "true";
    }

    auto session = std::make_unique<AdtSession>(host, port, use_https, user,
                                                password, sap_client, opts);

    // Load session file if present.
    auto session_file = GetFlag(args, "session-file");
    if (!session_file.empty()) {
        std::ifstream probe(session_file);
        if (probe.good()) {
            (void)session->LoadSession(session_file);
        }
    }

    return session;
}

// Save session file after stateful operations.
void MaybeSaveSession(const AdtSession& session, const CommandArgs& args) {
    auto session_file = GetFlag(args, "session-file");
    if (!session_file.empty()) {
        (void)session.SaveSession(session_file);
    }
}

// Delete session file (e.g., after unlock).
void MaybeDeleteSessionFile(const CommandArgs& args) {
    auto session_file = GetFlag(args, "session-file");
    if (!session_file.empty()) {
        std::remove(session_file.c_str());
    }
}

// ---------------------------------------------------------------------------
// search query
// ---------------------------------------------------------------------------
int HandleSearchQuery(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing search pattern. Usage: erpl-adt search <pattern> [--type=CLAS] [--max=N]"));
        return 99;
    }

    auto session = CreateSession(args);
    SearchOptions opts;
    opts.query = args.positional[0];
    if (HasFlag(args, "max")) {
        opts.max_results = std::stoi(GetFlag(args, "max"));
    }
    if (HasFlag(args, "type")) {
        opts.object_type = GetFlag(args, "type");
    }

    auto result = SearchObjects(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    auto items = std::move(result).Value();
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : items) {
            j.push_back({{"name", r.name},
                         {"type", r.type},
                         {"uri", r.uri},
                         {"description", r.description},
                         {"package", r.package_name}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "Package", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : items) {
            rows.push_back({r.name, r.type, r.package_name, r.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// object read
// ---------------------------------------------------------------------------
int HandleObjectRead(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing object URI. Usage: erpl-adt object read <uri>"));
        return 99;
    }

    auto uri_result = ObjectUri::Create(args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid URI: " + uri_result.Error()));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = GetObjectStructure(*session, uri_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& obj = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::cout << obj.info.name << " (" << obj.info.type << ")\n";
        std::cout << "  URI: " << obj.info.uri << "\n";
        std::cout << "  Description: " << obj.info.description << "\n";
        if (!obj.includes.empty()) {
            std::cout << "  Includes:\n";
            for (const auto& inc : obj.includes) {
                std::cout << "    " << inc.include_type << ": " << inc.source_uri << "\n";
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// object create
// ---------------------------------------------------------------------------
int HandleObjectCreate(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    auto obj_type = GetFlag(args, "type");
    auto name = GetFlag(args, "name");
    auto package = GetFlag(args, "package");
    auto description = GetFlag(args, "description");

    if (obj_type.empty() || name.empty() || package.empty()) {
        fmt.PrintError(MakeValidationError(
            "Missing required flags. Usage: erpl-adt object create --type <type> --name <name> --package <pkg>"));
        return 99;
    }

    CreateObjectParams params;
    params.object_type = obj_type;
    params.name = name;
    params.package_name = package;
    params.description = description;
    if (HasFlag(args, "transport")) {
        params.transport_number = GetFlag(args, "transport");
    }

    auto session = CreateSession(args);
    auto result = CreateObject(*session, params);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["uri"] = result.Value().Value();
        fmt.PrintJson(j.dump());
    } else {
        fmt.PrintSuccess("Created: " + result.Value().Value());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// object delete
// ---------------------------------------------------------------------------
int HandleObjectDelete(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing object URI. Usage: erpl-adt object delete <uri>"));
        return 99;
    }

    auto uri_result = ObjectUri::Create(args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid URI: " + uri_result.Error()));
        return 99;
    }

    std::optional<std::string> transport;
    if (HasFlag(args, "transport")) {
        transport = GetFlag(args, "transport");
    }

    auto session = CreateSession(args);
    auto handle_str = GetFlag(args, "handle");

    if (!handle_str.empty()) {
        // Explicit handle: use it directly (advanced / session-file mode).
        auto handle_result = LockHandle::Create(handle_str);
        if (handle_result.IsErr()) {
            fmt.PrintError(MakeValidationError("Invalid handle: " + handle_result.Error()));
            return 99;
        }
        auto result = DeleteObject(*session, uri_result.Value(),
                                   handle_result.Value(), transport);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
    } else {
        // Auto-lock mode: lock → delete → unlock in a single session.
        session->SetStateful(true);
        auto lock_result = LockObject(*session, uri_result.Value());
        if (lock_result.IsErr()) {
            fmt.PrintError(lock_result.Error());
            return lock_result.Error().ExitCode();
        }
        auto del_result = DeleteObject(*session, uri_result.Value(),
                                       lock_result.Value().handle, transport);
        auto unlock_result = UnlockObject(*session, uri_result.Value(),
                                          lock_result.Value().handle);
        session->SetStateful(false);
        if (del_result.IsErr()) {
            fmt.PrintError(del_result.Error());
            return del_result.Error().ExitCode();
        }
    }

    fmt.PrintSuccess("Deleted: " + args.positional[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// object lock
// ---------------------------------------------------------------------------
int HandleObjectLock(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing object URI. Usage: erpl-adt object lock <uri>"));
        return 99;
    }

    auto uri_result = ObjectUri::Create(args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid URI: " + uri_result.Error()));
        return 99;
    }

    auto session = CreateSession(args);
    session->SetStateful(true);

    auto result = LockObject(*session, uri_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    MaybeSaveSession(*session, args);

    const auto& lock = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["handle"] = lock.handle.Value();
        j["transport_number"] = lock.transport_number;
        j["transport_owner"] = lock.transport_owner;
        j["transport_text"] = lock.transport_text;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Locked: " << args.positional[0] << "\n";
        std::cout << "  Handle: " << lock.handle.Value() << "\n";
        if (!lock.transport_number.empty()) {
            std::cout << "  Transport: " << lock.transport_number << "\n";
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// object unlock
// ---------------------------------------------------------------------------
int HandleObjectUnlock(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing object URI. Usage: erpl-adt object unlock <uri>"));
        return 99;
    }
    auto handle_str = GetFlag(args, "handle");
    if (handle_str.empty()) {
        fmt.PrintError(MakeValidationError("Missing --handle flag"));
        return 99;
    }

    auto uri_result = ObjectUri::Create(args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid URI: " + uri_result.Error()));
        return 99;
    }
    auto handle_result = LockHandle::Create(handle_str);
    if (handle_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid handle: " + handle_result.Error()));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = UnlockObject(*session, uri_result.Value(), handle_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    MaybeDeleteSessionFile(args);
    fmt.PrintSuccess("Unlocked: " + args.positional[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// source read
// ---------------------------------------------------------------------------
int HandleSourceRead(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing source URI. Usage: erpl-adt source read <uri>"));
        return 99;
    }

    auto version = GetFlag(args, "version", "active");
    auto session = CreateSession(args);
    auto result = ReadSource(*session, args.positional[0], version);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["source"] = result.Value();
        fmt.PrintJson(j.dump());
    } else {
        std::cout << result.Value();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// source write
// ---------------------------------------------------------------------------
int HandleSourceWrite(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing source URI. Usage: erpl-adt source write <uri> --file <path>"));
        return 99;
    }
    auto file_path = GetFlag(args, "file");
    if (file_path.empty()) {
        fmt.PrintError(MakeValidationError("Missing --file flag"));
        return 99;
    }

    // Read source from file.
    std::ifstream ifs(file_path);
    if (!ifs) {
        fmt.PrintError(MakeValidationError("Cannot open file: " + file_path));
        return 99;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

    std::optional<std::string> transport;
    if (HasFlag(args, "transport")) {
        transport = GetFlag(args, "transport");
    }

    auto session = CreateSession(args);
    auto handle_str = GetFlag(args, "handle");

    if (!handle_str.empty()) {
        // Explicit handle: use it directly (advanced / session-file mode).
        auto handle_result = LockHandle::Create(handle_str);
        if (handle_result.IsErr()) {
            fmt.PrintError(MakeValidationError("Invalid handle: " + handle_result.Error()));
            return 99;
        }
        auto result = WriteSource(*session, args.positional[0], source,
                                  handle_result.Value(), transport);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        MaybeSaveSession(*session, args);
    } else {
        // Auto-lock mode: derive object URI, lock → write → unlock.
        auto source_uri = args.positional[0];
        auto slash_pos = source_uri.find("/source/");
        if (slash_pos == std::string::npos) {
            fmt.PrintError(MakeValidationError(
                "Cannot derive object URI from source URI (expected /source/ segment): " +
                source_uri));
            return 99;
        }
        auto obj_uri_str = source_uri.substr(0, slash_pos);
        auto obj_uri = ObjectUri::Create(obj_uri_str);
        if (obj_uri.IsErr()) {
            fmt.PrintError(MakeValidationError("Invalid object URI: " + obj_uri.Error()));
            return 99;
        }

        session->SetStateful(true);
        auto lock_result = LockObject(*session, obj_uri.Value());
        if (lock_result.IsErr()) {
            fmt.PrintError(lock_result.Error());
            return lock_result.Error().ExitCode();
        }
        auto write_result = WriteSource(*session, source_uri, source,
                                        lock_result.Value().handle, transport);
        auto unlock_result = UnlockObject(*session, obj_uri.Value(),
                                          lock_result.Value().handle);
        session->SetStateful(false);
        if (write_result.IsErr()) {
            fmt.PrintError(write_result.Error());
            return write_result.Error().ExitCode();
        }
    }

    fmt.PrintSuccess("Source written: " + args.positional[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// source check
// ---------------------------------------------------------------------------
int HandleSourceCheck(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing source URI. Usage: erpl-adt source check <uri>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = CheckSyntax(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& messages = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& m : messages) {
            j.push_back({{"type", m.type},
                         {"text", m.text},
                         {"uri", m.uri},
                         {"line", m.line},
                         {"offset", m.offset}});
        }
        fmt.PrintJson(j.dump());
    } else {
        if (messages.empty()) {
            fmt.PrintSuccess("No syntax errors");
        } else {
            std::vector<std::string> headers = {"Type", "Line", "Text"};
            std::vector<std::vector<std::string>> rows;
            for (const auto& m : messages) {
                rows.push_back({m.type, std::to_string(m.line), m.text});
            }
            fmt.PrintTable(headers, rows);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// test run
// ---------------------------------------------------------------------------
int HandleTestRun(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing test URI. Usage: erpl-adt test run <uri>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = RunTests(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& tr = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Test results: " << tr.TotalMethods() << " methods, "
                  << tr.TotalFailed() << " failed\n";
        for (const auto& c : tr.classes) {
            for (const auto& m : c.methods) {
                auto status = m.Passed() ? "PASS" : "FAIL";
                std::cout << "  [" << status << "] " << c.name << "->" << m.name << "\n";
                for (const auto& a : m.alerts) {
                    std::cout << "    " << a.severity << ": " << a.title << "\n";
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// check run
// ---------------------------------------------------------------------------
int HandleCheckRun(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing object URI. Usage: erpl-adt check run <uri>"));
        return 99;
    }

    auto variant = GetFlag(args, "variant", "DEFAULT");
    auto session = CreateSession(args);
    auto result = RunAtcCheck(*session, args.positional[0], variant);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& atc = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "ATC Check: " << atc.ErrorCount() << " errors, "
                  << atc.WarningCount() << " warnings\n";
        for (const auto& f : atc.findings) {
            auto prio = f.priority == 1 ? "ERR" : (f.priority == 2 ? "WARN" : "INFO");
            std::cout << "  [" << prio << "] " << f.message << "\n";
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// transport list
// ---------------------------------------------------------------------------
int HandleTransportList(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    auto user = GetFlag(args, "user", "DEVELOPER");

    auto session = CreateSession(args);
    auto result = ListTransports(*session, user);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    auto transports = std::move(result).Value();
    std::sort(transports.begin(), transports.end(), [](const auto& a, const auto& b) {
        return a.number < b.number;
    });

    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& t : transports) {
            j.push_back({{"number", t.number},
                         {"description", t.description},
                         {"owner", t.owner},
                         {"status", t.status},
                         {"target", t.target}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Number", "Description", "Owner", "Status"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& t : transports) {
            rows.push_back({t.number, t.description, t.owner, t.status});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// transport create
// ---------------------------------------------------------------------------
int HandleTransportCreate(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    auto desc = GetFlag(args, "desc");
    auto pkg = GetFlag(args, "package");
    if (desc.empty()) {
        fmt.PrintError(MakeValidationError("Missing --desc flag"));
        return 99;
    }
    if (pkg.empty()) {
        fmt.PrintError(MakeValidationError("Missing --package flag"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = CreateTransport(*session, desc, pkg);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["transport_number"] = result.Value();
        fmt.PrintJson(j.dump());
    } else {
        fmt.PrintSuccess("Created transport: " + result.Value());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// transport release
// ---------------------------------------------------------------------------
int HandleTransportRelease(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing transport number. Usage: erpl-adt transport release <number>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = ReleaseTransport(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    fmt.PrintSuccess("Released transport: " + args.positional[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// ddic table
// ---------------------------------------------------------------------------
int HandleDdicTable(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing table name. Usage: erpl-adt ddic table <name>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = GetTableDefinition(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& table = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::cout << table.name << " — " << table.description << "\n";
        std::vector<std::string> headers = {"Field", "Type", "Key", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& f : table.fields) {
            rows.push_back({f.name, f.type, f.key_field ? "Y" : "", f.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ddic cds
// ---------------------------------------------------------------------------
int HandleDdicCds(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing CDS name. Usage: erpl-adt ddic cds <name>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = GetCdsSource(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["source"] = result.Value();
        fmt.PrintJson(j.dump());
    } else {
        std::cout << result.Value();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// package list
// ---------------------------------------------------------------------------
int HandlePackageList(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing package name. Usage: erpl-adt package list <name>"));
        return 99;
    }

    auto session = CreateSession(args);
    auto result = ListPackageContents(*session, args.positional[0]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    auto entries = std::move(result).Value();
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.object_name < b.object_name;
    });

    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries) {
            j.push_back({{"object_type", e.object_type},
                         {"object_name", e.object_name},
                         {"object_uri", e.object_uri},
                         {"description", e.description}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Type", "Name", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            rows.push_back({e.object_type, e.object_name, e.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// package tree
// ---------------------------------------------------------------------------
int HandlePackageTree(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Missing package name. Usage: erpl-adt package tree <name> [--type=CLAS]"));
        return 99;
    }

    PackageTreeOptions opts;
    opts.root_package = args.positional[0];
    if (HasFlag(args, "type")) {
        opts.type_filter = GetFlag(args, "type");
    }
    if (HasFlag(args, "max-depth")) {
        opts.max_depth = std::stoi(GetFlag(args, "max-depth"));
    }

    auto session = CreateSession(args);
    auto result = ListPackageTree(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    auto entries = std::move(result).Value();
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.object_name < b.object_name;
    });

    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries) {
            j.push_back({{"object_type", e.object_type},
                         {"object_name", e.object_name},
                         {"object_uri", e.object_uri},
                         {"description", e.description},
                         {"package", e.package_name}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Type", "Name", "Package", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            rows.push_back({e.object_type, e.object_name, e.package_name, e.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// package exists
// ---------------------------------------------------------------------------
int HandlePackageExists(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError("Missing package name. Usage: erpl-adt package exists <name>"));
        return 99;
    }

    auto pkg_result = PackageName::Create(args.positional[0]);
    if (pkg_result.IsErr()) {
        fmt.PrintError(MakeValidationError("Invalid package name: " + pkg_result.Error()));
        return 99;
    }

    auto session = CreateSession(args);
    XmlCodec codec;
    auto result = PackageExists(*session, codec, pkg_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["exists"] = result.Value();
        j["package"] = args.positional[0];
        fmt.PrintJson(j.dump());
    } else {
        if (result.Value()) {
            fmt.PrintSuccess("Package exists: " + args.positional[0]);
        } else {
            std::cout << "Package not found: " << args.positional[0] << "\n";
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// discover services
// ---------------------------------------------------------------------------
int HandleDiscoverServices(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    auto session = CreateSession(args);
    XmlCodec codec;
    auto result = Discover(*session, codec);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& disc = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "ADT Services:\n";
        for (const auto& s : disc.services) {
            std::cout << "  " << s.title << " → " << s.href << "\n";
        }
        std::cout << "\nCapabilities:\n";
        std::cout << "  abapGit: " << (disc.has_abapgit_support ? "yes" : "no") << "\n";
        std::cout << "  Packages: " << (disc.has_packages_support ? "yes" : "no") << "\n";
        std::cout << "  Activation: " << (disc.has_activation_support ? "yes" : "no") << "\n";
    }
    return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PrintTopLevelHelp
// ---------------------------------------------------------------------------

// Ansi helper — wraps ostream + color flag.
namespace {

struct Ansi {
    std::ostream& out;
    bool color;

    Ansi& Bold(const std::string& s) {
        if (color) out << ansi::kBold;
        out << s;
        if (color) out << ansi::kReset;
        return *this;
    }

    Ansi& Dim(const std::string& s) {
        if (color) out << ansi::kDim;
        out << s;
        if (color) out << ansi::kReset;
        return *this;
    }

    Ansi& Yellow(const std::string& s) {
        if (color) out << ansi::kYellow;
        out << s;
        if (color) out << ansi::kReset;
        return *this;
    }

    Ansi& Normal(const std::string& s) {
        out << s;
        return *this;
    }

    Ansi& Nl() {
        out << "\n";
        return *this;
    }
};

// Command display info for column alignment.
struct CmdDisplay {
    std::string left;  // e.g. "  search <pattern>"
    std::string desc;  // e.g. "Search for ABAP objects"
    std::vector<FlagHelp> flags;  // copied, since CommandsForGroup returns by value
};

} // anonymous namespace (Ansi helper)

void PrintTopLevelHelp(const CommandRouter& router, std::ostream& out, bool color) {
    Ansi a{out, color};

    // Title + tagline.
    a.Bold("erpl-adt").Normal(" - CLI for the SAP ADT REST API").Nl().Nl();
    a.Dim("  Talks the same HTTP endpoints Eclipse ADT uses. No Eclipse, no RFC SDK, no JVM.").Nl();
    a.Dim("  All commands accept --json for machine-readable output.").Nl();

    // Usage.
    out << "\n";
    a.Bold("USAGE").Nl();
    out << "  erpl-adt [global-flags] <command> [args] [flags]\n";

    // Group ordering.
    const std::vector<std::string> group_order = {
        "search", "object", "source", "test", "check",
        "transport", "ddic", "package", "discover"};

    // Group display names and short descriptions (overrides for cleaner display).
    struct GroupMeta {
        std::string label;
        std::string short_desc;  // Empty means use router's GroupDescription.
    };
    const std::map<std::string, GroupMeta> group_meta = {
        {"search",    {"SEARCH", ""}},
        {"object",    {"OBJECT", ""}},
        {"source",    {"SOURCE", ""}},
        {"test",      {"TEST", ""}},
        {"check",     {"CHECK", ""}},
        {"transport", {"TRANSPORT", ""}},
        {"ddic",      {"DATA DICTIONARY", "Tables and CDS views"}},
        {"package",   {"PACKAGE", ""}},
        {"discover",  {"DISCOVER", ""}},
    };

    // Pre-compute max left-column width across ALL groups for alignment.
    size_t max_left = 0;
    std::map<std::string, std::vector<CmdDisplay>> all_displays;

    for (const auto& group : group_order) {
        auto cmds = router.CommandsForGroup(group);
        auto& displays = all_displays[group];

        for (const auto& cmd : cmds) {
            CmdDisplay d;
            // Build left column from usage string (strips "erpl-adt group" prefix).
            // Usage format: "erpl-adt <group> [<action>] [<positionals>] [flags]"
            std::string command_part;
            if (cmd.help.has_value() && !cmd.help->usage.empty()) {
                auto usage = cmd.help->usage;
                auto prefix = std::string("erpl-adt ") + group + " ";
                if (usage.substr(0, prefix.size()) == prefix) {
                    // Remainder after "erpl-adt <group> " — take until [flags] or --
                    auto rest = usage.substr(prefix.size());
                    auto bracket = rest.find('[');
                    auto dash = rest.find("--");
                    auto end = std::min(bracket, dash);
                    if (end != std::string::npos) {
                        command_part = rest.substr(0, end);
                    } else {
                        command_part = rest;
                    }
                    // Trim trailing whitespace.
                    while (!command_part.empty() && command_part.back() == ' ')
                        command_part.pop_back();
                    // If the first char is '<', this is a default action — prefix group name.
                    // e.g. "search <pattern>" instead of just "<pattern>"
                    if (!command_part.empty() && command_part[0] == '<') {
                        command_part = group + " " + command_part;
                    }
                } else {
                    command_part = cmd.action;
                }
            } else {
                command_part = cmd.action;
            }
            d.left = "  " + command_part;
            d.desc = cmd.description;
            if (cmd.help.has_value()) {
                d.flags = cmd.help->flags;
            }
            max_left = std::max(max_left, d.left.size());
            displays.push_back(std::move(d));
        }
    }

    // Pad to at least 42, cap at 48.
    max_left = std::max(max_left, static_cast<size_t>(42));
    if (max_left > 48) max_left = 48;

    // Print each group.
    for (const auto& group : group_order) {
        auto meta_it = group_meta.find(group);
        auto label = (meta_it != group_meta.end()) ? meta_it->second.label : group;
        auto desc = (meta_it != group_meta.end() && !meta_it->second.short_desc.empty())
            ? meta_it->second.short_desc
            : router.GroupDescription(group);

        out << "\n";
        a.Bold(label);
        if (!desc.empty()) {
            a.Dim(" \xe2\x80\x94 " + desc);  // em dash (UTF-8: E2 80 94)
        }
        a.Nl();

        const auto& displays = all_displays[group];
        for (const auto& d : displays) {
            // Print command line with padding.
            size_t pad = (max_left > d.left.size()) ? (max_left - d.left.size()) : 2;
            out << d.left << std::string(pad, ' ') << d.desc << "\n";

            // Print flags indented under the command.
            for (const auto& f : d.flags) {
                std::string flag_line = "      --" + f.name;
                if (!f.placeholder.empty()) {
                    flag_line += " " + f.placeholder;
                }
                // Pad flag description.
                size_t flag_pad = (max_left > flag_line.size())
                    ? (max_left - flag_line.size()) : 2;
                if (color) out << ansi::kDim;
                out << flag_line << std::string(flag_pad, ' ') << f.description;
                if (color) out << ansi::kReset;
                if (f.required) {
                    out << "  ";
                    a.Yellow("(required)");
                }
                out << "\n";
            }
        }
    }

    // MCP server section.
    out << "\n";
    a.Bold("MCP SERVER").Nl();
    out << "  mcp                                       Start MCP server (JSON-RPC over stdio)\n";

    // Credentials section.
    out << "\n";
    a.Bold("CREDENTIALS").Nl();
    out << "  login                                     Save connection credentials\n";
    out << "  logout                                    Remove saved credentials\n";

    // Global flags.
    out << "\n";
    a.Bold("GLOBAL FLAGS").Nl();

    // Flag entries: {flag_text, description}.
    struct GlobalFlag {
        const char* flag;
        const char* desc;
    };

    const GlobalFlag global_flags[] = {
        {"--host <host>",           "SAP hostname (default: localhost)"},
        {"--port <port>",           "SAP port (default: 50000)"},
        {"--user <user>",           "SAP username (default: DEVELOPER)"},
        {"--password <pass>",       "SAP password"},
        {"--password-env <var>",    "Read password from env var (default: SAP_PASSWORD)"},
        {"--client <num>",          "SAP client (default: 001)"},
        {"--https",                 "Use HTTPS"},
        {"--insecure",              "Skip TLS verification (with --https)"},
        {"--json",                  "JSON output"},
        {"--timeout <sec>",         "Request timeout in seconds"},
        {"--session-file <path>",   "Persist session for lock/write/unlock workflows"},
        {"--color",                 "Force colored output"},
        {"--no-color",              "Disable colored output"},
        {"-v",                      "Verbose logging (INFO level)"},
        {"-vv",                     "Debug logging (DEBUG level)"},
    };

    for (const auto& gf : global_flags) {
        std::string left = std::string("  ") + gf.flag;
        size_t pad = (max_left > left.size()) ? (max_left - left.size()) : 2;
        out << left << std::string(pad, ' ') << gf.desc << "\n";
    }

    out << "\n";
    a.Dim("  Credential priority: flags > --password-env > .adt.creds (via login) > SAP_PASSWORD env var").Nl();

    // Exit codes — compact 3-column layout.
    out << "\n";
    a.Bold("EXIT CODES").Nl();
    out << "  0  Success          1  Connection/auth     2  Not found\n";
    out << "  3  Clone error      4  Pull error          5  Activation error\n";
    out << "  6  Lock conflict    7  Test failure        8  ATC check error\n";
    out << "  9  Transport error  10 Timeout             99 Internal error\n";

    out << "\n";
    a.Dim("  Use \"erpl-adt <command> --help\" for examples and workflows.").Nl();
}

// ---------------------------------------------------------------------------
// PrintLoginHelp / PrintLogoutHelp
// ---------------------------------------------------------------------------
void PrintLoginHelp(std::ostream& out, bool color) {
    Ansi a{out, color};

    a.Bold("erpl-adt login").Normal(" ").Dim("- Save SAP connection credentials to .adt.creds").Nl();
    out << "\n";
    a.Bold("USAGE").Nl();
    out << "  erpl-adt login                                                  # Interactive mode\n";
    out << "  erpl-adt login --host <host> --user <user> --password <pass>    # Flag mode\n";
    out << "\nWhen called with no flags (and stdin is a terminal), an interactive form is shown.\n";
    out << "Existing saved credentials are pre-populated as defaults.\n";
    out << "\n";
    a.Bold("FLAGS").Nl();
    out << "  --host <host>         SAP hostname (required in flag mode)\n";
    out << "  --user <user>         SAP username (required in flag mode)\n";
    out << "  --password <pass>     SAP password (required in flag mode, or use --password-env)\n";
    out << "  --password-env <var>  Read password from env var (default: SAP_PASSWORD)\n";
    out << "  --port <port>         SAP port (default: 50000)\n";
    out << "  --client <num>        SAP client (default: 001)\n";
    out << "  --https               Use HTTPS\n";
    out << "\n";
    a.Bold("EXAMPLES").Nl();
    a.Dim("  # Interactive wizard (recommended for first-time setup)").Nl();
    out << "  erpl-adt login\n";
    out << "\n";
    a.Dim("  # Flag mode (for scripts/CI)").Nl();
    out << "  erpl-adt login --host sap.example.com --user DEV --password secret\n";
    out << "  erpl-adt login --host sap.example.com --user DEV --password-env MY_PASS\n";
}

void PrintLogoutHelp(std::ostream& out, bool color) {
    Ansi a{out, color};

    a.Bold("erpl-adt logout").Normal(" ").Dim("- Remove saved credentials").Nl();
    out << "\n";
    a.Bold("USAGE").Nl();
    out << "  erpl-adt logout\n";
    out << "\nRemoves the .adt.creds file containing saved connection credentials.\n";
}

// ---------------------------------------------------------------------------
// IsNewStyleCommand
// ---------------------------------------------------------------------------
// Boolean flags that don't consume a following value argument.
bool IsBooleanFlag(std::string_view arg) {
    return arg == "--color" || arg == "--no-color" ||
           arg == "--json" || arg == "--https" || arg == "--insecure";
}

bool IsNewStyleCommand(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        // Skip short verbosity flags.
        if (arg == "-v" || arg == "-vv") {
            continue;
        }
        if (arg.substr(0, 2) == "--") {
            // Skip flags (and their values).
            auto eq = arg.find('=');
            if (eq == std::string_view::npos && !IsBooleanFlag(arg) &&
                i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i; // skip the value
            }
            continue;
        }
        // First non-flag arg — is it a new-style group?
        return kNewStyleGroups.count(std::string(arg)) > 0;
    }
    return false;
}

// ---------------------------------------------------------------------------
// RegisterAllCommands
// ---------------------------------------------------------------------------
void RegisterAllCommands(CommandRouter& router) {
    // -----------------------------------------------------------------------
    // Group descriptions and examples
    // -----------------------------------------------------------------------
    router.SetGroupDescription("search", "Search for ABAP objects");
    router.SetGroupExamples("search", {
        "$ erpl-adt search \"ZCL_*\" --type=CLAS --max=50",
        "$ erpl-adt --json search \"FLIGHT*\" --max=10",
    });

    router.SetGroupDescription("object", "Read, create, delete, lock/unlock ABAP objects");
    router.SetGroupExamples("object", {
        "$ erpl-adt object read /sap/bc/adt/oo/classes/ZCL_EXAMPLE",
        "$ erpl-adt object create --type=CLAS/OC --name=ZCL_NEW --package=ZTEST",
        "# Lock, write, unlock workflow:",
        "$ erpl-adt --json object lock /sap/bc/adt/oo/classes/ZCL_TEST --session-file=s.json",
        "$ erpl-adt source write .../source/main --file=src.abap --handle=H --session-file=s.json",
        "$ erpl-adt object unlock /sap/bc/adt/oo/classes/ZCL_TEST --handle=H --session-file=s.json",
    });

    router.SetGroupDescription("source", "Read, write, and check ABAP source code");
    router.SetGroupExamples("source", {
        "# Read active source",
        "$ erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main",
        "",
        "# Write source (auto-lock mode)",
        "$ erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main --file=source.abap",
        "",
        "# Syntax check",
        "$ erpl-adt source check /sap/bc/adt/oo/classes/zcl_test/source/main",
    });

    router.SetGroupDescription("test", "Run ABAP Unit tests");
    router.SetGroupExamples("test", {
        "$ erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST",
        "$ erpl-adt --json test run /sap/bc/adt/oo/classes/ZCL_TEST",
    });

    router.SetGroupDescription("check", "Run ATC quality checks");
    router.SetGroupExamples("check", {
        "$ erpl-adt check run /sap/bc/adt/packages/ZTEST",
        "$ erpl-adt check run /sap/bc/adt/oo/classes/ZCL_TEST --variant=FUNCTIONAL_DB_ADDITION",
    });

    router.SetGroupDescription("transport", "List, create, and release transports");
    router.SetGroupExamples("transport", {
        "$ erpl-adt transport list",
        "$ erpl-adt transport create --desc=\"Feature X\" --package=ZTEST",
        "$ erpl-adt transport release NPLK900001",
    });

    router.SetGroupDescription("ddic", "Data Dictionary — tables and CDS views");
    router.SetGroupExamples("ddic", {
        "$ erpl-adt ddic table SFLIGHT",
        "$ erpl-adt --json ddic cds I_BUSINESSPARTNER",
    });

    router.SetGroupDescription("package", "List contents and check package existence");
    router.SetGroupExamples("package", {
        "$ erpl-adt package list ZTEST",
        "$ erpl-adt --json package exists ZTEST",
    });

    router.SetGroupDescription("discover", "Discover available ADT services");
    router.SetGroupExamples("discover", {
        "$ erpl-adt discover services",
        "$ erpl-adt --json discover services",
    });

    // -----------------------------------------------------------------------
    // search query (default action — "erpl-adt search <pattern>" works)
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt search <pattern> [flags]";
        help.args_description = "<pattern>    Search pattern with wildcards (e.g., ZCL_*)";
        help.long_description =
            "The SAP server caps results at its configured limit. "
            "For exhaustive object enumeration, use 'erpl-adt package tree <pkg>' "
            "to recursively traverse package contents.";
        help.flags = {
            {"type", "<type>", "Object type: CLAS, PROG, TABL, INTF, FUGR", false},
            {"max", "<n>", "Maximum number of results", false},
        };
        help.examples = {
            "erpl-adt search \"ZCL_*\" --type=CLAS",
            "erpl-adt search \"FLIGHT*\" --max=10",
            "erpl-adt --json search \"ZCL_*\"",
        };
        router.Register("search", "query", "Search for ABAP objects",
                         HandleSearchQuery, std::move(help));
        router.SetDefaultAction("search", "query");
    }

    // -----------------------------------------------------------------------
    // object read
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt object read <uri>";
        help.args_description = "<uri>    ADT object URI (e.g., /sap/bc/adt/oo/classes/ZCL_EXAMPLE)";
        help.examples = {
            "erpl-adt object read /sap/bc/adt/oo/classes/ZCL_EXAMPLE",
            "erpl-adt --json object read /sap/bc/adt/programs/programs/ZREPORT",
        };
        router.Register("object", "read", "Read object structure",
                         HandleObjectRead, std::move(help));
    }

    // -----------------------------------------------------------------------
    // object create
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt object create --type <type> --name <name> --package <pkg> [flags]";
        help.flags = {
            {"type", "<type>", "Object type (e.g., CLAS/OC, PROG/P)", true},
            {"name", "<name>", "Object name", true},
            {"package", "<pkg>", "Target package", true},
            {"description", "<text>", "Object description", false},
            {"transport", "<id>", "Transport request number", false},
        };
        help.examples = {
            "erpl-adt object create --type=CLAS/OC --name=ZCL_NEW --package=ZTEST",
            "erpl-adt object create --type=PROG/P --name=ZREPORT --package=ZTEST --description=\"My report\"",
        };
        router.Register("object", "create", "Create an ABAP object",
                         HandleObjectCreate, std::move(help));
    }

    // -----------------------------------------------------------------------
    // object delete
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt object delete <uri> [flags]";
        help.args_description = "<uri>    Object URI to delete";
        help.long_description = "Without --handle, auto-locks, deletes, and unlocks in one session.";
        help.flags = {
            {"handle", "<handle>", "Lock handle (skips auto-lock if provided)", false},
            {"transport", "<id>", "Transport request number", false},
        };
        help.examples = {
            "erpl-adt object delete /sap/bc/adt/oo/classes/ZCL_OLD",
            "erpl-adt object delete /sap/bc/adt/oo/classes/ZCL_OLD --transport=NPLK900001",
        };
        router.Register("object", "delete", "Delete an ABAP object",
                         HandleObjectDelete, std::move(help));
    }

    // -----------------------------------------------------------------------
    // object lock
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt object lock <uri> [flags]";
        help.args_description = "<uri>    Object URI";
        help.flags = {
            {"session-file", "<path>", "Save session for later unlock", false},
        };
        help.examples = {
            "erpl-adt object lock /sap/bc/adt/oo/classes/ZCL_TEST",
            "erpl-adt --json object lock /sap/bc/adt/oo/classes/ZCL_TEST --session-file=session.json",
        };
        router.Register("object", "lock", "Lock an object for editing",
                         HandleObjectLock, std::move(help));
    }

    // -----------------------------------------------------------------------
    // object unlock
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt object unlock <uri> --handle <handle> [flags]";
        help.args_description = "<uri>    Object URI";
        help.flags = {
            {"handle", "<handle>", "Lock handle", true},
            {"session-file", "<path>", "Session file for stateful workflow", false},
        };
        help.examples = {
            "erpl-adt object unlock /sap/bc/adt/oo/classes/ZCL_TEST --handle=LOCK_HANDLE",
        };
        router.Register("object", "unlock", "Unlock an object",
                         HandleObjectUnlock, std::move(help));
    }

    // -----------------------------------------------------------------------
    // source read
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt source read <uri> [flags]";
        help.args_description = "<uri>    Source URI";
        help.flags = {
            {"version", "<version>", "active or inactive (default: active)", false},
        };
        help.examples = {
            "erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main",
            "erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main --version=inactive",
        };
        router.Register("source", "read", "Read source code",
                         HandleSourceRead, std::move(help));
    }

    // -----------------------------------------------------------------------
    // source write
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt source write <uri> --file <path> [flags]";
        help.args_description = "<uri>    Source URI (e.g., /sap/bc/adt/oo/classes/zcl_test/source/main)";
        help.long_description = "Without --handle, the object is automatically locked, written, and unlocked.";
        help.flags = {
            {"file", "<path>", "Path to local source file", true},
            {"handle", "<handle>", "Lock handle (skips auto-lock if provided)", false},
            {"transport", "<id>", "Transport request number", false},
            {"session-file", "<path>", "Session file for stateful workflow", false},
        };
        help.examples = {
            "erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main --file=source.abap",
            "erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main --file=source.abap --handle=LOCK_HANDLE --transport=NPLK900001",
        };
        router.Register("source", "write", "Write source code",
                         HandleSourceWrite, std::move(help));
    }

    // -----------------------------------------------------------------------
    // source check
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt source check <uri>";
        help.args_description = "<uri>    Source URI";
        help.examples = {
            "erpl-adt source check /sap/bc/adt/oo/classes/zcl_test/source/main",
            "erpl-adt --json source check /sap/bc/adt/oo/classes/zcl_test/source/main",
        };
        router.Register("source", "check", "Check syntax",
                         HandleSourceCheck, std::move(help));
    }

    // -----------------------------------------------------------------------
    // test run
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt test run <uri>";
        help.args_description = "<uri>    Object or package URI";
        help.long_description = "Exit code 7 indicates test failures.";
        help.examples = {
            "erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST",
            "erpl-adt --json test run /sap/bc/adt/oo/classes/ZCL_TEST",
        };
        router.Register("test", "run", "Run ABAP unit tests",
                         HandleTestRun, std::move(help));
    }

    // -----------------------------------------------------------------------
    // check run
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt check run <uri> [flags]";
        help.args_description = "<uri>    Object or package URI";
        help.long_description = "Exit code 8 indicates ATC errors.";
        help.flags = {
            {"variant", "<name>", "ATC variant (default: DEFAULT)", false},
        };
        help.examples = {
            "erpl-adt check run /sap/bc/adt/packages/ZTEST",
            "erpl-adt check run /sap/bc/adt/oo/classes/ZCL_TEST --variant=FUNCTIONAL_DB_ADDITION",
        };
        router.Register("check", "run", "Run ATC checks",
                         HandleCheckRun, std::move(help));
    }

    // -----------------------------------------------------------------------
    // transport list
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt transport list [flags]";
        help.flags = {
            {"user", "<user>", "Filter by user (default: DEVELOPER)", false},
        };
        help.examples = {
            "erpl-adt transport list",
            "erpl-adt --json transport list --user=ADMIN",
        };
        router.Register("transport", "list", "List transports",
                         HandleTransportList, std::move(help));
    }

    // -----------------------------------------------------------------------
    // transport create
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt transport create --desc <text> --package <pkg>";
        help.flags = {
            {"desc", "<text>", "Transport description", true},
            {"package", "<pkg>", "Target package", true},
        };
        help.examples = {
            "erpl-adt transport create --desc=\"Feature X\" --package=ZTEST",
        };
        router.Register("transport", "create", "Create a transport",
                         HandleTransportCreate, std::move(help));
    }

    // -----------------------------------------------------------------------
    // transport release
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt transport release <number>";
        help.args_description = "<number>    Transport number";
        help.long_description = "Exit code 9 indicates release failure.";
        help.examples = {
            "erpl-adt transport release NPLK900001",
        };
        router.Register("transport", "release", "Release a transport",
                         HandleTransportRelease, std::move(help));
    }

    // -----------------------------------------------------------------------
    // ddic table
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt ddic table <name>";
        help.args_description = "<name>    Table name";
        help.examples = {
            "erpl-adt ddic table SFLIGHT",
            "erpl-adt --json ddic table MARA",
        };
        router.Register("ddic", "table", "Get table definition",
                         HandleDdicTable, std::move(help));
    }

    // -----------------------------------------------------------------------
    // ddic cds
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt ddic cds <name>";
        help.args_description = "<name>    CDS view name";
        help.examples = {
            "erpl-adt ddic cds ZCDS_VIEW",
            "erpl-adt --json ddic cds I_BUSINESSPARTNER",
        };
        router.Register("ddic", "cds", "Get CDS source",
                         HandleDdicCds, std::move(help));
    }

    // -----------------------------------------------------------------------
    // package list
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt package list <name>";
        help.args_description = "<name>    Package name";
        help.examples = {
            "erpl-adt package list ZTEST",
            "erpl-adt --json package list $TMP",
        };
        router.Register("package", "list", "List package contents",
                         HandlePackageList, std::move(help));
    }

    // -----------------------------------------------------------------------
    // package tree
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt package tree <name> [flags]";
        help.args_description = "<name>    Root package name";
        help.long_description = "Recursively lists all objects in a package and its sub-packages. "
            "Useful for exhaustive enumeration when search maxResults is not sufficient.";
        help.flags = {
            {"type", "<type>", "Filter by object type: CLAS, PROG, TABL, INTF, FUGR", false},
            {"max-depth", "<n>", "Maximum recursion depth (default: 50)", false},
        };
        help.examples = {
            "erpl-adt package tree ZTEST",
            "erpl-adt package tree $TMP --type=TABL",
            "erpl-adt --json package tree ZTEST --type=CLAS",
        };
        router.Register("package", "tree", "List package contents recursively",
                         HandlePackageTree, std::move(help));
    }

    // -----------------------------------------------------------------------
    // package exists
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt package exists <name>";
        help.args_description = "<name>    Package name";
        help.examples = {
            "erpl-adt package exists ZTEST",
            "erpl-adt --json package exists ZTEST",
        };
        router.Register("package", "exists", "Check if package exists",
                         HandlePackageExists, std::move(help));
    }

    // -----------------------------------------------------------------------
    // discover services
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt discover services";
        help.long_description = "Lists all ADT REST API services and capabilities (abapGit, packages, activation).";
        help.examples = {
            "erpl-adt discover services",
            "erpl-adt --json discover services",
        };
        router.Register("discover", "services", "Discover ADT services",
                         HandleDiscoverServices, std::move(help));
    }
}

// ---------------------------------------------------------------------------
// HandleLogin / HandleLogout
// ---------------------------------------------------------------------------

int HandleLogin(int argc, const char* const* argv) {
    // Parse flags manually — "login" is a single-word command, not group+action.
    std::map<std::string, std::string> flags;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "login" || arg == "-v" || arg == "-vv") {
            continue;
        }
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq != std::string_view::npos) {
                flags[std::string(arg.substr(2, eq - 2))] =
                    std::string(arg.substr(eq + 1));
            } else {
                auto key = std::string(arg.substr(2));
                if (i + 1 < argc &&
                    std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                    flags[key] = argv[i + 1];
                    ++i;
                } else {
                    flags[key] = "true";
                }
            }
        }
    }

    auto get = [&](const std::string& key,
                    const std::string& def = "") -> std::string {
        auto it = flags.find(key);
        return (it != flags.end()) ? it->second : def;
    };

    // Check if any login-specific flags were provided.
    bool has_login_flags = !get("host").empty() || !get("user").empty() ||
                           !get("password").empty() || !get("password-env").empty();

    if (!has_login_flags && IsStdinTty()) {
        // Wizard mode: load existing creds as defaults.
        auto existing = LoadCredentials();
        std::optional<LoginCredentials> defaults;
        if (existing) {
            LoginCredentials lc;
            lc.host = existing->host;
            lc.port = existing->port;
            lc.user = existing->user;
            lc.password = "";  // Never pre-fill password.
            lc.client = existing->client;
            lc.use_https = existing->use_https;
            defaults = std::move(lc);
        }
        auto result = RunLoginWizard(defaults);
        if (!result) {
            std::cout << "Login cancelled.\n";
            return 0;
        }
        SavedCredentials creds;
        creds.host = result->host;
        creds.port = result->port;
        creds.user = result->user;
        creds.password = result->password;
        creds.client = result->client;
        creds.use_https = result->use_https;
        if (!SaveCredentials(creds)) {
            std::cerr << "Error: failed to write " << kCredsFile << "\n";
            return 99;
        }
        std::cout << "Credentials saved to " << kCredsFile << "\n";
        return 0;
    }

    // Flag-based mode (existing behavior).
    auto host = get("host");
    auto user = get("user");
    auto password = get("password");
    auto client = get("client", "001");
    auto port_str = get("port", "50000");
    auto use_https = get("https") == "true";

    if (host.empty()) {
        std::cerr << "Error: --host is required for login\n";
        return 99;
    }
    if (user.empty()) {
        std::cerr << "Error: --user is required for login\n";
        return 99;
    }
    if (password.empty()) {
        auto env_var = get("password-env", "SAP_PASSWORD");
        const char* env_val = std::getenv(env_var.c_str());
        if (env_val != nullptr) {
            password = env_val;
        }
    }
    if (password.empty()) {
        std::cerr << "Error: --password is required for login\n";
        return 99;
    }

    SavedCredentials creds;
    creds.host = host;
    creds.port = static_cast<uint16_t>(std::stoi(port_str));
    creds.user = user;
    creds.password = password;
    creds.client = client;
    creds.use_https = use_https;

    if (!SaveCredentials(creds)) {
        std::cerr << "Error: failed to write " << kCredsFile << "\n";
        return 99;
    }

    std::cout << "Credentials saved to " << kCredsFile << "\n";
    return 0;
}

int HandleLogout() {
    if (DeleteCredentials()) {
        std::cout << "Credentials removed (" << kCredsFile << " deleted)\n";
    } else {
        std::cout << "No credentials file found\n";
    }
    return 0;
}

} // namespace erpl_adt
