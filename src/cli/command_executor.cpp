#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/cli/login_wizard.hpp>
#include <erpl_adt/cli/output_formatter.hpp>
#include <erpl_adt/core/ansi.hpp>
#include <erpl_adt/core/terminal.hpp>

#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/adt_session.hpp>
#include <erpl_adt/adt/bw_activation.hpp>
#include <erpl_adt/adt/bw_discovery.hpp>
#include <erpl_adt/adt/bw_endpoint_resolver.hpp>
#include <erpl_adt/adt/bw_jobs.hpp>
#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/bw_lineage_planner.hpp>
#include <erpl_adt/adt/bw_object.hpp>
#include <erpl_adt/adt/bw_locks.hpp>
#include <erpl_adt/adt/bw_repo_utils.hpp>
#include <erpl_adt/adt/bw_reporting.hpp>
#include <erpl_adt/adt/bw_dataflow.hpp>
#include <erpl_adt/adt/bw_query.hpp>
#include <erpl_adt/adt/bw_rsds.hpp>
#include <erpl_adt/adt/bw_search.hpp>
#include <erpl_adt/adt/bw_system.hpp>
#include <erpl_adt/adt/bw_nodes.hpp>
#include <erpl_adt/adt/bw_transport.hpp>
#include <erpl_adt/adt/bw_transport_collect.hpp>
#include <erpl_adt/adt/bw_export.hpp>
#include <erpl_adt/adt/bw_validation.hpp>
#include <erpl_adt/adt/bw_valuehelp.hpp>
#include <erpl_adt/adt/bw_xref.hpp>
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
#include <erpl_adt/workflow/lock_workflow.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
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
    "activate", "bw", "search", "object", "source", "test", "check",
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

Result<int, Error> ParseIntInRange(std::string_view raw,
                                   int min_value,
                                   int max_value,
                                   const std::string& field_name) {
    if (raw.empty()) {
        return Result<int, Error>::Err(
            MakeValidationError("Missing " + field_name));
    }

    std::string s(raw);
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE ||
        value < min_value || value > max_value) {
        return Result<int, Error>::Err(
            MakeValidationError("Invalid " + field_name + ": " + s));
    }

    return Result<int, Error>::Ok(static_cast<int>(value));
}

Result<uint16_t, Error> ParsePort(std::string_view raw) {
    auto result = ParseIntInRange(raw, 1, 65535, "--port");
    if (result.IsErr()) {
        return Result<uint16_t, Error>::Err(std::move(result).Error());
    }
    return Result<uint16_t, Error>::Ok(
        static_cast<uint16_t>(std::move(result).Value()));
}

// Create an AdtSession from CommandArgs flags.
Result<std::unique_ptr<AdtSession>, Error> CreateSession(const CommandArgs& args) {
    auto creds = LoadCredentials();

    auto host = GetFlag(args, "host", creds ? creds->host : "localhost");
    auto port_str = GetFlag(args, "port",
                            creds ? std::to_string(creds->port) : "50000");
    auto port_result = ParsePort(port_str);
    if (port_result.IsErr()) {
        return Result<std::unique_ptr<AdtSession>, Error>::Err(
            std::move(port_result).Error());
    }
    auto port = port_result.Value();
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

    auto client_result = SapClient::Create(client_str);
    if (client_result.IsErr()) {
        return Result<std::unique_ptr<AdtSession>, Error>::Err(
            MakeValidationError("Invalid --client: " + client_result.Error()));
    }
    auto sap_client = std::move(client_result).Value();

    AdtSessionOptions opts;
    if (HasFlag(args, "timeout")) {
        auto timeout_result = ParseIntInRange(
            GetFlag(args, "timeout"),
            1,
            std::numeric_limits<int>::max(),
            "--timeout");
        if (timeout_result.IsErr()) {
            return Result<std::unique_ptr<AdtSession>, Error>::Err(
                std::move(timeout_result).Error());
        }
        opts.read_timeout = std::chrono::seconds(timeout_result.Value());
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

    return Result<std::unique_ptr<AdtSession>, Error>::Ok(std::move(session));
}

std::unique_ptr<AdtSession> RequireSession(const CommandArgs& args,
                                           OutputFormatter& fmt) {
    auto session_result = CreateSession(args);
    if (session_result.IsErr()) {
        fmt.PrintError(session_result.Error());
        return nullptr;
    }
    return std::move(session_result).Value();
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
// ResolveObjectUri — resolve object name to URI via search
// ---------------------------------------------------------------------------
Result<std::string, Error> ResolveObjectUri(IAdtSession& session,
                                            const std::string& name_or_uri) {
    // Already a URI — pass through.
    if (name_or_uri.find("/sap/bc/adt/") == 0) {
        return Result<std::string, Error>::Ok(std::string(name_or_uri));
    }

    // Search for the object by name.
    SearchOptions opts;
    opts.query = name_or_uri;
    opts.max_results = 10;
    auto result = SearchObjects(session, opts);
    if (result.IsErr()) {
        return Result<std::string, Error>::Err(std::move(result).Error());
    }

    const auto& items = result.Value();

    // Look for an exact name match (case-insensitive).
    std::string upper_name = name_or_uri;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto& item : items) {
        std::string upper_item = item.name;
        std::transform(upper_item.begin(), upper_item.end(), upper_item.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (upper_item == upper_name) {
            return Result<std::string, Error>::Ok(std::string(item.uri));
        }
    }

    // No exact match.
    if (items.empty()) {
        Error err;
        err.operation = "ResolveObjectUri";
        err.message = "Object not found: " + name_or_uri;
        err.category = ErrorCategory::NotFound;
        return Result<std::string, Error>::Err(std::move(err));
    }

    // Build suggestion list.
    std::string suggestions;
    for (size_t i = 0; i < items.size() && i < 5; ++i) {
        if (i > 0) suggestions += ", ";
        suggestions += items[i].name;
    }
    Error err;
    err.operation = "ResolveObjectUri";
    err.message = "No exact match for '" + name_or_uri + "'. Did you mean: " + suggestions;
    err.category = ErrorCategory::NotFound;
    return Result<std::string, Error>::Err(std::move(err));
}

// ---------------------------------------------------------------------------
// ResolveObjectInfo — resolve object name to full SearchResult
// ---------------------------------------------------------------------------
Result<SearchResult, Error> ResolveObjectInfo(IAdtSession& session,
                                              const std::string& name_or_uri) {
    // Already a URI — return with just uri set.
    if (name_or_uri.find("/sap/bc/adt/") == 0) {
        SearchResult sr;
        sr.uri = name_or_uri;
        return Result<SearchResult, Error>::Ok(std::move(sr));
    }

    // Search for the object by name.
    SearchOptions opts;
    opts.query = name_or_uri;
    opts.max_results = 10;
    auto result = SearchObjects(session, opts);
    if (result.IsErr()) {
        return Result<SearchResult, Error>::Err(std::move(result).Error());
    }

    const auto& items = result.Value();

    // Look for an exact name match (case-insensitive).
    std::string upper_name = name_or_uri;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto& item : items) {
        std::string upper_item = item.name;
        std::transform(upper_item.begin(), upper_item.end(), upper_item.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (upper_item == upper_name) {
            return Result<SearchResult, Error>::Ok(SearchResult(item));
        }
    }

    // No exact match.
    if (items.empty()) {
        Error err;
        err.operation = "ResolveObjectInfo";
        err.message = "Object not found: " + name_or_uri;
        err.category = ErrorCategory::NotFound;
        return Result<SearchResult, Error>::Err(std::move(err));
    }

    // Build suggestion list.
    std::string suggestions;
    for (size_t i = 0; i < items.size() && i < 5; ++i) {
        if (i > 0) suggestions += ", ";
        suggestions += items[i].name;
    }
    Error err;
    err.operation = "ResolveObjectInfo";
    err.message = "No exact match for '" + name_or_uri + "'. Did you mean: " + suggestions;
    err.category = ErrorCategory::NotFound;
    return Result<SearchResult, Error>::Err(std::move(err));
}

std::optional<std::string> TryResolveBwEndpoint(
    IAdtSession& session,
    const std::string& scheme,
    const std::string& term,
    const BwTemplateParams& path_params,
    const BwTemplateParams& query_params) {
    auto resolved = BwDiscoverResolveAndExpandEndpoint(
        session, scheme, term, path_params, query_params);
    if (resolved.IsErr() || resolved.Value().empty()) {
        return std::nullopt;
    }
    return resolved.Value();
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// ---------------------------------------------------------------------------
// Syntax highlighting helpers
// ---------------------------------------------------------------------------

enum class SourceLanguage { Abap, Xml, Mermaid, Plain };

static std::string HighlightAbap(const std::string& src) {
    // Known ABAP keywords (uppercase for comparison).
    static const std::set<std::string> kKeywords = {
        "ABAP", "ABSTRACT", "ADD", "ALIAS", "AND", "APPEND", "AS", "ASSIGN",
        "AT", "AUTHORITY-CHECK", "BEGIN", "BREAK-POINT", "BY", "CALL",
        "CASE", "CATCH", "CHECK", "CLASS", "CLASS-DATA", "CLASS-EVENTS",
        "CLASS-METHODS", "CLEAR", "CLOSE", "COMMIT", "CONDENSE", "CONSTANTS",
        "CONTINUE", "CREATE", "DATA", "DEFAULT", "DELETE", "DESCRIBE",
        "DO", "ELSEIF", "ELSE", "ENDCASE", "ENDCLASS", "ENDDO",
        "ENDFORM", "ENDIF", "ENDINTERFACE", "ENDLOOP", "ENDMETHOD",
        "ENDMODULE", "ENDSELECT", "ENDTRY", "ENDWHILE", "ENUM",
        "EVENTS", "EXCEPTION", "EXCEPTIONS", "EXIT", "EXPORTING",
        "FIELD-SYMBOLS", "FINAL", "FIND", "FORM", "FORMAT", "FREE",
        "FROM", "FUNCTION", "GET", "GROUP", "IF", "IMPLEMENTATION",
        "IMPORTING", "IN", "INCLUDE", "INNER", "INSERT", "INTERFACE",
        "INTERFACES", "INTO", "IS", "JOIN", "LIKE", "LOCAL", "LOOP",
        "MESSAGE", "METHOD", "METHODS", "MODIFY", "MODULE", "MOVE",
        "NEW", "NOT", "OBJECT", "OF", "OFFSET", "ON", "OPTIONAL", "OR",
        "ORDER", "OTHERS", "OUTER", "PERFORM", "PRIVATE", "PROTECTED",
        "PUBLIC", "RAISE", "RAISING", "READ", "RECEIVING", "REF",
        "REFRESH", "RETURNING", "ROLLBACK", "SECTION", "SELECT",
        "SORT", "SPLIT", "STATIC", "SUPPLY", "TABLE", "TABLES", "TO",
        "TRY", "TYPE", "TYPES", "UP", "VALUE", "WHERE", "WHILE",
        "WITH", "WRITE",
    };

    using namespace erpl_adt::ansi;
    std::ostringstream out;
    std::istringstream in(src);
    std::string line;

    while (std::getline(in, line)) {
        // Full-line comment: first non-space character is '*'
        {
            size_t pos = 0;
            while (pos < line.size() && line[pos] == ' ') ++pos;
            if (pos < line.size() && line[pos] == '*') {
                out << kDim << line << kReset << '\n';
                continue;
            }
        }

        // Scan character by character.
        enum class State { Normal, InString, InComment };
        State state = State::Normal;
        std::string word;
        size_t i = 0;

        auto flush_word = [&]() {
            if (word.empty()) return;
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (kKeywords.count(upper)) {
                out << kCyan << word << kReset;
            } else {
                out << word;
            }
            word.clear();
        };

        while (i < line.size()) {
            char c = line[i];
            if (state == State::InString) {
                out << c;
                if (c == '\'') {
                    // Check for escaped quote ('').
                    if (i + 1 < line.size() && line[i + 1] == '\'') {
                        out << '\'';
                        ++i;
                    } else {
                        out << kReset;
                        state = State::Normal;
                    }
                }
            } else if (state == State::InComment) {
                out << c;
            } else {
                // Normal state.
                if (c == '\'') {
                    flush_word();
                    out << kGreen << c;
                    state = State::InString;
                } else if (c == '"') {
                    flush_word();
                    out << kDim << c;
                    state = State::InComment;
                } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                    word += c;
                } else {
                    flush_word();
                    out << c;
                }
            }
            ++i;
        }

        if (state == State::InString) {
            flush_word();
            out << kReset;
        } else if (state == State::InComment) {
            out << kReset;
        } else {
            flush_word();
        }
        out << '\n';
    }
    return out.str();
}

static std::string HighlightXml(const std::string& src) {
    using namespace erpl_adt::ansi;
    std::ostringstream out;

    enum class State {
        Text, InComment, InTagName, InAttrName, AfterAttrName,
        InAttrValueDq, InAttrValueSq
    };
    State state = State::Text;
    size_t i = 0;

    while (i < src.size()) {
        char c = src[i];
        switch (state) {
        case State::Text:
            if (c == '<') {
                if (src.compare(i, 4, "<!--") == 0) {
                    out << kDim << "<!--";
                    i += 4;
                    state = State::InComment;
                    continue;
                }
                out << kCyan << c;
                state = State::InTagName;
            } else {
                out << c;
            }
            break;
        case State::InComment:
            out << c;
            if (c == '-' && src.compare(i, 3, "-->") == 0) {
                out << "->";
                i += 3;
                out << kReset;
                state = State::Text;
                continue;
            }
            break;
        case State::InTagName:
            if (c == '>') {
                out << c << kReset;
                state = State::Text;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                out << kReset << c;
                state = State::InAttrName;
            } else {
                out << c;
            }
            break;
        case State::InAttrName:
            if (c == '>') {
                out << kCyan << c << kReset;
                state = State::Text;
            } else if (c == '=') {
                out << kYellow << c << kReset;
                state = State::AfterAttrName;
            } else {
                out << kYellow << c;
            }
            break;
        case State::AfterAttrName:
            if (c == '"') {
                out << kGreen << c;
                state = State::InAttrValueDq;
            } else if (c == '\'') {
                out << kGreen << c;
                state = State::InAttrValueSq;
            } else if (c == '>') {
                out << kCyan << c << kReset;
                state = State::Text;
            } else {
                out << c;
            }
            break;
        case State::InAttrValueDq:
            out << c;
            if (c == '"') {
                out << kReset;
                state = State::InAttrName;
            }
            break;
        case State::InAttrValueSq:
            out << c;
            if (c == '\'') {
                out << kReset;
                state = State::InAttrName;
            }
            break;
        }
        ++i;
    }
    // Close any open escape sequence.
    if (state != State::Text) out << kReset;
    return out.str();
}

std::string HighlightSource(const std::string& src, SourceLanguage lang,
                             bool color_mode) {
    if (!color_mode) return src;
    switch (lang) {
    case SourceLanguage::Abap:    return HighlightAbap(src);
    case SourceLanguage::Xml:     return HighlightXml(src);
    case SourceLanguage::Mermaid: return src;  // No tokenizer for Mermaid — pass through.
    case SourceLanguage::Plain:   return src;
    }
    return src;
}

// ---------------------------------------------------------------------------
// --editor helpers
// ---------------------------------------------------------------------------

std::string MakeTempPath(const std::string& ext) {
    auto uid = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (std::filesystem::temp_directory_path()
            / ("erpl-adt-" + uid + ext)).string();
}

int LaunchEditor(const std::string& path) {
    const char* ed = std::getenv("VISUAL");
    if (!ed) ed = std::getenv("EDITOR");
#ifdef _WIN32
    if (!ed) ed = "notepad";
    auto cmd = "\"" + std::string(ed) + "\" \"" + path + "\"";
#else
    if (!ed) ed = "vi";
    auto cmd = std::string(ed) + " '" + path + "'";
#endif
    return std::system(cmd.c_str());
}

BwTemplateParams BuildBwObjectPathParams(const BwReadOptions& opts) {
    BwTemplateParams params;
    params["version"] = opts.version;
    params["objectName"] = opts.object_name;
    params["objectType"] = opts.object_type;
    params["objname"] = opts.object_name;
    params["objvers"] = opts.version;
    params["objectname"] = opts.object_name;

    const auto lower = ToLowerCopy(opts.object_type);
    params["adsonm"] = opts.object_name;
    params["hcprnm"] = opts.object_name;
    params["infoobject"] = opts.object_name;
    params["trfnnm"] = opts.object_name;
    params["dtpanm"] = opts.object_name;
    params["sourcesystem"] = opts.object_name;
    params["compid"] = opts.object_name;
    params["fbpnm"] = opts.object_name;
    params["dmodnm"] = opts.object_name;
    params["segrnm"] = opts.object_name;
    params["destnm"] = opts.object_name;
    params["trcsnm"] = opts.object_name;
    params["rspcnm"] = opts.object_name;
    params["docanm"] = opts.object_name;
    params["dhdsnm"] = opts.object_name;
    params["infoprov"] = opts.object_name;
    params["datasource"] = opts.object_name;

    if (opts.source_system.has_value()) {
        params["logsys"] = *opts.source_system;
        params["logicalsystem"] = *opts.source_system;
    }

    if (lower == "rsds" && opts.source_system.has_value()) {
        params["datasource"] = opts.object_name;
    }
    return params;
}

BwContextHeaders ParseBwContextHeaders(const CommandArgs& args) {
    BwContextHeaders context;
    if (HasFlag(args, "transport-lock-holder")) {
        context.transport_lock_holder = GetFlag(args, "transport-lock-holder");
    }
    if (HasFlag(args, "foreign-objects")) {
        context.foreign_objects = GetFlag(args, "foreign-objects");
    }
    if (HasFlag(args, "foreign-object-locks")) {
        context.foreign_object_locks = GetFlag(args, "foreign-object-locks");
    }
    if (HasFlag(args, "foreign-correction-number")) {
        context.foreign_correction_number = GetFlag(args, "foreign-correction-number");
    }
    if (HasFlag(args, "foreign-package")) {
        context.foreign_package = GetFlag(args, "foreign-package");
    }
    return context;
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
    SearchOptions opts;
    opts.query = args.positional[0];
    if (HasFlag(args, "max")) {
        auto max_result = ParseIntInRange(
            GetFlag(args, "max"),
            1,
            std::numeric_limits<int>::max(),
            "--max");
        if (max_result.IsErr()) {
            fmt.PrintError(max_result.Error());
            return 99;
        }
        opts.max_results = max_result.Value();
    }
    if (HasFlag(args, "type")) {
        auto type_val = GetFlag(args, "type");
        if (type_val == "true" || type_val.empty()) {
            fmt.PrintError(MakeValidationError(
                "Missing value for --type. Usage: --type=CLAS (valid types: CLAS, PROG, INTF, TABL, FUGR, DTEL, DOMA, SHLP, MSAG, TTYP)"));
            return 99;
        }
        opts.object_type = type_val;
    }

    auto result = SearchObjects(*session, opts);
    if (result.IsErr()) {
        auto err = result.Error();
        if (err.http_status.has_value() && err.http_status.value() == 406) {
            err.hint = "Check --type value. Valid types: CLAS, PROG, INTF, TABL, FUGR, DTEL, DOMA, SHLP, MSAG, TTYP";
        }
        fmt.PrintError(err);
        return err.ExitCode();
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
    auto result = GetObjectStructure(*session, uri_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& obj = result.Value();

    // Default source_uri for class types on ABAP Cloud where XML omits it
    auto source_uri = obj.info.source_uri;
    if (source_uri.empty() && obj.info.type.substr(0, 5) == "CLAS/") {
        source_uri = "source/main";
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = obj.info.name;
        j["type"] = obj.info.type;
        j["uri"] = obj.info.uri;
        j["description"] = obj.info.description;
        j["source_uri"] = source_uri;
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
        if (!source_uri.empty()) {
            std::cout << "  Source: " << source_uri << "\n";
        }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto handle_str = GetFlag(args, "handle");

    std::optional<LockHandle> explicit_handle;
    if (!handle_str.empty()) {
        auto handle_result = LockHandle::Create(handle_str);
        if (handle_result.IsErr()) {
            fmt.PrintError(MakeValidationError("Invalid handle: " + handle_result.Error()));
            return 99;
        }
        explicit_handle = std::move(handle_result).Value();
    }

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }

    if (explicit_handle.has_value()) {
        // Explicit handle: use it directly (advanced / session-file mode).
        auto result = DeleteObject(*session, uri_result.Value(),
                                   *explicit_handle, transport);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
    } else {
        // Auto-lock mode: lock → delete → unlock in a single session.
        auto del_result = DeleteObjectWithAutoLock(
            *session, uri_result.Value(), transport);
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
    session->SetStateful(true);

    auto result = LockObject(*session, uri_result.Value());

    // On "Session not found", the loaded session file had a stale context.
    // Clear it and retry once with a fresh stateful establishment.
    if (result.IsErr() && result.Error().http_status == 400 &&
        result.Error().message.find("Session not found") != std::string::npos) {
        session->ResetStatefulSession();
        result = LockObject(*session, uri_result.Value());
    }

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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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
        fmt.PrintError(MakeValidationError("Missing source URI or object name. Usage: erpl-adt source read <name-or-uri>"));
        return 99;
    }

    auto version     = GetFlag(args, "version", "active");
    auto section     = GetFlag(args, "section",  "main");
    auto type_filter = GetFlag(args, "type");

    static const std::vector<std::string> kValidSections = {
        "main", "localdefinitions", "localimplementations", "testclasses", "all"};
    if (std::find(kValidSections.begin(), kValidSections.end(), section) == kValidSections.end()) {
        fmt.PrintError(MakeValidationError(
            "Invalid --section value '" + section +
            "'. Valid values: main, localdefinitions, localimplementations, testclasses, all"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }

    const auto& arg = args.positional[0];

    // Determine the base object URI (without /source/...) and whether the
    // caller already supplied a full source URI.
    std::string base_uri;
    bool arg_is_full_source_uri = false;

    if (arg.find("/sap/bc/adt/") == 0) {
        auto source_pos = arg.find("/source/");
        if (source_pos != std::string::npos) {
            arg_is_full_source_uri = true;
            base_uri = arg.substr(0, source_pos);
        } else {
            base_uri = arg;
        }
    } else {
        // Name resolution via search.
        if (!type_filter.empty()) {
            // Caller supplied --type: filter search results.
            SearchOptions opts;
            opts.query       = arg;
            opts.max_results = 10;
            opts.object_type = type_filter;
            auto search_result = SearchObjects(*session, opts);
            if (search_result.IsErr()) {
                fmt.PrintError(search_result.Error());
                return search_result.Error().ExitCode();
            }
            std::string upper_arg = arg;
            std::transform(upper_arg.begin(), upper_arg.end(), upper_arg.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            for (const auto& item : search_result.Value()) {
                std::string upper_name = item.name;
                std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                               [](unsigned char c) { return std::toupper(c); });
                if (upper_name == upper_arg) {
                    base_uri = item.uri;
                    break;
                }
            }
            if (base_uri.empty()) {
                Error err;
                err.operation = "SourceRead";
                err.message   = "Object not found: " + arg;
                err.category  = ErrorCategory::NotFound;
                fmt.PrintError(err);
                return 2;
            }
        } else {
            // No type filter: delegate to existing resolver.
            auto resolved = ResolveObjectUri(*session, arg);
            if (resolved.IsErr()) {
                fmt.PrintError(resolved.Error());
                return resolved.Error().ExitCode();
            }
            base_uri = resolved.Value();
        }
    }

    // --section all: read every section, skip duplicates with a stderr notice.
    static const std::vector<std::string> kAllSections = {
        "main", "localdefinitions", "localimplementations", "testclasses"};

    const bool color_mode = ColorMode(args);
    const bool editor_mode = HasFlag(args, "editor");

    if (section == "all") {
        auto main_result = ReadSource(*session, base_uri + "/source/main", version);
        if (main_result.IsErr()) {
            fmt.PrintError(main_result.Error());
            return main_result.Error().ExitCode();
        }
        const auto& main_source = main_result.Value();

        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["sections"]["main"] = main_source;
            for (const auto& sec : kAllSections) {
                if (sec == "main") continue;
                auto sec_result = ReadSource(*session, base_uri + "/source/" + sec, version);
                if (sec_result.IsOk() && !sec_result.Value().empty() &&
                    sec_result.Value() != main_source) {
                    j["sections"][sec] = sec_result.Value();
                } else {
                    j["sections"][sec] = nullptr;
                }
            }
            if (editor_mode) {
                auto tmp = MakeTempPath(".json");
                std::ofstream tf(tmp);
                tf << j.dump(2);
                tf.close();
                LaunchEditor(tmp);
                std::remove(tmp.c_str());
            } else {
                fmt.PrintJson(j.dump());
            }
        } else {
            // Collect all sections into a combined string for --editor mode.
            std::ostringstream combined;
            combined << "*--- source/main ---*\n";
            combined << main_source;
            if (!main_source.empty() && main_source.back() != '\n') combined << '\n';
            for (const auto& sec : kAllSections) {
                if (sec == "main") continue;
                auto sec_result = ReadSource(*session, base_uri + "/source/" + sec, version);
                combined << "\n*--- source/" << sec << " ---*\n";
                if (sec_result.IsOk() && !sec_result.Value().empty() &&
                    sec_result.Value() != main_source) {
                    combined << sec_result.Value();
                    if (sec_result.Value().back() != '\n') combined << '\n';
                } else {
                    std::cerr << "Note: source/" << sec << " is not separately available"
                              << " on this system (returned same content as source/main or empty).\n";
                    combined << "[not available]\n";
                }
            }
            if (editor_mode) {
                auto tmp = MakeTempPath(".abap");
                std::ofstream tf(tmp);
                tf << combined.str();
                tf.close();
                LaunchEditor(tmp);
                std::remove(tmp.c_str());
            } else {
                std::cout << HighlightSource(combined.str(), SourceLanguage::Abap, color_mode);
            }
        }
        return 0;
    }

    // Single section.
    // Preserve exact URI when caller passed a full source URI and wants main.
    std::string source_uri = (arg_is_full_source_uri && section == "main")
                             ? arg
                             : base_uri + "/source/" + section;

    auto result = ReadSource(*session, source_uri, version);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    // Warn when a non-main section silently echoes the main source.
    if (section != "main") {
        auto main_result = ReadSource(*session, base_uri + "/source/main", version);
        if (main_result.IsOk() && main_result.Value() == result.Value()) {
            std::cerr << "Note: source/" << section << " returned the same content as"
                      << " source/main on this system.\n"
                      << "      The local class definitions (CCDEF/CCIMP includes) may not\n"
                      << "      be separately accessible via ADT on this ABAP system.\n";
        }
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["source"] = result.Value();
        if (editor_mode) {
            auto tmp = MakeTempPath(".json");
            std::ofstream tf(tmp);
            tf << j.dump(2);
            tf.close();
            LaunchEditor(tmp);
            std::remove(tmp.c_str());
        } else {
            fmt.PrintJson(j.dump());
        }
    } else {
        const auto& src = result.Value();
        if (editor_mode) {
            auto tmp = MakeTempPath(".abap");
            std::ofstream tf(tmp);
            tf << src;
            tf.close();
            LaunchEditor(tmp);
            std::remove(tmp.c_str());
        } else {
            auto highlighted = HighlightSource(src, SourceLanguage::Abap, color_mode);
            std::cout << highlighted;
            if (!highlighted.empty() && highlighted.back() != '\n') {
                std::cout << '\n';
            }
        }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
    auto handle_str = GetFlag(args, "handle");

    // Derive object URI for --activate flag (needed in both paths).
    std::string obj_uri_for_activate;

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

        // Try to derive object URI from source URI for activation.
        auto slash_pos = std::string(args.positional[0]).find("/source/");
        if (slash_pos != std::string::npos) {
            obj_uri_for_activate = std::string(args.positional[0]).substr(0, slash_pos);
        }
    } else {
        // Auto-lock mode: derive object URI, lock → write → unlock.
        auto write_result = WriteSourceWithAutoLock(
            *session, args.positional[0], source, transport);
        if (write_result.IsErr()) {
            fmt.PrintError(write_result.Error());
            return write_result.Error().ExitCode();
        }
        obj_uri_for_activate = std::move(write_result).Value();
    }

    fmt.PrintSuccess("Source written: " + args.positional[0]);

    // Optional activation after successful write.
    if (HasFlag(args, "activate") && !obj_uri_for_activate.empty()) {
        ActivateObjectParams act_params;
        act_params.uri = obj_uri_for_activate;
        auto act_result = ActivateObject(*session, act_params);
        if (act_result.IsErr()) {
            fmt.PrintError(act_result.Error());
            return act_result.Error().ExitCode();
        }
        if (act_result.Value().failed > 0) {
            for (const auto& msg : act_result.Value().error_messages) {
                std::cerr << "  Activation warning: " << msg << "\n";
            }
        }
        fmt.PrintSuccess("Activated: " + obj_uri_for_activate);
    }

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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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
// activate run
// ---------------------------------------------------------------------------
int HandleActivateRun(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Missing object name or URI. Usage: erpl-adt activate <name-or-uri>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }

    auto info_result = ResolveObjectInfo(*session, args.positional[0]);
    if (info_result.IsErr()) {
        fmt.PrintError(info_result.Error());
        return info_result.Error().ExitCode();
    }

    const auto& info = info_result.Value();
    ActivateObjectParams params;
    params.uri = info.uri;
    params.type = info.type;
    params.name = info.name;

    auto result = ActivateObject(*session, params);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& act = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["activated"] = act.activated;
        j["failed"] = act.failed;
        nlohmann::json msgs = nlohmann::json::array();
        for (const auto& m : act.error_messages) {
            msgs.push_back(m);
        }
        j["error_messages"] = msgs;
        fmt.PrintJson(j.dump());
    } else {
        if (act.failed > 0) {
            std::cerr << "Activation completed with " << act.failed << " error(s)\n";
            for (const auto& m : act.error_messages) {
                std::cerr << "  " << m << "\n";
            }
            return 5;
        }
        fmt.PrintSuccess("Activated: " + args.positional[0]);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// test run
// ---------------------------------------------------------------------------
int HandleTestRun(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Missing object name or URI. Usage: erpl-adt test [run] <name-or-uri>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }

    auto uri_result = ResolveObjectUri(*session, args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(uri_result.Error());
        return uri_result.Error().ExitCode();
    }

    auto result = RunTests(*session, uri_result.Value());
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& tr = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["total_methods"] = tr.TotalMethods();
        j["total_failed"] = tr.TotalFailed();
        j["total_skipped"] = tr.TotalSkipped();
        j["all_passed"] = tr.AllPassed();
        if (tr.TotalMethods() == 0 && tr.TotalSkipped() == 0) {
            j["note"] = "No test methods found";
        }
        nlohmann::json classes = nlohmann::json::array();
        for (const auto& c : tr.classes) {
            nlohmann::json class_alerts = nlohmann::json::array();
            for (const auto& a : c.alerts) {
                class_alerts.push_back({{"kind", a.kind},
                                        {"severity", a.severity},
                                        {"title", a.title},
                                        {"detail", a.detail}});
            }
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
            nlohmann::json cj = {{"name", c.name},
                                 {"uri", c.uri},
                                 {"skipped", c.Skipped()},
                                 {"methods", methods}};
            if (!class_alerts.empty()) {
                cj["alerts"] = class_alerts;
            }
            classes.push_back(cj);
        }
        j["classes"] = classes;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Test results: " << tr.TotalMethods() << " methods, "
                  << tr.TotalFailed() << " failed\n";
        for (const auto& c : tr.classes) {
            if (c.Skipped()) {
                std::cout << "  [SKIP] " << c.name << "\n";
                for (const auto& a : c.alerts) {
                    std::cout << "    " << a.severity << ": " << a.title << "\n";
                }
                continue;
            }
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
        fmt.PrintError(MakeValidationError(
            "Missing object name or URI. Usage: erpl-adt check [run] <name-or-uri>"));
        return 99;
    }

    auto variant = GetFlag(args, "variant", "DEFAULT");
    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }

    auto uri_result = ResolveObjectUri(*session, args.positional[0]);
    if (uri_result.IsErr()) {
        fmt.PrintError(uri_result.Error());
        return uri_result.Error().ExitCode();
    }

    auto result = RunAtcCheck(*session, uri_result.Value(), variant);
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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
        if (table.fields.empty()) {
            std::cerr << "Note: Field definitions may be in DDL source on ABAP Cloud systems. "
                      << "Try 'erpl-adt ddic cds " << table.name << "' instead.\n";
        }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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
        auto depth_result = ParseIntInRange(
            GetFlag(args, "max-depth"),
            1,
            std::numeric_limits<int>::max(),
            "--max-depth");
        if (depth_result.IsErr()) {
            fmt.PrintError(depth_result.Error());
            return 99;
        }
        opts.max_depth = depth_result.Value();
    }

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
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

    auto session = RequireSession(args, fmt);
    if (!session) {
        return 99;
    }
    XmlCodec codec;
    auto result = Discover(*session, codec);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& disc = result.Value();
    auto workspace_filter = GetFlag(args, "workspace");

    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        for (const auto& ws : disc.workspaces) {
            if (!workspace_filter.empty() && ws.title != workspace_filter) {
                continue;
            }
            std::cout << ws.title << " (" << ws.services.size()
                      << (ws.services.size() == 1 ? " service" : " services")
                      << ")\n";
            for (const auto& s : ws.services) {
                std::cout << "  " << s.title;
                // Pad to align hrefs.
                auto pad = (s.title.size() < 30)
                    ? std::string(30 - s.title.size(), ' ')
                    : std::string(2, ' ');
                std::cout << pad << s.href << "\n";
            }
            std::cout << "\n";
        }
        std::cout << "Capabilities:\n";
        std::cout << "  abapGit: " << (disc.has_abapgit_support ? "yes" : "no") << "\n";
        std::cout << "  Packages: " << (disc.has_packages_support ? "yes" : "no") << "\n";
        std::cout << "  Activation: " << (disc.has_activation_support ? "yes" : "no") << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw discover
// ---------------------------------------------------------------------------
int HandleBwDiscover(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwDiscover(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& disc = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& s : disc.services) {
            j.push_back({{"scheme", s.scheme},
                         {"term", s.term},
                         {"href", s.href},
                         {"content_type", s.content_type}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Scheme", "Term", "URI", "Content-Type"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& s : disc.services) {
            rows.push_back({s.scheme, s.term, s.href, s.content_type});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw search (default action)
// ---------------------------------------------------------------------------
int HandleBwSearch(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Missing search pattern. Usage: erpl-adt bw search <pattern>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwSearchOptions opts;
    opts.query = args.positional[0];
    if (HasFlag(args, "max")) {
        auto max_result = ParseIntInRange(
            GetFlag(args, "max"), 1,
            std::numeric_limits<int>::max(), "--max");
        if (max_result.IsErr()) {
            fmt.PrintError(max_result.Error());
            return 99;
        }
        opts.max_results = max_result.Value();
    }
    if (HasFlag(args, "type")) {
        opts.object_type = GetFlag(args, "type");
    }
    if (HasFlag(args, "subtype")) {
        opts.object_sub_type = GetFlag(args, "subtype");
    }
    if (HasFlag(args, "status")) {
        opts.object_status = GetFlag(args, "status");
    }
    if (HasFlag(args, "changed-by")) {
        opts.changed_by = GetFlag(args, "changed-by");
    }
    if (HasFlag(args, "changed-from")) {
        opts.changed_on_from = GetFlag(args, "changed-from");
    }
    if (HasFlag(args, "changed-to")) {
        opts.changed_on_to = GetFlag(args, "changed-to");
    }
    if (HasFlag(args, "created-by")) {
        opts.created_by = GetFlag(args, "created-by");
    }
    if (HasFlag(args, "created-from")) {
        opts.created_on_from = GetFlag(args, "created-from");
    }
    if (HasFlag(args, "created-to")) {
        opts.created_on_to = GetFlag(args, "created-to");
    }
    if (HasFlag(args, "depends-on-name")) {
        opts.depends_on_name = GetFlag(args, "depends-on-name");
    }
    if (HasFlag(args, "depends-on-type")) {
        opts.depends_on_type = GetFlag(args, "depends-on-type");
    }
    if (HasFlag(args, "infoarea")) {
        opts.info_area = GetFlag(args, "infoarea");
    }
    if (HasFlag(args, "search-desc")) {
        opts.search_in_description = true;
    }
    if (HasFlag(args, "search-name")) {
        opts.search_in_name = true;
    }

    BwTemplateParams path_params;
    BwTemplateParams query_params{
        {"searchTerm", opts.query},
        {"maxSize", std::to_string(opts.max_results)},
    };
    if (opts.object_type.has_value()) query_params["objectType"] = *opts.object_type;
    if (opts.object_sub_type.has_value()) query_params["objectSubType"] = *opts.object_sub_type;
    if (opts.object_status.has_value()) query_params["objectStatus"] = *opts.object_status;
    if (opts.object_version.has_value()) query_params["objectVersion"] = *opts.object_version;
    if (opts.changed_by.has_value()) query_params["changedBy"] = *opts.changed_by;
    if (opts.changed_on_from.has_value()) query_params["changedOnFrom"] = *opts.changed_on_from;
    if (opts.changed_on_to.has_value()) query_params["changedOnTo"] = *opts.changed_on_to;
    if (opts.created_by.has_value()) query_params["createdBy"] = *opts.created_by;
    if (opts.created_on_from.has_value()) query_params["createdOnFrom"] = *opts.created_on_from;
    if (opts.created_on_to.has_value()) query_params["createdOnTo"] = *opts.created_on_to;
    if (opts.depends_on_name.has_value()) query_params["dependsOnObjectName"] = *opts.depends_on_name;
    if (opts.depends_on_type.has_value()) query_params["dependsOnObjectType"] = *opts.depends_on_type;
    if (opts.info_area.has_value()) query_params["infoArea"] = *opts.info_area;
    if (opts.search_in_description) query_params["searchInDescription"] = "true";
    if (!opts.search_in_name) query_params["searchInName"] = "false";

    auto endpoint = TryResolveBwEndpoint(
        *session, "http://www.sap.com/bw/modeling/repo", "bwSearch",
        path_params, query_params);
    if (endpoint.has_value()) {
        opts.endpoint_override = *endpoint;
    }

    auto result = BwSearchObjects(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& items = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : items) {
            nlohmann::json obj = {{"name", r.name},
                                  {"type", r.type},
                                  {"subtype", r.subtype},
                                  {"description", r.description},
                                  {"version", r.version},
                                  {"status", r.status},
                                  {"uri", r.uri}};
            if (!r.technical_name.empty()) obj["technical_name"] = r.technical_name;
            if (!r.last_changed.empty()) obj["last_changed"] = r.last_changed;
            j.push_back(std::move(obj));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "Status", "Description", "Changed", "URI"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : items) {
            rows.push_back({r.name, r.type, r.status, r.description, r.last_changed, r.uri});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read
// ---------------------------------------------------------------------------
int HandleBwRead(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    bool has_uri = HasFlag(args, "uri");
    if (args.positional.size() < 2 && !has_uri) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read <type> <name> [--version=a|m|d]\n"
            "   or: erpl-adt bw read --uri <path>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwReadOptions opts;
    if (args.positional.size() >= 1) {
        opts.object_type = args.positional[0];
    }
    if (args.positional.size() >= 2) {
        opts.object_name = args.positional[1];
    }
    opts.version = GetFlag(args, "version", "a");
    if (HasFlag(args, "source-system")) {
        opts.source_system = GetFlag(args, "source-system");
    }
    if (has_uri) {
        opts.uri = GetFlag(args, "uri");
    }
    opts.raw = HasFlag(args, "raw");

    if (!has_uri && !opts.object_type.empty() && !opts.object_name.empty()) {
        BwTemplateParams path_params = BuildBwObjectPathParams(opts);
        BwTemplateParams query_params;
        auto scheme = "http://www.sap.com/bw/modeling/" + ToLowerCopy(opts.object_type);
        auto term = ToLowerCopy(opts.object_type);
        auto endpoint = TryResolveBwEndpoint(
            *session, scheme, term, path_params, query_params);
        if (endpoint.has_value()) {
            opts.uri = *endpoint;
        }
    }

    // Resolve content type from discovery (best-effort)
    if (!opts.object_type.empty()) {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            auto ct = BwResolveContentType(disc.Value(), opts.object_type);
            if (!ct.empty()) {
                opts.content_type = ct;
            }
        }
    }

    auto result = BwReadObject(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& meta = result.Value();
    if (opts.raw) {
        std::cout << meta.raw_xml;
        return 0;
    }

    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        // Build detail sections for tree view
        std::string title = meta.type + " " + meta.name;
        if (!meta.long_description.empty()) {
            title += " — " + meta.long_description;
        } else if (!meta.short_description.empty()) {
            title += " — " + meta.short_description;
        } else if (!meta.description.empty()) {
            title += " — " + meta.description;
        }

        OutputFormatter::DetailSection main_section;
        if (!meta.description.empty())
            main_section.entries.emplace_back("Description", meta.description);
        if (!meta.sub_type.empty())
            main_section.entries.emplace_back("Sub Type", meta.sub_type);
        main_section.entries.emplace_back("Version", meta.version);

        // Status line: combine status and content_state
        if (!meta.status.empty()) {
            std::string status_str = meta.status;
            if (!meta.content_state.empty())
                status_str += " (" + meta.content_state + ")";
            main_section.entries.emplace_back("Status", status_str);
        }
        if (!meta.info_area.empty())
            main_section.entries.emplace_back("Info Area", meta.info_area);
        if (!meta.package_name.empty())
            main_section.entries.emplace_back("Package", meta.package_name);
        if (!meta.responsible.empty())
            main_section.entries.emplace_back("Responsible", meta.responsible);
        if (!meta.last_changed_by.empty()) {
            std::string changed = meta.last_changed_by;
            if (!meta.last_changed_at.empty())
                changed += " at " + meta.last_changed_at;
            main_section.entries.emplace_back("Changed", changed);
        }
        if (!meta.created_at.empty())
            main_section.entries.emplace_back("Created", meta.created_at);
        if (!meta.language.empty())
            main_section.entries.emplace_back("Language", meta.language);

        std::vector<OutputFormatter::DetailSection> sections;
        sections.push_back(std::move(main_section));

        // Properties sub-section
        if (!meta.properties.empty()) {
            OutputFormatter::DetailSection props_section;
            props_section.title = "Properties";
            for (const auto& kv : meta.properties) {
                props_section.entries.emplace_back(kv.first, kv.second);
            }
            sections.push_back(std::move(props_section));
        }

        fmt.PrintDetail(title, sections);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw lock
// ---------------------------------------------------------------------------
int HandleBwLock(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw lock <type> <name>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;
    session->SetStateful(true);

    BwLockOptions lock_options;
    lock_options.object_type = args.positional[0];
    lock_options.object_name = args.positional[1];
    lock_options.activity = GetFlag(args, "activity", "CHAN");
    if (HasFlag(args, "parent-name")) {
        lock_options.parent_name = GetFlag(args, "parent-name");
    }
    if (HasFlag(args, "parent-type")) {
        lock_options.parent_type = GetFlag(args, "parent-type");
    }
    lock_options.context_headers = ParseBwContextHeaders(args);

    auto result = BwLockObject(*session, lock_options);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    MaybeSaveSession(*session, args);

    const auto& lock = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["lock_handle"] = lock.lock_handle;
        j["transport"] = lock.transport_number;
        j["timestamp"] = lock.timestamp;
        j["package"] = lock.package_name;
        j["is_local"] = lock.is_local;
        if (!lock.transport_text.empty()) j["transport_text"] = lock.transport_text;
        if (!lock.transport_owner.empty()) j["transport_owner"] = lock.transport_owner;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Locked: " << args.positional[0] << " "
                  << args.positional[1] << "\n";
        std::cout << "  Handle: " << lock.lock_handle << "\n";
        if (!lock.transport_number.empty()) {
            std::cout << "  Transport: " << lock.transport_number;
            if (!lock.transport_text.empty()) std::cout << " (" << lock.transport_text << ")";
            if (!lock.transport_owner.empty()) std::cout << " [" << lock.transport_owner << "]";
            std::cout << "\n";
        }
        if (!lock.timestamp.empty())
            std::cout << "  Timestamp: " << lock.timestamp << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw unlock
// ---------------------------------------------------------------------------
int HandleBwUnlock(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw unlock <type> <name>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwUnlockObject(*session, args.positional[0],
                                  args.positional[1]);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    MaybeDeleteSessionFile(args);
    fmt.PrintSuccess("Unlocked: " + args.positional[0] + " " + args.positional[1]);
    return 0;
}

// ---------------------------------------------------------------------------
// bw save
// ---------------------------------------------------------------------------
int HandleBwSave(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw save <type> <name> --lock-handle=... < file.xml"));
        return 99;
    }

    auto lock_handle = GetFlag(args, "lock-handle");
    if (lock_handle.empty()) {
        fmt.PrintError(MakeValidationError("Missing --lock-handle flag"));
        return 99;
    }

    // Read content from stdin
    std::string content;
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    content = ss.str();
    if (content.empty()) {
        fmt.PrintError(MakeValidationError("No content on stdin"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwSaveOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    opts.content = std::move(content);
    opts.lock_handle = lock_handle;
    opts.transport = GetFlag(args, "transport");
    opts.timestamp = GetFlag(args, "timestamp");
    opts.context_headers = ParseBwContextHeaders(args);

    auto result = BwSaveObject(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    fmt.PrintSuccess("Saved: " + args.positional[0] + " " + args.positional[1]);
    return 0;
}

// ---------------------------------------------------------------------------
// bw delete
// ---------------------------------------------------------------------------
int HandleBwDelete(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw delete <type> <name> --lock-handle=..."));
        return 99;
    }

    auto lock_handle = GetFlag(args, "lock-handle");
    if (lock_handle.empty()) {
        fmt.PrintError(MakeValidationError("Missing --lock-handle flag"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwDeleteOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    opts.lock_handle = lock_handle;
    opts.transport = GetFlag(args, "transport");
    opts.context_headers = ParseBwContextHeaders(args);

    auto result = BwDeleteObject(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    fmt.PrintSuccess("Deleted: " + args.positional[0] + " " + args.positional[1]);
    return 0;
}

// ---------------------------------------------------------------------------
// bw activate
// ---------------------------------------------------------------------------
int HandleBwActivate(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw activate <type> <name> [<name2> ...]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwActivateOptions opts;

    // First positional is type, rest are names
    auto type = args.positional[0];
    for (size_t i = 1; i < args.positional.size(); ++i) {
        BwActivationObject obj;
        obj.name = args.positional[i];
        obj.type = type;
        obj.uri = "/sap/bw/modeling/" + type + "/" + args.positional[i] + "/m";
        opts.objects.push_back(std::move(obj));
    }

    if (HasFlag(args, "validate")) {
        opts.mode = BwActivationMode::Validate;
    } else if (HasFlag(args, "simulate")) {
        opts.mode = BwActivationMode::Simulate;
    } else if (HasFlag(args, "background")) {
        opts.mode = BwActivationMode::Background;
    }
    opts.force = HasFlag(args, "force");
    opts.exec_checks = HasFlag(args, "exec-check");
    opts.with_cto = HasFlag(args, "with-cto");
    opts.sort = HasFlag(args, "sort");
    opts.only_inactive = HasFlag(args, "only-ina");
    if (HasFlag(args, "transport")) {
        opts.transport = GetFlag(args, "transport");
    }

    {
        BwTemplateParams path_params;
        BwTemplateParams query_params;
        auto endpoint = TryResolveBwEndpoint(
            *session,
            "http://www.sap.com/bw/modeling/activation",
            "activate",
            path_params,
            query_params);
        if (endpoint.has_value()) {
            opts.endpoint_override = *endpoint;
        }
    }

    auto result = BwActivateObjects(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& act = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        if (act.success) {
            fmt.PrintSuccess("Activation successful");
        } else {
            std::cerr << "Activation completed with errors\n";
        }
        if (!act.job_guid.empty()) {
            std::cout << "Job GUID: " << act.job_guid << "\n";
        }
        for (const auto& m : act.messages) {
            std::cout << "  [" << m.severity << "] " << m.text << "\n";
        }
    }
    return act.success ? 0 : 5;
}

// ---------------------------------------------------------------------------
// bw xref
// ---------------------------------------------------------------------------
int HandleBwXref(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw xref <type> <name> [flags]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwXrefOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    if (HasFlag(args, "version")) {
        opts.object_version = GetFlag(args, "version");
    }
    if (HasFlag(args, "association")) {
        opts.association = GetFlag(args, "association");
    }
    if (HasFlag(args, "assoc-type")) {
        opts.associated_object_type = GetFlag(args, "assoc-type");
    }
    if (HasFlag(args, "max")) {
        auto max_result = ParseIntInRange(
            GetFlag(args, "max"), 1,
            std::numeric_limits<int>::max(), "--max");
        if (max_result.IsErr()) {
            fmt.PrintError(max_result.Error());
            return 99;
        }
        opts.max_results = max_result.Value();
    }

    BwTemplateParams path_params;
    BwTemplateParams query_params{
        {"objectType", opts.object_type},
        {"objectName", opts.object_name},
    };
    if (opts.object_version.has_value()) query_params["objectVersion"] = *opts.object_version;
    if (opts.association.has_value()) query_params["association"] = *opts.association;
    if (opts.associated_object_type.has_value()) query_params["associatedObjectType"] = *opts.associated_object_type;
    if (opts.max_results > 0) query_params["$top"] = std::to_string(opts.max_results);

    auto endpoint = TryResolveBwEndpoint(
        *session, "http://www.sap.com/bw/modeling/repo", "xref",
        path_params, query_params);
    if (endpoint.has_value()) {
        opts.endpoint_override = *endpoint;
    }

    auto result = BwGetXrefs(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& items = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : items) {
            j.push_back({{"name", r.name},
                         {"type", r.type},
                         {"association_type", r.association_type},
                         {"association_label", r.association_label},
                         {"version", r.version},
                         {"status", r.status},
                         {"description", r.description},
                         {"uri", r.uri}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "Association", "Description", "URI"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : items) {
            rows.push_back({r.name, r.type, r.association_label, r.description, r.uri});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw nodes
// ---------------------------------------------------------------------------
int HandleBwNodes(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw nodes <type> <name> [flags]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwNodesOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    opts.datasource = HasFlag(args, "datasource");
    if (HasFlag(args, "child-name")) {
        opts.child_name = GetFlag(args, "child-name");
    }
    if (HasFlag(args, "child-type")) {
        opts.child_type = GetFlag(args, "child-type");
    }

    BwTemplateParams path_params{
        {"objectType", opts.object_type},
        {"objectName", opts.object_name},
    };
    BwTemplateParams query_params;
    if (opts.child_name.has_value()) query_params["childName"] = *opts.child_name;
    if (opts.child_type.has_value()) query_params["childType"] = *opts.child_type;

    auto endpoint = TryResolveBwEndpoint(
        *session,
        "http://www.sap.com/bw/modeling/repo",
        opts.datasource ? "datasourcenodes" : "nodes",
        path_params, query_params);
    if (endpoint.has_value()) {
        opts.endpoint_override = *endpoint;
    }

    auto result = BwGetNodes(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& items = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : items) {
            j.push_back({{"name", r.name},
                         {"type", r.type},
                         {"subtype", r.subtype},
                         {"description", r.description},
                         {"version", r.version},
                         {"status", r.status},
                         {"uri", r.uri}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "Subtype", "Status", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : items) {
            rows.push_back({r.name, r.type, r.subtype, r.status, r.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw search-md
// ---------------------------------------------------------------------------
int HandleBwSearchMetadata(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwGetSearchMetadata(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& entries = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries) {
            nlohmann::json row{
                {"name", e.name},
                {"value", e.value},
                {"description", e.description},
                {"category", e.category}};
            j.push_back(std::move(row));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Value", "Category", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            rows.push_back({e.name, e.value, e.category, e.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw favorites (sub-actions: list, clear)
// ---------------------------------------------------------------------------
int HandleBwFavorites(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto sub_action = args.positional.empty() ? "list" : args.positional[0];

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (sub_action == "clear") {
        auto result = BwDeleteAllBackendFavorites(*session);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        fmt.PrintSuccess("BW backend favorites cleared");
        return 0;
    }

    if (sub_action != "list") {
        fmt.PrintError(MakeValidationError(
            "Unknown favorites action: " + sub_action + ". Use list or clear."));
        return 99;
    }

    auto result = BwListBackendFavorites(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& favorites = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& f : favorites) {
            j.push_back({{"name", f.name},
                         {"type", f.type},
                         {"description", f.description},
                         {"uri", f.uri}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "Description", "URI"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& f : favorites) {
            rows.push_back({f.name, f.type, f.description, f.uri});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw nodepath
// ---------------------------------------------------------------------------
int HandleBwNodePath(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    std::string object_uri;
    if (HasFlag(args, "object-uri")) {
        object_uri = GetFlag(args, "object-uri");
    } else if (!args.positional.empty()) {
        object_uri = args.positional[0];
    }

    if (object_uri.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw nodepath --object-uri <bw object uri>"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwGetNodePath(*session, object_uri);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& nodes = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& n : nodes) {
            j.push_back({{"name", n.name},
                         {"type", n.type},
                         {"uri", n.uri}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Name", "Type", "URI"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& n : nodes) {
            rows.push_back({n.name, n.type, n.uri});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw applog
// ---------------------------------------------------------------------------
int HandleBwApplicationLog(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwApplicationLogOptions opts;
    if (HasFlag(args, "username")) {
        opts.username = GetFlag(args, "username");
    }
    if (HasFlag(args, "start")) {
        opts.start_timestamp = GetFlag(args, "start");
    }
    if (HasFlag(args, "end")) {
        opts.end_timestamp = GetFlag(args, "end");
    }

    auto result = BwGetApplicationLog(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& logs = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& l : logs) {
            j.push_back({{"identifier", l.identifier},
                         {"user", l.user},
                         {"timestamp", l.timestamp},
                         {"severity", l.severity},
                         {"text", l.text}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Identifier", "User", "Timestamp", "Severity", "Text"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& l : logs) {
            rows.push_back({l.identifier, l.user, l.timestamp, l.severity, l.text});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw message
// ---------------------------------------------------------------------------
int HandleBwMessage(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw message <identifier> <textype> [--msgv1=...] [--msgv2=...] [--msgv3=...] [--msgv4=...]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwMessageTextOptions opts;
    opts.identifier = args.positional[0];
    opts.text_type = args.positional[1];
    if (HasFlag(args, "msgv1")) opts.msgv1 = GetFlag(args, "msgv1");
    if (HasFlag(args, "msgv2")) opts.msgv2 = GetFlag(args, "msgv2");
    if (HasFlag(args, "msgv3")) opts.msgv3 = GetFlag(args, "msgv3");
    if (HasFlag(args, "msgv4")) opts.msgv4 = GetFlag(args, "msgv4");

    auto result = BwGetMessageText(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& message = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j{
            {"identifier", message.identifier},
            {"text_type", message.text_type},
            {"text", message.text}};
        fmt.PrintJson(j.dump());
    } else {
        std::cout << message.text << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw validate
// ---------------------------------------------------------------------------
int HandleBwValidate(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw validate <type> <name> [--action=validate]"));
        return 99;
    }

    BwValidationOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    opts.action = GetFlag(args, "action", "validate");

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwValidateObject(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& messages = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& m : messages) {
            j.push_back({{"severity", m.severity},
                         {"text", m.text},
                         {"object_type", m.object_type},
                         {"object_name", m.object_name},
                         {"code", m.code}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Severity", "Code", "Type", "Name", "Text"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& m : messages) {
            rows.push_back({m.severity, m.code, m.object_type, m.object_name, m.text});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw move (sub-actions: list)
// ---------------------------------------------------------------------------
int HandleBwMove(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto sub_action = args.positional.empty() ? "list" : args.positional[0];
    if (sub_action != "list") {
        fmt.PrintError(MakeValidationError(
            "Unknown move action: " + sub_action + ". Use list."));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwListMoveRequests(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& entries = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries) {
            j.push_back({{"request", e.request},
                         {"owner", e.owner},
                         {"status", e.status},
                         {"description", e.description}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Request", "Owner", "Status", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            rows.push_back({e.request, e.owner, e.status, e.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw create
// ---------------------------------------------------------------------------
int HandleBwCreate(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw create <type> <name> [--package=...] [--copy-from-name=...] [--copy-from-type=...] [--file=...]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwCreateOptions opts;
    opts.object_type = args.positional[0];
    opts.object_name = args.positional[1];
    if (HasFlag(args, "package")) opts.package_name = GetFlag(args, "package");
    if (HasFlag(args, "copy-from-name")) opts.copy_from_name = GetFlag(args, "copy-from-name");
    if (HasFlag(args, "copy-from-type")) opts.copy_from_type = GetFlag(args, "copy-from-type");
    if (HasFlag(args, "file")) {
        std::ifstream ifs(GetFlag(args, "file"));
        if (!ifs) {
            fmt.PrintError(MakeValidationError("Unable to read --file path"));
            return 99;
        }
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        opts.content = buffer.str();
    }

    auto result = BwCreateObject(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["uri"] = result.Value().uri;
        j["http_status"] = result.Value().http_status;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Created: " << opts.object_type << " " << opts.object_name << "\n";
        std::cout << "  URI: " << result.Value().uri << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw valuehelp
// ---------------------------------------------------------------------------
int HandleBwValueHelp(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw valuehelp <domain> [--query='k=v&k2=v2'] [--max=100] [--pattern=...]"));
        return 99;
    }

    BwValueHelpOptions opts;
    opts.domain = args.positional[0];
    if (HasFlag(args, "query")) opts.raw_query = GetFlag(args, "query");
    if (HasFlag(args, "max")) {
        auto max_result = ParseIntInRange(GetFlag(args, "max"), 1, 100000, "--max");
        if (max_result.IsErr()) {
            fmt.PrintError(max_result.Error());
            return 99;
        }
        opts.max_rows = max_result.Value();
    }
    if (HasFlag(args, "pattern")) opts.pattern = GetFlag(args, "pattern");
    if (HasFlag(args, "type")) opts.object_type = GetFlag(args, "type");
    if (HasFlag(args, "infoprovider")) opts.infoprovider = GetFlag(args, "infoprovider");

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwGetValueHelp(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& rows = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& row : rows) {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : row.fields) r[k] = v;
            j.push_back(std::move(r));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Rows: " << rows.size() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw virtualfolders
// ---------------------------------------------------------------------------
int HandleBwVirtualFolders(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    std::optional<std::string> package_name;
    std::optional<std::string> object_type;
    std::optional<std::string> user_name;
    if (HasFlag(args, "package")) package_name = GetFlag(args, "package");
    if (HasFlag(args, "type")) object_type = GetFlag(args, "type");
    if (HasFlag(args, "user")) user_name = GetFlag(args, "user");

    auto result = BwGetVirtualFolders(*session, package_name, object_type, user_name);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& row : result.Value()) {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : row.fields) r[k] = v;
            j.push_back(std::move(r));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Rows: " << result.Value().size() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw datavolumes
// ---------------------------------------------------------------------------
int HandleBwDataVolumes(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    std::optional<std::string> infoprovider;
    std::optional<int> max_rows;
    if (HasFlag(args, "infoprovider")) infoprovider = GetFlag(args, "infoprovider");
    if (HasFlag(args, "max")) {
        auto max_result = ParseIntInRange(GetFlag(args, "max"), 1, 100000, "--max");
        if (max_result.IsErr()) {
            fmt.PrintError(max_result.Error());
            return 99;
        }
        max_rows = max_result.Value();
    }

    auto result = BwGetDataVolumes(*session, infoprovider, max_rows);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& row : result.Value()) {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : row.fields) r[k] = v;
            j.push_back(std::move(r));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Rows: " << result.Value().size() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw reporting
// ---------------------------------------------------------------------------
int HandleBwReporting(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw reporting <compid> [--dbgmode] [--metadata-only] [--incl-metadata]"));
        return 99;
    }
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwReportingOptions opts;
    opts.compid = args.positional[0];
    opts.dbgmode = HasFlag(args, "dbgmode");
    opts.metadata_only = HasFlag(args, "metadata-only");
    opts.incl_metadata = HasFlag(args, "incl-metadata");
    opts.incl_object_values = HasFlag(args, "incl-object-values");
    opts.incl_except_def = HasFlag(args, "incl-except-def");
    opts.compact_mode = HasFlag(args, "compact-mode");
    if (HasFlag(args, "from-row")) {
        auto p = ParseIntInRange(GetFlag(args, "from-row"), 0, 1000000, "--from-row");
        if (p.IsErr()) {
            fmt.PrintError(p.Error());
            return 99;
        }
        opts.from_row = p.Value();
    }
    if (HasFlag(args, "to-row")) {
        auto p = ParseIntInRange(GetFlag(args, "to-row"), 0, 1000000, "--to-row");
        if (p.IsErr()) {
            fmt.PrintError(p.Error());
            return 99;
        }
        opts.to_row = p.Value();
    }

    auto result = BwGetReportingMetadata(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& row : result.Value()) {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : row.fields) r[k] = v;
            j.push_back(std::move(r));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Records: " << result.Value().size() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw qprops
// ---------------------------------------------------------------------------
int HandleBwQueryProperties(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto result = BwGetQueryProperties(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& row : result.Value()) {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : row.fields) r[k] = v;
            j.push_back(std::move(r));
        }
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Records: " << result.Value().size() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read-trfn
// ---------------------------------------------------------------------------
int HandleBwReadTrfn(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-trfn <name> [--version=a|m|d]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto name = args.positional[0];
    auto version = GetFlag(args, "version", "a");

    // Resolve content type from discovery (best-effort)
    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), "TRFN");
        }
    }

    auto result = BwReadTransformation(*session, name, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = detail.name;
        j["description"] = detail.description;
        j["start_routine"] = detail.start_routine;
        j["end_routine"] = detail.end_routine;
        j["expert_routine"] = detail.expert_routine;
        j["hana_runtime"] = detail.hana_runtime;
        j["source_name"] = detail.source_name;
        j["source_type"] = detail.source_type;
        j["target_name"] = detail.target_name;
        j["target_type"] = detail.target_type;

        nlohmann::json sf = nlohmann::json::array();
        for (const auto& f : detail.source_fields) {
            sf.push_back({{"name", f.name}, {"type", f.type},
                          {"aggregation", f.aggregation}, {"key", f.key}});
        }
        j["source_fields"] = sf;

        nlohmann::json tf = nlohmann::json::array();
        for (const auto& f : detail.target_fields) {
            tf.push_back({{"name", f.name}, {"type", f.type},
                          {"aggregation", f.aggregation}, {"key", f.key}});
        }
        j["target_fields"] = tf;

        nlohmann::json rules = nlohmann::json::array();
        for (const auto& r : detail.rules) {
            rules.push_back({{"source_field", r.source_field},
                             {"target_field", r.target_field},
                             {"source_fields", r.source_fields},
                             {"target_fields", r.target_fields},
                             {"group_id", r.group_id},
                             {"group_description", r.group_description},
                             {"group_type", r.group_type},
                             {"rule_type", r.rule_type},
                             {"formula", r.formula},
                             {"constant", r.constant},
                             {"step_attributes", r.step_attributes}});
        }
        j["rules"] = rules;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Transformation: " << detail.name << "\n";
        if (!detail.description.empty())
            std::cout << "  Description: " << detail.description << "\n";
        std::cout << "  Source: " << detail.source_type << " " << detail.source_name << "\n";
        std::cout << "  Target: " << detail.target_type << " " << detail.target_name << "\n";
        if (!detail.rules.empty()) {
            std::cout << "\n  Rules (" << detail.rules.size() << "):\n";
            for (const auto& r : detail.rules) {
                std::cout << "    " << r.source_field << " -> " << r.target_field
                          << " [" << r.rule_type << "]\n";
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read-adso
// ---------------------------------------------------------------------------
int HandleBwReadAdso(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-adso <name> [--version=a|m|d]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto name = args.positional[0];
    auto version = GetFlag(args, "version", "a");

    // Resolve content type from discovery (best-effort)
    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), "ADSO");
        }
    }

    auto result = BwReadAdsoDetail(*session, name, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = detail.name;
        j["description"] = detail.description;
        j["package"] = detail.package_name;

        nlohmann::json fields = nlohmann::json::array();
        for (const auto& f : detail.fields) {
            fields.push_back({{"name", f.name},
                              {"description", f.description},
                              {"info_object", f.info_object},
                              {"data_type", f.data_type},
                              {"length", f.length},
                              {"decimals", f.decimals},
                              {"key", f.key}});
        }
        j["fields"] = fields;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "ADSO: " << detail.name << "\n";
        if (!detail.description.empty())
            std::cout << "  Description: " << detail.description << "\n";
        if (!detail.package_name.empty())
            std::cout << "  Package: " << detail.package_name << "\n";
        if (!detail.fields.empty()) {
            std::cout << "\n";
            std::vector<std::string> headers = {"Field", "Type", "Length", "Key", "InfoObject"};
            std::vector<std::vector<std::string>> rows;
            for (const auto& f : detail.fields) {
                rows.push_back({f.name, f.data_type,
                                f.length > 0 ? std::to_string(f.length) : "",
                                f.key ? "X" : "",
                                f.info_object});
            }
            fmt.PrintTable(headers, rows);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read-dtp
// ---------------------------------------------------------------------------
int HandleBwReadDtp(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-dtp <name> [--version=a|m|d]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    auto name = args.positional[0];
    auto version = GetFlag(args, "version", "a");

    // Resolve content type from discovery (best-effort)
    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), "DTPA");
        }
    }

    auto result = BwReadDtpDetail(*session, name, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = detail.name;
        j["description"] = detail.description;
        j["type"] = detail.type;
        j["source_name"] = detail.source_name;
        j["source_type"] = detail.source_type;
        j["target_name"] = detail.target_name;
        j["target_type"] = detail.target_type;
        j["source_system"] = detail.source_system;
        j["request_selection_mode"] = detail.request_selection_mode;
        j["extraction_settings"] = detail.extraction_settings;
        j["execution_settings"] = detail.execution_settings;
        j["runtime_properties"] = detail.runtime_properties;
        j["error_handling"] = detail.error_handling;
        j["dtp_execution"] = detail.dtp_execution;
        j["semantic_group_fields"] = detail.semantic_group_fields;
        nlohmann::json filter_fields = nlohmann::json::array();
        for (const auto& f : detail.filter_fields) {
            nlohmann::json selections = nlohmann::json::array();
            for (const auto& s : f.selections) {
                selections.push_back({{"low", s.low},
                                      {"high", s.high},
                                      {"op", s.op},
                                      {"excluding", s.excluding}});
            }
            filter_fields.push_back({{"name", f.name},
                                     {"field", f.field},
                                     {"selected", f.selected},
                                     {"filter_selection", f.filter_selection},
                                     {"selection_type", f.selection_type},
                                     {"selections", selections}});
        }
        j["filter_fields"] = std::move(filter_fields);

        nlohmann::json program_flow = nlohmann::json::array();
        for (const auto& p : detail.program_flow) {
            program_flow.push_back({{"id", p.id},
                                    {"type", p.type},
                                    {"name", p.name},
                                    {"next", p.next}});
        }
        j["program_flow"] = std::move(program_flow);
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "DTP: " << detail.name << "\n";
        if (!detail.description.empty())
            std::cout << "  Description: " << detail.description << "\n";
        std::cout << "  Source: " << detail.source_type << " " << detail.source_name << "\n";
        std::cout << "  Target: " << detail.target_type << " " << detail.target_name << "\n";
        if (!detail.source_system.empty())
            std::cout << "  Source System: " << detail.source_system << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read-rsds
// ---------------------------------------------------------------------------
int HandleBwReadRsds(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty() || !HasFlag(args, "source-system")) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-rsds <name> --source-system=<logsys> "
            "[--version=a|m|d]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    const auto name = args.positional[0];
    const auto source_system = GetFlag(args, "source-system");
    const auto version = GetFlag(args, "version", "a");

    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), "RSDS");
        }
    }

    auto result = BwReadRsdsDetail(*session, name, source_system, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = detail.name;
        j["source_system"] = detail.source_system;
        j["description"] = detail.description;
        j["package"] = detail.package_name;
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& f : detail.fields) {
            fields.push_back({{"segment_id", f.segment_id},
                              {"name", f.name},
                              {"description", f.description},
                              {"data_type", f.data_type},
                              {"length", f.length},
                              {"decimals", f.decimals},
                              {"key", f.key}});
        }
        j["fields"] = fields;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "RSDS: " << detail.name << "\n";
        std::cout << "  Source System: " << detail.source_system << "\n";
        if (!detail.description.empty()) {
            std::cout << "  Description: " << detail.description << "\n";
        }
        if (!detail.package_name.empty()) {
            std::cout << "  Package: " << detail.package_name << "\n";
        }
        std::cout << "  Fields: " << detail.fields.size() << "\n";
    }

    return 0;
}

// ---------------------------------------------------------------------------
// bw read-query
// ---------------------------------------------------------------------------
int HandleBwReadQuery(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-query <name> [--version=a|m|d] [--format=mermaid|table] "
            "[--layout=compact|detailed] [--direction=TD|LR]\n"
            "       erpl-adt bw read-query <query|variable|rkf|ckf|filter|structure> <name> [--version=a|m|d] "
            "[--format=mermaid|table] [--layout=compact|detailed] [--direction=TD|LR]"));
        return 99;
    }

    std::string component_type = "query";
    std::string name;
    if (args.positional.size() == 1) {
        name = args.positional[0];
    } else if (args.positional.size() == 2) {
        component_type = args.positional[0];
        name = args.positional[1];
    } else {
        fmt.PrintError(MakeValidationError(
            "Too many arguments. Usage: erpl-adt bw read-query <name> [--version=a|m|d] [--format=mermaid|table] "
            "[--layout=compact|detailed] [--direction=TD|LR]"));
        return 99;
    }

    const auto version = GetFlag(args, "version", "a");
    const auto format = GetFlag(args, "format", "mermaid");
    const auto layout = GetFlag(args, "layout", "detailed");
    const auto direction = GetFlag(args, "direction", "TD");
    const auto focus_role = GetFlag(args, "focus-role", "");
    const auto max_nodes_per_role = GetFlag(args, "max-nodes-per-role", "");
    const auto json_shape = GetFlag(args, "json-shape", "legacy");
    const auto upstream_mode = GetFlag(args, "upstream", "explicit");
    const auto upstream_dtp = GetFlag(args, "upstream-dtp", "");
    const auto upstream_max_xref = GetFlag(args, "upstream-max-xref", "100");
    const auto lineage_max_steps = GetFlag(args, "lineage-max-steps", "4");
    const bool upstream_no_xref = HasFlag(args, "upstream-no-xref");
    const bool lineage_strict = HasFlag(args, "lineage-strict");
    const bool lineage_explain = HasFlag(args, "lineage-explain");
    const auto component_type_lc = ToLowerCopy(component_type);
    static const std::set<std::string> kAllowedComponentTypes = {
        "query", "variable", "rkf", "ckf", "filter", "structure"};
    if (kAllowedComponentTypes.count(component_type_lc) == 0) {
        fmt.PrintError(MakeValidationError(
            "Unsupported query component type: " + component_type +
            ". Allowed: query, variable, rkf, ckf, filter, structure"));
        return 99;
    }
    static const std::set<std::string> kAllowedVersions = {"a", "m", "d"};
    if (kAllowedVersions.count(ToLowerCopy(version)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --version: " + version + ". Allowed: a, m, d"));
        return 99;
    }
    static const std::set<std::string> kAllowedFormats = {"mermaid", "table"};
    if (kAllowedFormats.count(ToLowerCopy(format)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --format: " + format + ". Allowed: mermaid, table"));
        return 99;
    }
    static const std::set<std::string> kAllowedLayouts = {"compact", "detailed"};
    if (kAllowedLayouts.count(ToLowerCopy(layout)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --layout: " + layout + ". Allowed: compact, detailed"));
        return 99;
    }
    static const std::set<std::string> kAllowedDirections = {"td", "lr"};
    if (kAllowedDirections.count(ToLowerCopy(direction)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --direction: " + direction + ". Allowed: TD, LR"));
        return 99;
    }
    static const std::set<std::string> kAllowedJsonShapes = {"legacy", "catalog", "truth"};
    if (kAllowedJsonShapes.count(ToLowerCopy(json_shape)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --json-shape: " + json_shape + ". Allowed: legacy, catalog, truth"));
        return 99;
    }
    static const std::set<std::string> kAllowedUpstreamModes = {"explicit", "auto"};
    if (kAllowedUpstreamModes.count(ToLowerCopy(upstream_mode)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --upstream: " + upstream_mode + ". Allowed: explicit, auto"));
        return 99;
    }
    if (!upstream_dtp.empty() && component_type_lc != "query") {
        fmt.PrintError(MakeValidationError(
            "--upstream-dtp is only supported for query components."));
        return 99;
    }
    if (ToLowerCopy(upstream_mode) == "auto" && component_type_lc != "query") {
        fmt.PrintError(MakeValidationError(
            "--upstream=auto is only supported for query components."));
        return 99;
    }
    int upstream_max_xref_value = 100;
    try {
        upstream_max_xref_value = std::stoi(upstream_max_xref);
    } catch (const std::exception&) {
        fmt.PrintError(MakeValidationError(
            "Invalid --upstream-max-xref: " + upstream_max_xref +
            ". Must be a positive integer."));
        return 99;
    }
    if (upstream_max_xref_value <= 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --upstream-max-xref: " + upstream_max_xref +
            ". Must be a positive integer."));
        return 99;
    }
    int lineage_max_steps_value = 4;
    try {
        lineage_max_steps_value = std::stoi(lineage_max_steps);
    } catch (const std::exception&) {
        fmt.PrintError(MakeValidationError(
            "Invalid --lineage-max-steps: " + lineage_max_steps +
            ". Must be a positive integer."));
        return 99;
    }
    if (lineage_max_steps_value <= 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --lineage-max-steps: " + lineage_max_steps +
            ". Must be a positive integer."));
        return 99;
    }
    static const std::set<std::string> kAllowedRoles = {
        "rows", "columns", "free", "filter", "member", "subcomponent", "component"};
    if (!focus_role.empty() && kAllowedRoles.count(ToLowerCopy(focus_role)) == 0) {
        fmt.PrintError(MakeValidationError(
            "Invalid --focus-role: " + focus_role +
            ". Allowed: rows, columns, free, filter, member, subcomponent, component"));
        return 99;
    }
    size_t max_nodes_per_role_value = 0;
    if (!max_nodes_per_role.empty()) {
        try {
            max_nodes_per_role_value = static_cast<size_t>(std::stoul(max_nodes_per_role));
        } catch (const std::exception&) {
            fmt.PrintError(MakeValidationError(
                "Invalid --max-nodes-per-role: " + max_nodes_per_role +
                ". Must be a positive integer."));
            return 99;
        }
        if (max_nodes_per_role_value == 0) {
            fmt.PrintError(MakeValidationError(
                "Invalid --max-nodes-per-role: 0. Must be a positive integer."));
            return 99;
        }
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), component_type);
        }
    }

    auto result = BwReadQueryComponent(*session, component_type, name, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    auto graph = BwBuildQueryGraph(detail);
    if (component_type_lc == "query") {
        auto assembled = BwAssembleQueryGraph(*session, detail, version);
        if (assembled.IsErr()) {
            fmt.PrintError(assembled.Error());
            return assembled.Error().ExitCode();
        }
        graph = assembled.Value();
    }
    nlohmann::json upstream_resolution = {
        {"mode", ToLowerCopy(upstream_mode)},
        {"selected_dtp", upstream_dtp},
        {"ambiguous", false},
        {"complete", true},
        {"steps", 0},
        {"strategy_version", "2"},
        {"candidates", nlohmann::json::array()},
        {"composed_candidates", nlohmann::json::array()},
        {"warnings", nlohmann::json::array()},
    };
    std::string resolved_upstream_dtp = upstream_dtp;
    std::vector<std::string> auto_upstream_candidates;
    if (component_type_lc == "query" && ToLowerCopy(upstream_mode) == "auto" &&
        resolved_upstream_dtp.empty()) {
        BwUpstreamLineagePlannerOptions plan_options;
        plan_options.max_steps = lineage_max_steps_value;
        auto plan_result = BwPlanQueryUpstreamLineage(*session, detail, plan_options);
        if (plan_result.IsErr()) {
            if (lineage_strict) {
                fmt.PrintError(plan_result.Error());
                return plan_result.Error().ExitCode();
            }
            graph.warnings.push_back("Auto upstream planning failed: " +
                                     plan_result.Error().message);
            upstream_resolution["warnings"].push_back("planner error: " +
                                                      plan_result.Error().message);
        } else {
            const auto& plan = plan_result.Value();
            upstream_resolution["mode"] = plan.mode;
            upstream_resolution["ambiguous"] = plan.ambiguous;
            upstream_resolution["complete"] = plan.complete;
            upstream_resolution["steps"] = plan.steps;
            if (plan.selected_dtp.has_value()) {
                resolved_upstream_dtp = *plan.selected_dtp;
                upstream_resolution["selected_dtp"] = *plan.selected_dtp;
            }
            for (const auto& candidate : plan.candidates) {
                auto_upstream_candidates.push_back(candidate.object_name);
                upstream_resolution["candidates"].push_back({
                    {"object_name", candidate.object_name},
                    {"object_type", candidate.object_type},
                    {"object_version", candidate.object_version},
                    {"object_status", candidate.object_status},
                    {"uri", candidate.uri},
                    {"evidence", candidate.evidence},
                });
            }
            for (const auto& warning : plan.warnings) {
                graph.warnings.push_back(warning);
                upstream_resolution["warnings"].push_back(warning);
            }
            if (lineage_strict &&
                (!plan.selected_dtp.has_value() || plan.ambiguous || !plan.complete)) {
                fmt.PrintError(MakeValidationError(
                    "Strict upstream resolution failed: ambiguous, incomplete, or missing DTP candidate"));
                return 99;
            }
        }
    }

    if (!resolved_upstream_dtp.empty()) {
        BwLineageGraphOptions options;
        options.dtp_name = resolved_upstream_dtp;
        options.version = version;
        options.include_xref = !upstream_no_xref;
        options.max_xref = upstream_max_xref_value;
        auto lineage_result = BwBuildLineageGraph(*session, options);
        if (lineage_result.IsErr()) {
            graph.warnings.push_back(
                "Failed to compose upstream lineage for DTP " + resolved_upstream_dtp + ": " +
                lineage_result.Error().message);
            upstream_resolution["warnings"].push_back(
                "compose error: " + lineage_result.Error().message);
            if (lineage_strict) {
                fmt.PrintError(lineage_result.Error());
                return lineage_result.Error().ExitCode();
            }
        } else {
            graph = BwMergeQueryAndLineageGraphs(graph, detail, lineage_result.Value());
        }
    } else if (component_type_lc == "query" && ToLowerCopy(upstream_mode) == "auto" &&
               !auto_upstream_candidates.empty() && !lineage_strict) {
        nlohmann::json composed = nlohmann::json::array();
        for (const auto& candidate_dtp : auto_upstream_candidates) {
            BwLineageGraphOptions options;
            options.dtp_name = candidate_dtp;
            options.version = version;
            options.include_xref = !upstream_no_xref;
            options.max_xref = upstream_max_xref_value;
            auto lineage_result = BwBuildLineageGraph(*session, options);
            if (lineage_result.IsErr()) {
                graph.warnings.push_back(
                    "Failed to compose ambiguous upstream candidate " + candidate_dtp + ": " +
                    lineage_result.Error().message);
                upstream_resolution["warnings"].push_back(
                    "compose candidate error (" + candidate_dtp + "): " +
                    lineage_result.Error().message);
                continue;
            }
            graph = BwMergeQueryAndLineageGraphs(graph, detail, lineage_result.Value());
            composed.push_back(candidate_dtp);
        }
        upstream_resolution["composed_candidates"] = composed;
    }
    BwQueryGraphReduceOptions reduction_options;
    if (!focus_role.empty()) {
        reduction_options.focus_role = ToLowerCopy(focus_role);
    }
    reduction_options.max_nodes_per_role = max_nodes_per_role_value;
    auto reduced_graph = BwReduceQueryGraph(graph, reduction_options);
    graph = std::move(reduced_graph.first);
    const auto& reduction = reduced_graph.second;
    const auto metrics = BwAnalyzeQueryGraph(graph);

    if (fmt.IsJsonMode()) {
        if (ToLowerCopy(json_shape) == "truth") {
            nlohmann::json j;
            j["schema_version"] = "3.0";
            j["contract"] = "bw.query.lineage.truth";
            j["root"] = {
                {"component_type", detail.component_type},
                {"component_name", detail.name},
                {"node_id", graph.root_node_id},
            };
            j["resolution"] = upstream_resolution;
            j["candidate_roots"] = upstream_resolution["candidates"];
            j["ambiguities"] = nlohmann::json::array();
            if (upstream_resolution.value("ambiguous", false)) {
                j["ambiguities"].push_back({
                    {"kind", "multiple_upstream_candidates"},
                    {"candidate_count", upstream_resolution["candidates"].size()},
                });
            }
            j["warnings"] = graph.warnings;
            j["provenance"] = graph.provenance;
            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& n : graph.nodes) {
                nodes.push_back({
                    {"node_id", n.id},
                    {"type", n.type},
                    {"name", n.name},
                    {"role", n.role},
                    {"label", n.label},
                    {"attributes", n.attributes},
                });
            }
            j["nodes"] = std::move(nodes);
            nlohmann::json edges = nlohmann::json::array();
            for (const auto& e : graph.edges) {
                edges.push_back({
                    {"edge_id", e.id},
                    {"from_node_id", e.from},
                    {"to_node_id", e.to},
                    {"edge_type", e.type},
                    {"role", e.role},
                    {"evidence", e.attributes},
                });
            }
            j["edges"] = std::move(edges);
            fmt.PrintJson(j.dump());
            return 0;
        }
        if (ToLowerCopy(json_shape) == "catalog") {
            nlohmann::json j;
            j["schema_version"] = "2.0";
            j["contract"] = "bw.query.catalog";
            j["root_component_type"] = detail.component_type;
            j["root_component_name"] = detail.name;
            j["root_node_id"] = graph.root_node_id;
            j["provenance"] = graph.provenance;
            j["warnings"] = graph.warnings;

            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& n : graph.nodes) {
                const auto business_key = n.type + ":" + n.name;
                const bool is_summary = n.type == "SUMMARY";
                nodes.push_back({
                    {"node_id", n.id},
                    {"business_key", business_key},
                    {"object_type", n.type},
                    {"object_name", n.name},
                    {"role", n.role},
                    {"label", n.label},
                    {"is_summary", is_summary},
                    {"source_component_type", detail.component_type},
                    {"source_component_name", detail.name},
                    {"attributes", n.attributes},
                });
            }
            j["nodes"] = std::move(nodes);

            nlohmann::json edges = nlohmann::json::array();
            for (const auto& e : graph.edges) {
                std::string from_business_key;
                std::string to_business_key;
                for (const auto& n : graph.nodes) {
                    if (n.id == e.from) from_business_key = n.type + ":" + n.name;
                    if (n.id == e.to) to_business_key = n.type + ":" + n.name;
                }
                edges.push_back({
                    {"edge_id", e.id},
                    {"from_node_id", e.from},
                    {"to_node_id", e.to},
                    {"from_business_key", from_business_key},
                    {"to_business_key", to_business_key},
                    {"edge_type", e.type},
                    {"role", e.role},
                    {"attributes", e.attributes},
                    {"source_component_type", detail.component_type},
                    {"source_component_name", detail.name},
                });
            }
            j["edges"] = std::move(edges);
            j["reduction"] = {
                {"applied", reduction.applied},
                {"focus_role", reduction.focus_role.has_value() ? *reduction.focus_role : ""},
                {"max_nodes_per_role", reduction.max_nodes_per_role}
            };
            nlohmann::json reduction_summaries = nlohmann::json::array();
            for (const auto& summary : reduction.summaries) {
                reduction_summaries.push_back({
                    {"summary_node_id", summary.summary_node_id},
                    {"role", summary.role},
                    {"omitted_node_ids", summary.omitted_node_ids},
                    {"kept_node_ids", summary.kept_node_ids},
                });
            }
            j["reduction"]["summaries"] = std::move(reduction_summaries);
            j["metrics"] = {
                {"node_count", metrics.node_count},
                {"edge_count", metrics.edge_count},
                {"max_out_degree", metrics.max_out_degree},
                {"summary_node_count", metrics.summary_node_count},
                {"high_fanout_node_ids", metrics.high_fanout_node_ids},
                {"ergonomics_flags", metrics.ergonomics_flags},
            };
            j["upstream_resolution"] = upstream_resolution;
            fmt.PrintJson(j.dump());
            return 0;
        }

        nlohmann::json j;
        j["schema_version"] = graph.schema_version;
        j["root_node_id"] = graph.root_node_id;
        j["metadata"] = {
            {"name", detail.name},
            {"component_type", detail.component_type},
            {"description", detail.description},
            {"info_provider", detail.info_provider},
            {"info_provider_type", detail.info_provider_type},
            {"attributes", detail.attributes}
        };
        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& n : graph.nodes) {
            nodes.push_back({{"id", n.id},
                             {"type", n.type},
                             {"name", n.name},
                             {"role", n.role},
                             {"label", n.label},
                             {"attributes", n.attributes}});
        }
        j["nodes"] = std::move(nodes);
        nlohmann::json edges = nlohmann::json::array();
        for (const auto& e : graph.edges) {
            edges.push_back({{"id", e.id},
                             {"from", e.from},
                             {"to", e.to},
                             {"type", e.type},
                             {"role", e.role},
                             {"attributes", e.attributes}});
        }
        j["edges"] = std::move(edges);
        j["warnings"] = graph.warnings;
        j["provenance"] = graph.provenance;
        j["reduction"] = {
            {"applied", reduction.applied},
            {"focus_role", reduction.focus_role.has_value() ? *reduction.focus_role : ""},
            {"max_nodes_per_role", reduction.max_nodes_per_role}
        };
        nlohmann::json reduction_summaries = nlohmann::json::array();
        for (const auto& summary : reduction.summaries) {
            reduction_summaries.push_back({
                {"summary_node_id", summary.summary_node_id},
                {"role", summary.role},
                {"omitted_node_ids", summary.omitted_node_ids},
                {"kept_node_ids", summary.kept_node_ids},
            });
        }
        j["reduction"]["summaries"] = std::move(reduction_summaries);
        j["metrics"] = {
            {"node_count", metrics.node_count},
            {"edge_count", metrics.edge_count},
            {"max_out_degree", metrics.max_out_degree},
            {"summary_node_count", metrics.summary_node_count},
            {"high_fanout_node_ids", metrics.high_fanout_node_ids},
            {"ergonomics_flags", metrics.ergonomics_flags},
        };
        j["upstream_resolution"] = upstream_resolution;

        // Backward-compatible fields retained during contract transition.
        j["name"] = detail.name;
        j["component_type"] = detail.component_type;
        j["description"] = detail.description;
        j["info_provider"] = detail.info_provider;
        j["info_provider_type"] = detail.info_provider_type;
        j["attributes"] = detail.attributes;
        nlohmann::json refs = nlohmann::json::array();
        for (const auto& r : detail.references) {
            refs.push_back({{"name", r.name},
                            {"type", r.type},
                            {"role", r.role},
                            {"attributes", r.attributes}});
        }
        j["references"] = std::move(refs);
        fmt.PrintJson(j.dump());
    } else {
        if (format == "table") {
            std::cout << detail.component_type << ": " << detail.name << "\n";
            if (!detail.description.empty()) std::cout << "  Description: " << detail.description << "\n";
            if (!detail.info_provider.empty()) {
                std::cout << "  InfoProvider: " << detail.info_provider;
                if (!detail.info_provider_type.empty()) {
                    std::cout << " (" << detail.info_provider_type << ")";
                }
                std::cout << "\n";
            }
            std::cout << "  References: " << detail.references.size() << "\n";
        } else {
            BwQueryMermaidOptions mermaid_options;
            mermaid_options.layout = layout;
            mermaid_options.direction = direction;
            if (lineage_explain && !upstream_resolution["warnings"].empty()) {
                std::cerr << "[lineage] upstream warnings:\n";
                for (const auto& warning : upstream_resolution["warnings"]) {
                    std::cerr << "  - " << warning.get<std::string>() << "\n";
                }
            }
            if (lineage_explain) {
                std::cerr << "[lineage] mode=" << upstream_resolution["mode"] << " complete="
                          << (upstream_resolution["complete"].get<bool>() ? "true" : "false")
                          << " ambiguous="
                          << (upstream_resolution["ambiguous"].get<bool>() ? "true" : "false")
                          << " steps=" << upstream_resolution["steps"] << "\n";
                if (!upstream_resolution["selected_dtp"].get<std::string>().empty()) {
                    std::cerr << "[lineage] selected_dtp="
                              << upstream_resolution["selected_dtp"].get<std::string>() << "\n";
                }
            }
            std::cout << BwRenderQueryGraphMermaid(graph, mermaid_options);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw read-dmod
// ---------------------------------------------------------------------------
int HandleBwReadDmod(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw read-dmod <name> [--version=a|m|d]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    const auto name = args.positional[0];
    const auto version = GetFlag(args, "version", "a");
    std::string resolved_ct;
    {
        auto disc = BwDiscover(*session);
        if (disc.IsOk()) {
            resolved_ct = BwResolveContentType(disc.Value(), "DMOD");
        }
    }

    auto result = BwReadDataFlow(*session, name, version, resolved_ct);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& detail = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["name"] = detail.name;
        j["description"] = detail.description;
        j["attributes"] = detail.attributes;
        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& n : detail.nodes) {
            nodes.push_back({{"id", n.id},
                             {"name", n.name},
                             {"type", n.type},
                             {"attributes", n.attributes}});
        }
        j["nodes"] = std::move(nodes);
        nlohmann::json connections = nlohmann::json::array();
        for (const auto& c : detail.connections) {
            connections.push_back({{"from", c.from},
                                   {"to", c.to},
                                   {"type", c.type},
                                   {"attributes", c.attributes}});
        }
        j["connections"] = std::move(connections);
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "DMOD: " << detail.name << "\n";
        if (!detail.description.empty()) std::cout << "  Description: " << detail.description << "\n";
        std::cout << "  Nodes: " << detail.nodes.size() << "\n";
        std::cout << "  Connections: " << detail.connections.size() << "\n";
    }

    return 0;
}

// ---------------------------------------------------------------------------
// bw lineage
// ---------------------------------------------------------------------------
int HandleBwLineage(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw lineage <dtp_name> [--trfn=<name>] "
            "[--version=a|m|d] [--max-xref=<n>] [--no-xref]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwLineageGraphOptions opts;
    opts.dtp_name = args.positional[0];
    opts.version = GetFlag(args, "version", "a");
    if (HasFlag(args, "trfn")) {
        opts.trfn_name = GetFlag(args, "trfn");
    }
    opts.include_xref = !HasFlag(args, "no-xref");
    if (HasFlag(args, "max-xref")) {
        auto parsed = ParseIntInRange(GetFlag(args, "max-xref"), 1, 10000,
                                      "--max-xref");
        if (parsed.IsErr()) {
            fmt.PrintError(parsed.Error());
            return 99;
        }
        opts.max_xref = parsed.Value();
    }

    auto result = BwBuildLineageGraph(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& graph = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j;
        j["schema_version"] = graph.schema_version;
        j["root"] = {{"type", graph.root_type}, {"name", graph.root_name}};

        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& n : graph.nodes) {
            nlohmann::json nj{
                {"id", n.id},
                {"type", n.type},
                {"name", n.name},
                {"role", n.role},
            };
            if (!n.uri.empty()) nj["uri"] = n.uri;
            if (!n.version.empty()) nj["version"] = n.version;
            if (!n.attributes.empty()) nj["attributes"] = n.attributes;
            nodes.push_back(std::move(nj));
        }
        j["nodes"] = std::move(nodes);

        nlohmann::json edges = nlohmann::json::array();
        for (const auto& e : graph.edges) {
            nlohmann::json ej{
                {"id", e.id},
                {"from", e.from},
                {"to", e.to},
                {"type", e.type},
            };
            if (!e.attributes.empty()) ej["attributes"] = e.attributes;
            edges.push_back(std::move(ej));
        }
        j["edges"] = std::move(edges);

        nlohmann::json prov = nlohmann::json::array();
        for (const auto& p : graph.provenance) {
            prov.push_back({{"operation", p.operation},
                            {"endpoint", p.endpoint},
                            {"status", p.status}});
        }
        j["provenance"] = std::move(prov);
        j["warnings"] = graph.warnings;
        fmt.PrintJson(j.dump());
    } else {
        std::cout << "Lineage graph for DTP " << graph.root_name << "\n";
        std::cout << "  Nodes: " << graph.nodes.size() << "\n";
        std::cout << "  Edges: " << graph.edges.size() << "\n";
        if (!graph.warnings.empty()) {
            std::cout << "  Warnings: " << graph.warnings.size() << "\n";
            for (const auto& w : graph.warnings) {
                std::cout << "    - " << w << "\n";
            }
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Shared output rendering for all three bw export-* commands.
// ---------------------------------------------------------------------------
static int RenderBwExport(const CommandArgs& args,
                           const BwInfoareaExport& exp,
                           const std::string& object_name) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    bool mermaid_mode = HasFlag(args, "mermaid");
    std::string shape = GetFlag(args, "shape", "catalog");
    std::string service_name = GetFlag(args, "service-name", "erpl_adt");
    std::string system_id = GetFlag(args, "system-id", "");

    BwMermaidOptions mopts;
    mopts.iobj_edges = HasFlag(args, "iobj-edges");

    std::string catalog_json = BwRenderExportCatalogJson(exp);
    std::string mermaid_str = mermaid_mode ? BwRenderExportMermaid(exp, mopts) : "";
    std::string om_json;
    if (shape == "openmetadata") {
        om_json = BwRenderExportOpenMetadataJson(exp, service_name, system_id);
    }

    if (HasFlag(args, "out-dir")) {
        auto out_dir = GetFlag(args, "out-dir");
        std::string catalog_path = out_dir + "/" + object_name + "_catalog.json";
        std::string mmd_path = out_dir + "/" + object_name + "_dataflow.mmd";
        std::ofstream cf(catalog_path);
        if (!cf) {
            fmt.PrintError(MakeValidationError("Cannot write to: " + catalog_path));
            return 99;
        }
        cf << catalog_json;
        cf.close();
        std::ofstream mf(mmd_path);
        if (!mf) {
            fmt.PrintError(MakeValidationError("Cannot write to: " + mmd_path));
            return 99;
        }
        mf << BwRenderExportMermaid(exp, mopts);
        mf.close();
        if (!fmt.IsJsonMode()) {
            std::cout << "Exported " << exp.objects.size() << " objects from "
                      << object_name << "\n";
            std::cout << "  Catalog JSON:  " << catalog_path << "\n";
            std::cout << "  Mermaid:       " << mmd_path << "\n";
            if (!exp.warnings.empty()) {
                std::cout << "  Warnings: " << exp.warnings.size() << "\n";
            }
        }
        if (fmt.IsJsonMode()) fmt.PrintJson(catalog_json);
        return 0;
    }

    const bool editor_mode = HasFlag(args, "editor");

    if (mermaid_mode) {
        const auto mmd = BwRenderExportMermaid(exp, mopts);
        if (editor_mode) {
            auto tmp = MakeTempPath(".mmd");
            std::ofstream tf(tmp);
            tf << mmd;
            tf.close();
            LaunchEditor(tmp);
            std::remove(tmp.c_str());
        } else {
            std::cout << mmd;
        }
    } else if (shape == "openmetadata") {
        if (editor_mode) {
            auto tmp = MakeTempPath(".json");
            std::ofstream tf(tmp);
            tf << om_json;
            tf.close();
            LaunchEditor(tmp);
            std::remove(tmp.c_str());
        } else {
            fmt.PrintJson(om_json);
        }
    } else {
        if (fmt.IsJsonMode()) {
            if (editor_mode) {
                auto tmp = MakeTempPath(".json");
                std::ofstream tf(tmp);
                tf << catalog_json;
                tf.close();
                LaunchEditor(tmp);
                std::remove(tmp.c_str());
            } else {
                fmt.PrintJson(catalog_json);
            }
        } else {
            std::cout << "Object:   " << object_name << "\n";
            std::cout << "Objects:  " << exp.objects.size() << "\n";
            std::cout << "Dataflow nodes: " << exp.dataflow_nodes.size() << "\n";
            std::cout << "Dataflow edges: " << exp.dataflow_edges.size() << "\n";
            if (!exp.warnings.empty()) {
                std::cout << "Warnings: " << exp.warnings.size() << "\n";
                for (const auto& w : exp.warnings) std::cout << "  - " << w << "\n";
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw export-area — enumerate and export an entire infoarea
// ---------------------------------------------------------------------------
int HandleBwExport(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw export <infoarea> [--mermaid] [--shape catalog|openmetadata] "
            "[--max-depth N] [--types T1,T2,...] [--no-lineage] [--no-queries] [--no-search] "
            "[--version a|m] [--no-elem-edges] [--iobj-edges] [--out-dir <dir>] [--service-name <name>] [--system-id <id>]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    BwExportOptions opts;
    opts.infoarea_name = args.positional[0];
    opts.version = GetFlag(args, "version", "a");
    opts.include_lineage = !HasFlag(args, "no-lineage");
    opts.include_queries = !HasFlag(args, "no-queries");
    opts.include_search_supplement = !HasFlag(args, "no-search");
    opts.include_xref_edges = !HasFlag(args, "no-xref-edges");
    opts.include_elem_provider_edges = !HasFlag(args, "no-elem-edges");

    if (HasFlag(args, "max-depth")) {
        auto parsed = ParseIntInRange(GetFlag(args, "max-depth"), 0, 100, "--max-depth");
        if (parsed.IsErr()) {
            fmt.PrintError(parsed.Error());
            return 99;
        }
        opts.max_depth = parsed.Value();
    }

    if (HasFlag(args, "types")) {
        auto types_str = GetFlag(args, "types");
        std::istringstream ss(types_str);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) opts.types_filter.push_back(tok);
        }
    }

    auto result = BwExportInfoarea(*session, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    return RenderBwExport(args, result.Value(), opts.infoarea_name);
}

// ---------------------------------------------------------------------------
// bw export-query — export a single BW query and its connected graph
// ---------------------------------------------------------------------------
int HandleBwExportQuery(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw export-query <query-name> [--mermaid] [--shape catalog|openmetadata] "
            "[--no-lineage] [--no-queries] [--version a|m] [--no-elem-edges] [--iobj-edges] "
            "[--out-dir <dir>] [--service-name <name>] [--system-id <id>]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    const std::string name = args.positional[0];
    BwExportOptions opts;
    opts.version = GetFlag(args, "version", "a");
    opts.include_lineage = !HasFlag(args, "no-lineage");
    opts.include_queries = !HasFlag(args, "no-queries");
    opts.include_xref_edges = !HasFlag(args, "no-xref-edges");
    opts.include_elem_provider_edges = !HasFlag(args, "no-elem-edges");

    auto result = BwExportQuery(*session, name, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    return RenderBwExport(args, result.Value(), name);
}

// ---------------------------------------------------------------------------
// bw export-cube — export a single BW infoprovider and its connected graph
// ---------------------------------------------------------------------------
int HandleBwExportCube(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw export-cube <cube-name> [--mermaid] [--shape catalog|openmetadata] "
            "[--no-lineage] [--version a|m] [--no-elem-edges] [--iobj-edges] "
            "[--out-dir <dir>] [--service-name <name>] [--system-id <id>]"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    const std::string name = args.positional[0];
    BwExportOptions opts;
    opts.version = GetFlag(args, "version", "a");
    opts.include_lineage = !HasFlag(args, "no-lineage");
    opts.include_xref_edges = !HasFlag(args, "no-xref-edges");
    opts.include_elem_provider_edges = !HasFlag(args, "no-elem-edges");

    auto result = BwExportCube(*session, name, opts);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }
    return RenderBwExport(args, result.Value(), name);
}

// ---------------------------------------------------------------------------
// bw transport (sub-actions: check, write, list, collect)
// ---------------------------------------------------------------------------
int HandleBwTransport(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw transport <check|write|list|collect> [args]"));
        return 99;
    }

    auto sub_action = args.positional[0];

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (sub_action == "check" || sub_action == "list") {
        BwTransportCheckOptions options;
        options.own_only = HasFlag(args, "own-only");
        options.read_properties = HasFlag(args, "rdprops");
        options.all_messages = HasFlag(args, "allmsgs");
        if (HasFlag(args, "rddetails")) {
            options.read_details = GetFlag(args, "rddetails");
        }

        auto result = BwTransportCheck(*session, options);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }

        const auto& tr = result.Value();
        if (fmt.IsJsonMode()) {
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

            if (sub_action == "check") {
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
                j["objects"] = objs;

                nlohmann::json chgs = nlohmann::json::array();
                for (const auto& c : tr.changeability) {
                    chgs.push_back({{"tlogo", c.tlogo},
                                    {"transportable", c.transportable},
                                    {"changeable", c.changeable}});
                }
                j["changeability"] = chgs;
            }
            fmt.PrintJson(j.dump());
        } else {
            if (sub_action == "list") {
                std::vector<std::string> headers = {"Number", "Type", "Status", "Description"};
                std::vector<std::vector<std::string>> rows;
                for (const auto& r : tr.requests) {
                    rows.push_back({r.number, r.function_type, r.status, r.description});
                }
                fmt.PrintTable(headers, rows);
            } else {
                std::cout << "Writing enabled: "
                          << (tr.writing_enabled ? "yes" : "no") << "\n\n";
                if (!tr.objects.empty()) {
                    std::cout << "Objects:\n";
                    for (const auto& o : tr.objects) {
                        std::cout << "  " << o.type << " " << o.name
                                  << " (" << o.operation << ")\n";
                    }
                }
                if (!tr.requests.empty()) {
                    std::cout << "\nTransport Requests:\n";
                    for (const auto& r : tr.requests) {
                        std::cout << "  " << r.number << " " << r.description
                                  << " [" << r.status << "]\n";
                    }
                }
                if (!tr.messages.empty()) {
                    std::cout << "\nMessages:\n";
                    for (const auto& msg : tr.messages) {
                        std::cout << "  " << msg << "\n";
                    }
                }
            }
        }
        return 0;
    }

    if (sub_action == "write") {
        if (args.positional.size() < 3) {
            fmt.PrintError(MakeValidationError(
                "Usage: erpl-adt bw transport write <type> <name> --transport=..."));
            return 99;
        }

        BwTransportWriteOptions opts;
        opts.object_type = args.positional[1];
        opts.object_name = args.positional[2];
        opts.transport = GetFlag(args, "transport");
        opts.package_name = GetFlag(args, "package");
        opts.simulate = HasFlag(args, "simulate");
        opts.all_messages = HasFlag(args, "allmsgs");
        opts.context_headers = ParseBwContextHeaders(args);

        if (opts.transport.empty()) {
            fmt.PrintError(MakeValidationError("Missing --transport flag"));
            return 99;
        }

        auto result = BwTransportWrite(*session, opts);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }

        const auto& wr = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["success"] = wr.success;
            j["messages"] = wr.messages;
            fmt.PrintJson(j.dump());
        } else {
            fmt.PrintSuccess("Written to transport: " + opts.transport);
            for (const auto& m : wr.messages) {
                std::cout << "  " << m << "\n";
            }
        }
        return 0;
    }

    if (sub_action == "collect") {
        if (args.positional.size() < 3) {
            fmt.PrintError(MakeValidationError(
                "Usage: erpl-adt bw transport collect <type> <name> [--mode=000]"));
            return 99;
        }

        BwTransportCollectOptions opts;
        opts.object_type = args.positional[1];
        opts.object_name = args.positional[2];
        if (HasFlag(args, "mode")) {
            opts.mode = GetFlag(args, "mode");
        }
        if (HasFlag(args, "transport")) {
            opts.transport = GetFlag(args, "transport");
        }
        opts.context_headers = ParseBwContextHeaders(args);

        auto result = BwTransportCollect(*session, opts);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }

        const auto& cr = result.Value();
        if (fmt.IsJsonMode()) {
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
            fmt.PrintJson(j.dump());
        } else {
            if (!cr.details.empty()) {
                std::cout << "Collected Objects:\n";
                for (const auto& d : cr.details) {
                    std::cout << "  " << d.type << " " << d.name
                              << " [" << d.status << "] " << d.description << "\n";
                }
            }
            if (!cr.dependencies.empty()) {
                std::cout << "\nDependencies:\n";
                for (const auto& d : cr.dependencies) {
                    std::cout << "  " << d.type << " " << d.name
                              << " -> " << d.associated_type << " " << d.associated_name
                              << "\n";
                }
            }
            for (const auto& m : cr.messages) {
                std::cout << "  " << m << "\n";
            }
        }
        return 0;
    }

    fmt.PrintError(MakeValidationError(
        "Unknown transport action: " + sub_action + ". Use check, write, list, or collect."));
    return 99;
}

// ---------------------------------------------------------------------------
// bw locks (sub-actions: list, delete)
// ---------------------------------------------------------------------------
int HandleBwLocks(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw locks <list|delete> [flags]"));
        return 99;
    }

    auto sub_action = args.positional[0];

    if (sub_action == "list") {
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        BwListLocksOptions opts;
        if (HasFlag(args, "user")) {
            opts.user = GetFlag(args, "user");
        }
        if (HasFlag(args, "search")) {
            opts.search = GetFlag(args, "search");
        }
        if (HasFlag(args, "max")) {
            auto max_result = ParseIntInRange(
                GetFlag(args, "max"), 1,
                std::numeric_limits<int>::max(), "--max");
            if (max_result.IsErr()) {
                fmt.PrintError(max_result.Error());
                return 99;
            }
            opts.max_results = max_result.Value();
        }

        auto result = BwListLocks(*session, opts);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }

        const auto& locks = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& l : locks) {
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
            fmt.PrintJson(j.dump());
        } else {
            std::vector<std::string> headers = {"User", "Object", "Mode", "Table", "Timestamp"};
            std::vector<std::vector<std::string>> rows;
            for (const auto& l : locks) {
                rows.push_back({l.user, l.object, l.mode, l.table_name, l.timestamp});
            }
            fmt.PrintTable(headers, rows);
        }
        return 0;
    }

    if (sub_action == "delete") {
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        BwDeleteLockOptions opts;
        if (!HasFlag(args, "user")) {
            fmt.PrintError(MakeValidationError("--user is required for lock delete"));
            return 99;
        }
        opts.user = GetFlag(args, "user");

        if (!HasFlag(args, "table-name")) {
            fmt.PrintError(MakeValidationError(
                "--table-name is required (from bw locks list output)"));
            return 99;
        }
        opts.table_name = GetFlag(args, "table-name");

        if (!HasFlag(args, "arg")) {
            fmt.PrintError(MakeValidationError(
                "--arg is required (base64 arg from bw locks list output)"));
            return 99;
        }
        opts.arg = GetFlag(args, "arg");
        opts.lock_mode = HasFlag(args, "mode") ? GetFlag(args, "mode") : "E";
        opts.scope = HasFlag(args, "scope") ? GetFlag(args, "scope") : "1";
        if (HasFlag(args, "owner1")) opts.owner1 = GetFlag(args, "owner1");
        if (HasFlag(args, "owner2")) opts.owner2 = GetFlag(args, "owner2");

        auto result = BwDeleteLock(*session, opts);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        fmt.PrintSuccess("Lock deleted for user " + opts.user);
        return 0;
    }

    fmt.PrintError(MakeValidationError(
        "Unknown locks action: " + sub_action + ". Use list or delete."));
    return 99;
}

// ---------------------------------------------------------------------------
// bw dbinfo
// ---------------------------------------------------------------------------
int HandleBwDbInfo(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (HasFlag(args, "raw")) {
        HttpHeaders headers;
        headers["Accept"] = "application/atom+xml";
        auto resp = session->Get("/sap/bw/modeling/repo/is/dbinfo", headers);
        if (resp.IsErr()) {
            fmt.PrintError(resp.Error());
            return resp.Error().ExitCode();
        }
        if (resp.Value().status_code != 200) {
            auto error = Error::FromHttpStatus("BwGetDbInfo",
                "/sap/bw/modeling/repo/is/dbinfo",
                resp.Value().status_code, resp.Value().body);
            fmt.PrintError(error);
            return error.ExitCode();
        }
        std::cout << resp.Value().body;
        return 0;
    }

    auto result = BwGetDbInfo(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& info = result.Value();
    if (fmt.IsJsonMode()) {
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
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Property", "Value"};
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Host", info.host});
        rows.push_back({"Port", info.port});
        rows.push_back({"Schema", info.schema});
        rows.push_back({"Database Type", info.database_type});
        if (!info.database_name.empty()) rows.push_back({"Database Name", info.database_name});
        if (!info.instance.empty()) rows.push_back({"Instance", info.instance});
        if (!info.user.empty()) rows.push_back({"User", info.user});
        if (!info.version.empty()) rows.push_back({"Version", info.version});
        if (!info.patchlevel.empty()) rows.push_back({"Patchlevel", info.patchlevel});
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw sysinfo
// ---------------------------------------------------------------------------
int HandleBwSysInfo(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (HasFlag(args, "raw")) {
        HttpHeaders headers;
        headers["Accept"] = "application/atom+xml";
        auto resp = session->Get("/sap/bw/modeling/repo/is/systeminfo", headers);
        if (resp.IsErr()) {
            fmt.PrintError(resp.Error());
            return resp.Error().ExitCode();
        }
        if (resp.Value().status_code != 200) {
            auto error = Error::FromHttpStatus("BwGetSystemInfo",
                "/sap/bw/modeling/repo/is/systeminfo",
                resp.Value().status_code, resp.Value().body);
            fmt.PrintError(error);
            return error.ExitCode();
        }
        std::cout << resp.Value().body;
        return 0;
    }

    auto result = BwGetSystemInfo(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& props = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& p : props) {
            j.push_back({{"key", p.key},
                         {"value", p.value},
                         {"description", p.description}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Key", "Value", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : props) {
            rows.push_back({p.key, p.value, p.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw changeability
// ---------------------------------------------------------------------------
int HandleBwChangeability(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (HasFlag(args, "raw")) {
        HttpHeaders headers;
        headers["Accept"] = "application/atom+xml";
        auto resp = session->Get("/sap/bw/modeling/repo/is/chginfo", headers);
        if (resp.IsErr()) {
            fmt.PrintError(resp.Error());
            return resp.Error().ExitCode();
        }
        if (resp.Value().status_code != 200) {
            auto error = Error::FromHttpStatus("BwGetChangeability",
                "/sap/bw/modeling/repo/is/chginfo",
                resp.Value().status_code, resp.Value().body);
            fmt.PrintError(error);
            return error.ExitCode();
        }
        std::cout << resp.Value().body;
        return 0;
    }

    auto result = BwGetChangeability(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& entries = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries) {
            j.push_back({{"object_type", e.object_type},
                         {"changeable", e.changeable},
                         {"transportable", e.transportable},
                         {"description", e.description}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"Type", "Changeable", "Transportable", "Description"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            rows.push_back({e.object_type, e.changeable, e.transportable, e.description});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw adturi
// ---------------------------------------------------------------------------
int HandleBwAdtUri(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));
    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    if (HasFlag(args, "raw")) {
        HttpHeaders headers;
        headers["Accept"] = "application/atom+xml";
        auto resp = session->Get("/sap/bw/modeling/repo/is/adturi", headers);
        if (resp.IsErr()) {
            fmt.PrintError(resp.Error());
            return resp.Error().ExitCode();
        }
        if (resp.Value().status_code != 200) {
            auto error = Error::FromHttpStatus("BwGetAdtUriMappings",
                "/sap/bw/modeling/repo/is/adturi",
                resp.Value().status_code, resp.Value().body);
            fmt.PrintError(error);
            return error.ExitCode();
        }
        std::cout << resp.Value().body;
        return 0;
    }

    auto result = BwGetAdtUriMappings(*session);
    if (result.IsErr()) {
        fmt.PrintError(result.Error());
        return result.Error().ExitCode();
    }

    const auto& mappings = result.Value();
    if (fmt.IsJsonMode()) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& m : mappings) {
            j.push_back({{"bw_type", m.bw_type},
                         {"adt_type", m.adt_type},
                         {"bw_uri_template", m.bw_uri_template},
                         {"adt_uri_template", m.adt_uri_template}});
        }
        fmt.PrintJson(j.dump());
    } else {
        std::vector<std::string> headers = {"BW Type", "ADT Type", "BW URI", "ADT URI"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& m : mappings) {
            rows.push_back({m.bw_type, m.adt_type, m.bw_uri_template, m.adt_uri_template});
        }
        fmt.PrintTable(headers, rows);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bw job (sub-actions: list, result, status, progress, steps, step, messages,
// cancel, restart, cleanup)
// ---------------------------------------------------------------------------
int HandleBwJob(const CommandArgs& args) {
    OutputFormatter fmt(JsonMode(args), ColorMode(args));

    if (args.positional.empty()) {
        fmt.PrintError(MakeValidationError(
            "Usage: erpl-adt bw job <list|result|status|progress|steps|step|messages|cancel|restart|cleanup> [args]"));
        return 99;
    }

    auto sub_action = args.positional[0];

    if (sub_action == "list") {
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        auto result = BwListJobs(*session);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& jobs = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& job : jobs) {
                j.push_back({{"guid", job.guid},
                             {"status", job.status},
                             {"job_type", job.job_type},
                             {"description", job.description}});
            }
            fmt.PrintJson(j.dump());
        } else {
            std::vector<std::string> headers = {"GUID", "Status", "Type", "Description"};
            std::vector<std::vector<std::string>> rows;
            for (const auto& job : jobs) {
                rows.push_back({job.guid, job.status, job.job_type, job.description});
            }
            fmt.PrintTable(headers, rows);
        }
        return 0;
    }

    if (sub_action == "result") {
        if (args.positional.size() < 2) {
            fmt.PrintError(MakeValidationError("Missing job GUID"));
            return 99;
        }
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        auto result = BwGetJobResult(*session, args.positional[1]);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& job = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["guid"] = job.guid;
            j["status"] = job.status;
            j["job_type"] = job.job_type;
            j["description"] = job.description;
            fmt.PrintJson(j.dump());
        } else {
            std::cout << "Job " << job.guid << ": " << job.status << "\n";
            if (!job.job_type.empty()) {
                std::cout << "  Type: " << job.job_type << "\n";
            }
            if (!job.description.empty()) {
                std::cout << "  Description: " << job.description << "\n";
            }
        }
        return 0;
    }

    if (sub_action == "cancel") {
        if (args.positional.size() < 2) {
            fmt.PrintError(MakeValidationError("Missing job GUID"));
            return 99;
        }
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        auto result = BwCancelJob(*session, args.positional[1]);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        fmt.PrintSuccess("Job cancelled: " + args.positional[1]);
        return 0;
    }

    if (sub_action == "restart") {
        if (args.positional.size() < 2) {
            fmt.PrintError(MakeValidationError("Missing job GUID"));
            return 99;
        }
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        auto result = BwRestartJob(*session, args.positional[1]);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        fmt.PrintSuccess("Job restarted: " + args.positional[1]);
        return 0;
    }

    if (sub_action == "cleanup") {
        if (args.positional.size() < 2) {
            fmt.PrintError(MakeValidationError("Missing job GUID"));
            return 99;
        }
        auto session = RequireSession(args, fmt);
        if (!session) return 99;

        auto result = BwCleanupJob(*session, args.positional[1]);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        fmt.PrintSuccess("Job cleanup complete: " + args.positional[1]);
        return 0;
    }

    if (args.positional.size() < 2) {
        fmt.PrintError(MakeValidationError("Missing job GUID"));
        return 99;
    }

    auto session = RequireSession(args, fmt);
    if (!session) return 99;

    const auto& guid = args.positional[1];

    if (sub_action == "status") {
        auto result = BwGetJobStatus(*session, guid);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& st = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["guid"] = st.guid;
            j["status"] = st.status;
            j["job_type"] = st.job_type;
            j["description"] = st.description;
            fmt.PrintJson(j.dump());
        } else {
            std::cout << "Job " << st.guid << ": " << st.status << "\n";
            if (!st.job_type.empty())
                std::cout << "  Type: " << st.job_type << "\n";
            if (!st.description.empty())
                std::cout << "  Description: " << st.description << "\n";
        }
        return 0;
    }

    if (sub_action == "progress") {
        auto result = BwGetJobProgress(*session, guid);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& pr = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["guid"] = pr.guid;
            j["percentage"] = pr.percentage;
            j["status"] = pr.status;
            j["description"] = pr.description;
            fmt.PrintJson(j.dump());
        } else {
            std::cout << "Job " << pr.guid << ": " << pr.percentage << "%\n";
            if (!pr.description.empty())
                std::cout << "  " << pr.description << "\n";
        }
        return 0;
    }

    if (sub_action == "steps") {
        auto result = BwGetJobSteps(*session, guid);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& steps = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& s : steps) {
                j.push_back({{"name", s.name},
                             {"status", s.status},
                             {"description", s.description}});
            }
            fmt.PrintJson(j.dump());
        } else {
            std::vector<std::string> headers = {"Name", "Status", "Description"};
            std::vector<std::vector<std::string>> rows;
            for (const auto& s : steps) {
                rows.push_back({s.name, s.status, s.description});
            }
            fmt.PrintTable(headers, rows);
        }
        return 0;
    }

    if (sub_action == "step") {
        if (args.positional.size() < 3) {
            fmt.PrintError(MakeValidationError("Usage: erpl-adt bw job step <guid> <step>"));
            return 99;
        }
        auto result = BwGetJobStep(*session, guid, args.positional[2]);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& step = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j;
            j["name"] = step.name;
            j["status"] = step.status;
            j["description"] = step.description;
            fmt.PrintJson(j.dump());
        } else {
            std::cout << step.name << " [" << step.status << "] " << step.description << "\n";
        }
        return 0;
    }

    if (sub_action == "messages") {
        auto result = BwGetJobMessages(*session, guid);
        if (result.IsErr()) {
            fmt.PrintError(result.Error());
            return result.Error().ExitCode();
        }
        const auto& msgs = result.Value();
        if (fmt.IsJsonMode()) {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& m : msgs) {
                j.push_back({{"severity", m.severity},
                             {"text", m.text},
                             {"object_name", m.object_name}});
            }
            fmt.PrintJson(j.dump());
        } else {
            for (const auto& m : msgs) {
                std::cout << "[" << m.severity << "] " << m.text << "\n";
            }
        }
        return 0;
    }

    fmt.PrintError(MakeValidationError(
        "Unknown job action: " + sub_action +
        ". Use list, result, status, progress, steps, step, messages, cancel, restart, or cleanup."));
    return 99;
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
        "search", "object", "source", "activate", "test", "check",
        "transport", "ddic", "package", "discover", "bw"};

    // Group display names and short descriptions (overrides for cleaner display).
    struct GroupMeta {
        std::string label;
        std::string short_desc;  // Empty means use router's GroupDescription.
    };
    const std::map<std::string, GroupMeta> group_meta = {
        {"search",    {"SEARCH", ""}},
        {"object",    {"OBJECT", ""}},
        {"source",    {"SOURCE", ""}},
        {"activate",  {"ACTIVATE", ""}},
        {"test",      {"TEST", ""}},
        {"check",     {"CHECK", ""}},
        {"transport", {"TRANSPORT", ""}},
        {"ddic",      {"DATA DICTIONARY", "Tables and CDS views"}},
        {"package",   {"PACKAGE", ""}},
        {"discover",  {"DISCOVER", ""}},
        {"bw",        {"BW", "SAP BW/4HANA Modeling operations"}},
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
           arg == "--json" || arg == "--https" || arg == "--insecure" ||
           arg == "--help" || arg == "--activate" || arg == "--raw" ||
           arg == "--datasource" || arg == "--search-desc" ||
           arg == "--own-only" || arg == "--simulate" ||
           arg == "--validate" || arg == "--background" || arg == "--force" ||
           arg == "--sort" || arg == "--only-ina" || arg == "--exec-check" ||
           arg == "--with-cto" || arg == "--rdprops" || arg == "--allmsgs" ||
           arg == "--dbgmode" || arg == "--metadata-only" ||
           arg == "--incl-metadata" || arg == "--incl-object-values" ||
           arg == "--incl-except-def" || arg == "--compact-mode" ||
           arg == "--no-xref" || arg == "--no-search" || arg == "--no-elem-edges" ||
           arg == "--iobj-edges" || arg == "--editor";
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
// IsBwHelpRequest
// ---------------------------------------------------------------------------
bool IsBwHelpRequest(int argc, const char* const* argv) {
    bool found_bw = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        // Skip short verbosity flags.
        if (arg == "-v" || arg == "-vv") continue;
        // Track help flags but keep scanning for positionals.
        if (arg == "--help" || arg == "-h") continue;
        // Skip boolean flags.
        if (IsBooleanFlag(arg)) continue;
        // Skip other flags (and their values).
        if (arg.substr(0, 2) == "--") {
            auto eq = arg.find('=');
            if (eq == std::string_view::npos &&
                i + 1 < argc &&
                std::string_view{argv[i + 1]}.substr(0, 2) != "--") {
                ++i; // skip the value
            }
            continue;
        }
        // Positional argument.
        if (!found_bw) {
            if (arg != "bw") return false;
            found_bw = true;
            continue;
        }
        // Second positional after "bw" — "help" means group help, anything else
        // is a real action (let the router handle it).
        return arg == "help";
    }

    // "bw" alone or "bw" + only flags (including --help) → show group help.
    return found_bw;
}

// ---------------------------------------------------------------------------
// PrintBwGroupHelp
// ---------------------------------------------------------------------------
void PrintBwGroupHelp(const CommandRouter& router, std::ostream& out, bool color) {
    Ansi a{out, color};

    // Title + tagline.
    a.Bold("erpl-adt bw").Normal(" - SAP BW/4HANA Modeling operations").Nl().Nl();
    a.Dim("  Search, read, and manage BW/4HANA modeling objects (ADSO, TRFN, DTP, IOBJ, ...).").Nl();
    a.Dim("  All commands accept --json for machine-readable output.").Nl();

    // Usage.
    out << "\n";
    a.Bold("USAGE").Nl();
    out << "  erpl-adt bw <action> [args] [flags]\n";

    // Build a lookup from action name to command info.
    auto cmds = router.CommandsForGroup("bw");
    std::map<std::string, CommandInfo> cmd_map;
    for (auto& cmd : cmds) {
        cmd_map[cmd.action] = cmd;
    }

    // Sub-categories.
    struct Category {
        const char* label;
        std::vector<std::string> actions;
    };
    const std::vector<Category> categories = {
        {"SEARCH & READ",    {"search", "read", "read-adso", "read-trfn", "read-dtp", "read-rsds", "read-query", "read-dmod", "lineage", "export-area", "export-query", "export-cube", "discover"}},
        {"CROSS-REFERENCES", {"xref", "nodes", "nodepath"}},
        {"REPOSITORY",       {"search-md", "favorites", "applog", "message"}},
        {"LIFECYCLE",        {"create", "lock", "unlock", "save", "delete", "activate"}},
        {"TRANSPORT",        {"transport"}},
        {"VALIDATION",       {"validate", "move"}},
        {"ADVANCED",         {"valuehelp", "virtualfolders", "datavolumes", "reporting", "qprops"}},
        {"JOBS",             {"job"}},
        {"SYSTEM",           {"sysinfo", "changeability", "dbinfo", "adturi"}},
        {"LOCKS",            {"locks"}},
    };

    // Pre-compute display entries and max width.
    size_t max_left = 0;
    std::vector<std::vector<CmdDisplay>> cat_entries(categories.size());

    for (size_t c = 0; c < categories.size(); ++c) {
        for (const auto& action : categories[c].actions) {
            auto it = cmd_map.find(action);
            if (it == cmd_map.end()) continue;

            CmdDisplay d;
            // Build left column from usage string.
            std::string command_part;
            if (it->second.help.has_value() && !it->second.help->usage.empty()) {
                auto usage = it->second.help->usage;
                auto prefix = std::string("erpl-adt bw ");
                if (usage.substr(0, prefix.size()) == prefix) {
                    auto rest = usage.substr(prefix.size());
                    // Take until [flags], newline, or --
                    auto bracket = rest.find('[');
                    auto nl = rest.find('\n');
                    auto dash = rest.find("--");
                    auto end_pos = std::min({bracket, nl, dash});
                    if (end_pos != std::string::npos) {
                        command_part = rest.substr(0, end_pos);
                    } else {
                        command_part = rest;
                    }
                    // Trim trailing whitespace.
                    while (!command_part.empty() && command_part.back() == ' ')
                        command_part.pop_back();
                } else {
                    command_part = action;
                }
            } else {
                command_part = action;
            }

            d.left = "  " + command_part;
            d.desc = it->second.description;
            if (it->second.help.has_value()) {
                d.flags = it->second.help->flags;
            }
            max_left = std::max(max_left, d.left.size());
            // Also account for flag widths for alignment.
            for (const auto& f : d.flags) {
                std::string flag_line = "      --" + f.name;
                if (!f.placeholder.empty()) flag_line += " " + f.placeholder;
                max_left = std::max(max_left, flag_line.size());
            }
            cat_entries[c].push_back(std::move(d));
        }
    }

    max_left = std::max(max_left, static_cast<size_t>(42));
    if (max_left > 52) max_left = 52;

    // Print each category.
    for (size_t c = 0; c < categories.size(); ++c) {
        out << "\n";
        a.Bold(categories[c].label).Nl();

        for (const auto& d : cat_entries[c]) {
            size_t pad = (max_left > d.left.size()) ? (max_left - d.left.size()) : 2;
            out << d.left << std::string(pad, ' ') << d.desc << "\n";

            for (const auto& f : d.flags) {
                std::string flag_line = "      --" + f.name;
                if (!f.placeholder.empty()) flag_line += " " + f.placeholder;
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

    // Examples.
    auto examples = router.GroupExamples("bw");
    if (!examples.empty()) {
        out << "\n";
        a.Bold("EXAMPLES").Nl();
        for (const auto& ex : examples) {
            a.Dim("  " + ex).Nl();
        }
    }

    // Shorthand note.
    out << "\n";
    a.Dim("  Shorthand: 'search' is the default action, so 'erpl-adt bw <args>'").Nl();
    a.Dim("  is equivalent to 'erpl-adt bw search <args>'.").Nl();

    out << "\n";
    a.Dim("  Use \"erpl-adt bw <action> --help\" for details on a specific action.").Nl();
}

// ---------------------------------------------------------------------------
// RegisterAllCommands
// ---------------------------------------------------------------------------
void RegisterAllCommands(CommandRouter& router) {
    // -----------------------------------------------------------------------
    // Group descriptions and examples
    // -----------------------------------------------------------------------
    router.SetGroupDescription("activate", "Activate inactive ABAP objects");
    router.SetGroupExamples("activate", {
        "$ erpl-adt activate ZCL_MY_CLASS",
        "$ erpl-adt --json activate /sap/bc/adt/oo/classes/zcl_my_class",
    });

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
        "# Read source by class name (no URI required)",
        "$ erpl-adt source read ZCL_MY_CLASS",
        "",
        "# Read all class source sections (main + local types)",
        "$ erpl-adt source read ZCL_MY_CLASS --section all",
        "",
        "# Read active source by full URI",
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
        "$ erpl-adt test ZCL_MY_TEST_CLASS",
        "$ erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST",
        "$ erpl-adt --json test ZCL_TEST",
    });

    router.SetGroupDescription("check", "Run ATC quality checks");
    router.SetGroupExamples("check", {
        "$ erpl-adt check ZCL_MY_CLASS",
        "$ erpl-adt check run /sap/bc/adt/packages/ZTEST --variant=FUNCTIONAL_DB_ADDITION",
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
    // activate run (default action — "erpl-adt activate <name>" works)
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt activate <name-or-uri> [flags]";
        help.args_description = "<name-or-uri>    Object name (e.g. ZCL_TEST) or full ADT URI";
        help.long_description = "Activates inactive ABAP objects. Accepts an object name or URI. "
            "Names are resolved via search. Exit code 5 indicates activation failure.";
        help.examples = {
            "erpl-adt activate ZCL_MY_CLASS",
            "erpl-adt activate /sap/bc/adt/oo/classes/zcl_my_class",
            "erpl-adt --json activate ZCL_MY_CLASS",
        };
        router.Register("activate", "run", "Activate an ABAP object",
                         HandleActivateRun, std::move(help));
        router.SetDefaultAction("activate", "run");
    }

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
        help.usage = "erpl-adt source read <name-or-uri> [flags]";
        help.args_description = "<name-or-uri>    Object technical name (e.g. ZCL_MY_CLASS) or full ADT source URI";
        help.flags = {
            {"version", "<version>",  "active or inactive (default: active)",                                        false},
            {"section", "<section>",  "Source section: main (default), localdefinitions, localimplementations, testclasses, all", false},
            {"type",    "<type>",     "Object type to disambiguate name resolution (e.g. CLAS, PROG, INTF)",          false},
            {"color",   "",           "Force ANSI syntax highlighting even when piped",                               false},
            {"no-color","",           "Disable ANSI syntax highlighting",                                             false},
            {"editor",  "",           "Open source in $VISUAL/$EDITOR (plain text, no ANSI codes)",                  false},
        };
        help.examples = {
            "erpl-adt source read ZCL_MY_CLASS",
            "erpl-adt source read /DMO/CL_FLIGHT_LEGACY --section localimplementations",
            "erpl-adt source read /DMO/CL_FLIGHT_LEGACY --section all",
            "erpl-adt source read /DMO/CL_FLIGHT_LEGACY --type CLAS",
            "erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main",
            "erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main --version=inactive",
            "erpl-adt source read ZCL_MY_CLASS --editor",
            "erpl-adt source read ZCL_MY_CLASS --color | less -R",
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
        help.long_description = "Without --handle, the object is automatically locked, written, and unlocked. "
            "Use --activate to activate the object after writing.";
        help.flags = {
            {"file", "<path>", "Path to local source file", true},
            {"handle", "<handle>", "Lock handle (skips auto-lock if provided)", false},
            {"transport", "<id>", "Transport request number", false},
            {"session-file", "<path>", "Session file for stateful workflow", false},
            {"activate", "", "Activate the object after writing", false},
        };
        help.examples = {
            "erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main --file=source.abap",
            "erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main --file=source.abap --activate",
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
    // test run (default action — "erpl-adt test <name>" works)
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt test <name-or-uri> [flags]";
        help.args_description = "<name-or-uri>    Object name (e.g. ZCL_TEST) or full ADT URI";
        help.long_description = "Accepts an object name or URI. Names are resolved via search. "
            "Exit code 7 indicates test failures.";
        help.examples = {
            "erpl-adt test ZCL_MY_TEST_CLASS",
            "erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST",
            "erpl-adt --json test ZCL_TEST",
        };
        router.Register("test", "run", "Run ABAP unit tests",
                         HandleTestRun, std::move(help));
        router.SetDefaultAction("test", "run");
    }

    // -----------------------------------------------------------------------
    // check run (default action — "erpl-adt check <name>" works)
    // -----------------------------------------------------------------------
    {
        CommandHelp help;
        help.usage = "erpl-adt check <name-or-uri> [flags]";
        help.args_description = "<name-or-uri>    Object name (e.g. ZCL_TEST) or full ADT URI";
        help.long_description = "Accepts an object name or URI. Names are resolved via search. "
            "Exit code 8 indicates ATC errors.";
        help.flags = {
            {"variant", "<name>", "ATC variant (default: DEFAULT)", false},
        };
        help.examples = {
            "erpl-adt check ZCL_MY_CLASS",
            "erpl-adt check run /sap/bc/adt/packages/ZTEST",
            "erpl-adt check run /sap/bc/adt/oo/classes/ZCL_TEST --variant=FUNCTIONAL_DB_ADDITION",
        };
        router.Register("check", "run", "Run ATC checks",
                         HandleCheckRun, std::move(help));
        router.SetDefaultAction("check", "run");
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
        help.usage = "erpl-adt discover services [flags]";
        help.long_description = "Lists all ADT REST API services grouped by workspace, with capabilities (abapGit, packages, activation).";
        help.flags = {
            {"workspace", "<name>", "Filter by workspace name (e.g., \"Object Repository\")", false},
        };
        help.examples = {
            "erpl-adt discover services",
            "erpl-adt discover services --workspace=\"Sources\"",
            "erpl-adt --json discover services",
        };
        router.Register("discover", "services", "Discover ADT services",
                         HandleDiscoverServices, std::move(help));
    }

    // -----------------------------------------------------------------------
    // BW commands
    // -----------------------------------------------------------------------
    router.SetGroupDescription("bw", "SAP BW/4HANA Modeling operations");
    router.SetGroupExamples("bw", {
        "$ erpl-adt bw search \"Z*\" --type=ADSO",
        "$ erpl-adt bw ZADSO*                  # shorthand (default action)",
        "$ erpl-adt bw read ADSO ZSALES_DATA",
        "$ erpl-adt bw read-adso ZSALES_DATA   # structured field list",
        "$ erpl-adt bw read-trfn ZTRFN_SALES   # transformation lineage",
        "$ erpl-adt bw read-dtp DTP_ZSALES     # DTP connection details",
        "$ erpl-adt bw read-rsds ZSRC --source-system=ECLCLNT100",
        "$ erpl-adt bw read-query query ZQ_SALES",
        "$ erpl-adt bw read-dmod ZDMOD_SALES",
        "$ erpl-adt bw lineage DTP_ZSALES       # canonical lineage graph",
        "$ erpl-adt bw xref ADSO ZSALES_DATA",
        "$ erpl-adt bw nodes ADSO ZSALES_DATA",
        "$ erpl-adt bw discover",
        "$ erpl-adt --json bw activate ADSO ZSALES_DATA",
    });

    // bw discover
    {
        CommandHelp help;
        help.usage = "erpl-adt bw discover";
        help.long_description = "Lists available BW Modeling services from the "
            "discovery endpoint. Shows scheme/term pairs and URI templates.";
        help.examples = {
            "erpl-adt bw discover",
            "erpl-adt --json bw discover",
        };
        router.Register("bw", "discover", "Discover BW modeling services",
                         HandleBwDiscover, std::move(help));
    }

    // bw search (default action)
    {
        CommandHelp help;
        help.usage = "erpl-adt bw search <pattern> [flags]";
        help.args_description = "<pattern>    Search term with wildcards (e.g., Z*, *SALES*)";
        help.long_description = "Search BW repository for modeling objects by name pattern.";
        help.flags = {
            {"type", "<code>", "Object type (ADSO, HCPR, IOBJ, TRFN, DTPA, RSDS, ...)", false},
            {"subtype", "<code>", "Object subtype (REP, SOB, RKF, ...)", false},
            {"max", "<n>", "Maximum results (default: 100)", false},
            {"status", "<code>", "Object status: ACT, INA, OFF", false},
            {"changed-by", "<user>", "Last changed by filter", false},
            {"changed-from", "<date>", "Changed on or after date", false},
            {"changed-to", "<date>", "Changed on or before date", false},
            {"created-by", "<user>", "Created by filter", false},
            {"created-from", "<date>", "Created on or after date", false},
            {"created-to", "<date>", "Created on or before date", false},
            {"depends-on-name", "<name>", "Filter by dependency object name", false},
            {"depends-on-type", "<type>", "Filter by dependency object type", false},
            {"infoarea", "<name>", "Filter by infoarea assignment (e.g. 0D_NW_DEMO)", false},
            {"search-desc", "", "Also search in descriptions", false},
            {"search-name", "", "Search in names (default: true)", false},
        };
        help.examples = {
            "erpl-adt bw search \"Z*\"",
            "erpl-adt bw search \"SALES*\" --type=ADSO --max=50",
            "erpl-adt bw search \"*\" --changed-by=DEVELOPER --changed-from=2026-01-01",
            "erpl-adt bw search \"*\" --infoarea=0D_NW_DEMO",
            "erpl-adt bw ZADSO*",
        };
        router.Register("bw", "search", "Search BW objects",
                         HandleBwSearch, std::move(help));
        router.SetDefaultAction("bw", "search");
    }

    // bw read
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read <type> <name> [flags]\n"
                     "       erpl-adt bw read --uri <path>";
        help.args_description = "<type>    Object type (ADSO, IOBJ, TRFN, ...)\n"
                                "  <name>    Object name";
        help.long_description = "Read a BW object definition. Use --uri to pass the URI "
            "directly from search results (avoids type-to-path mapping issues).";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
            {"source-system", "<name>", "Source system (required for RSDS, APCO)", false},
            {"uri", "<path>", "Direct URI from search results (overrides type/name path)", false},
            {"raw", "", "Output raw XML", false},
        };
        help.examples = {
            "erpl-adt bw read ADSO ZSALES_DATA",
            "erpl-adt bw read IOBJ 0MATERIAL --version=m",
            "erpl-adt bw read RSDS ZSRC --source-system=ECLCLNT100",
            "erpl-adt bw read --uri /sap/bw/modeling/query/0D_FC_NW_C01_Q0007/a",
            "erpl-adt bw read ELEM NAME --uri /sap/bw/modeling/query/NAME/a",
        };
        router.Register("bw", "read", "Read BW object definition",
                         HandleBwRead, std::move(help));
    }

    // bw read-trfn
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-trfn <name> [--version=a|m|d]";
        help.args_description = "<name>    Transformation name";
        help.long_description = "Read a BW transformation definition with source/target "
            "fields and mapping rules. Provides structured lineage data.";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
        };
        help.examples = {
            "erpl-adt bw read-trfn ZTRFN_SALES",
            "erpl-adt --json bw read-trfn ZTRFN_SALES --version=m",
        };
        router.Register("bw", "read-trfn", "Read BW transformation definition",
                         HandleBwReadTrfn, std::move(help));
    }

    // bw read-adso
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-adso <name> [--version=a|m|d]";
        help.args_description = "<name>    ADSO name";
        help.long_description = "Read a BW ADSO (Advanced DataStore Object) definition "
            "with field list including types, lengths, and key flags.";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
        };
        help.examples = {
            "erpl-adt bw read-adso ZSALES_DATA",
            "erpl-adt --json bw read-adso ZSALES_DATA",
        };
        router.Register("bw", "read-adso", "Read BW ADSO field structure",
                         HandleBwReadAdso, std::move(help));
    }

    // bw read-dtp
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-dtp <name> [--version=a|m|d]";
        help.args_description = "<name>    DTP name";
        help.long_description = "Read a BW DTP (Data Transfer Process) definition showing "
            "source/target connections and source system.";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
        };
        help.examples = {
            "erpl-adt bw read-dtp DTP_ZSALES",
            "erpl-adt --json bw read-dtp DTP_ZSALES",
        };
        router.Register("bw", "read-dtp", "Read BW DTP connection details",
                         HandleBwReadDtp, std::move(help));
    }

    // bw read-rsds
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-rsds <name> --source-system=<logsys> [--version=a|m|d]";
        help.args_description = "<name>    DataSource (RSDS) name";
        help.long_description = "Read BW DataSource (RSDS) with parsed segment/field metadata.";
        help.flags = {
            {"source-system", "<logsys>", "Source system (required, e.g. ECLCLNT100)", false},
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
        };
        help.examples = {
            "erpl-adt bw read-rsds ZSRC_SALES --source-system=ECLCLNT100",
            "erpl-adt --json bw read-rsds ZSRC_SALES --source-system=ECLCLNT100 --version=m",
        };
        router.Register("bw", "read-rsds", "Read BW RSDS field structure",
                         HandleBwReadRsds, std::move(help));
    }

    // bw read-query
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-query <name> [--version=a|m|d] [--format=mermaid|table] [--layout=compact|detailed] [--direction=TD|LR]\n"
                     "       erpl-adt bw read-query <query|variable|rkf|ckf|filter|structure> <name> [--version=a|m|d] [--format=mermaid|table] [--layout=compact|detailed] [--direction=TD|LR]\n"
                     "       erpl-adt bw read-query <name> [--max-nodes-per-role=<n>] [--focus-role=<role>] [--json-shape=legacy|catalog|truth]\n"
                     "       erpl-adt bw read-query query <name> [--upstream=explicit|auto] [--upstream-dtp=<dtp>] [--upstream-no-xref] [--upstream-max-xref=<n>] [--lineage-max-steps=<n>] [--lineage-strict] [--lineage-explain]";
        help.args_description = "<name>               Query component technical name\n"
                                "  <component-type>     Optional explicit type (default: query)";
        help.long_description = "Read BW query-family component definitions with structured references "
            "for query lineage modeling. Non-JSON output defaults to Mermaid graph text.";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
            {"format", "<f>", "Non-JSON output format: mermaid (default) or table", false},
            {"layout", "<l>", "Mermaid layout: detailed (default) or compact", false},
            {"direction", "<d>", "Mermaid direction: TD (default) or LR", false},
            {"max-nodes-per-role", "<n>", "Reduce graph fan-out: keep at most n nodes per role; add summary nodes", false},
            {"focus-role", "<r>", "Limit reduction to a specific role (rows|columns|free|filter|member|subcomponent|component)", false},
            {"json-shape", "<s>", "JSON output shape: legacy (default), catalog (flat), or truth (lineage v3)", false},
            {"upstream", "<m>", "Upstream resolution mode: explicit (default) or auto", false},
            {"upstream-dtp", "<name>", "Compose query graph with upstream BW lineage rooted at DTP", false},
            {"upstream-no-xref", "", "Disable xref expansion for upstream lineage composition", false},
            {"upstream-max-xref", "<n>", "Maximum xref neighbors in upstream lineage composition (default: 100)", false},
            {"lineage-max-steps", "<n>", "Maximum auto-upstream planner expansion steps (default: 4)", false},
            {"lineage-strict", "", "Fail when auto-upstream resolution is ambiguous or incomplete", false},
            {"lineage-explain", "", "Emit upstream resolution warnings/decisions for troubleshooting", false},
        };
        help.examples = {
            "erpl-adt bw read-query 0D_FC_NW_C01_Q0007",
            "erpl-adt bw read-query 0D_FC_NW_C01_Q0007 --layout=detailed --direction=LR",
            "erpl-adt --json bw read-query 0D_FC_NW_C01_Q0007 --max-nodes-per-role=5 --focus-role=filter",
            "erpl-adt --json bw read-query query 0D_FC_NW_C01_Q0007 --json-shape=catalog",
            "erpl-adt --json bw read-query query 0D_FC_NW_C01_Q0007 --json-shape=truth --upstream=auto",
            "erpl-adt --json bw read-query query 0D_FC_NW_C01_Q0007 --upstream=auto --lineage-explain",
            "erpl-adt --json bw read-query query 0D_FC_NW_C01_Q0007 --upstream-dtp=DTP_ZSALES --upstream-no-xref",
            "erpl-adt bw read-query query ZQ_SALES --format=table",
            "erpl-adt --json bw read-query variable ZVAR_FISCYEAR",
            "erpl-adt --json bw read-query rkf ZRKF_MARGIN",
        };
        router.Register("bw", "read-query", "Read BW query-family component",
                         HandleBwReadQuery, std::move(help));
    }

    // bw read-dmod
    {
        CommandHelp help;
        help.usage = "erpl-adt bw read-dmod <name> [--version=a|m|d]";
        help.args_description = "<name>    DataFlow (DMOD) name";
        help.long_description = "Read BW DataFlow topology with nodes and connections.";
        help.flags = {
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
        };
        help.examples = {
            "erpl-adt bw read-dmod ZDMOD_SALES",
            "erpl-adt --json bw read-dmod ZDMOD_SALES --version=m",
        };
        router.Register("bw", "read-dmod", "Read BW DMOD topology",
                         HandleBwReadDmod, std::move(help));
    }

    // bw lineage
    {
        CommandHelp help;
        help.usage =
            "erpl-adt bw lineage <dtp_name> [--trfn=<name>] [--version=a|m|d] "
            "[--max-xref=<n>] [--no-xref]";
        help.args_description = "<dtp_name>    DTP name used as lineage root";
        help.long_description =
            "Build canonical BW lineage graph JSON combining DTP, TRFN field mappings, "
            "and optional XREF relations.";
        help.flags = {
            {"trfn", "<name>", "Optional explicit transformation name", false},
            {"version", "<v>", "Version: a (active, default), m (modified), d (delivery)", false},
            {"max-xref", "<n>", "Maximum xref neighbors to include (default: 100)", false},
            {"no-xref", "", "Disable xref expansion for a strict DTP/TRFN graph", false},
        };
        help.examples = {
            "erpl-adt bw lineage DTP_ZSALES",
            "erpl-adt --json bw lineage DTP_ZSALES --trfn=ZTRFN_SALES --max-xref=20",
            "erpl-adt --json bw lineage DTP_ZSALES --no-xref",
        };
        router.Register("bw", "lineage", "Build canonical BW lineage graph",
                         HandleBwLineage, std::move(help));
    }

    // bw export-area
    {
        CommandHelp help;
        help.usage =
            "erpl-adt bw export-area <infoarea> [--mermaid] [--shape catalog|openmetadata]\n"
            "                        [--max-depth N] [--types T1,T2,...]\n"
            "                        [--no-lineage] [--no-queries] [--no-search] [--version a|m]\n"
            "                        [--out-dir <dir>] [--service-name <name>] [--system-id <id>]";
        help.args_description = "<infoarea>    InfoArea name to export (e.g. 0D_NW_DEMO)";
        help.long_description =
            "Enumerate all objects in a BW InfoArea (ADSOs, RSDs, TRFNs, DTPs, Queries) "
            "and export them as structured JSON or a Mermaid dataflow diagram. "
            "Optionally writes catalog JSON + Mermaid to --out-dir. "
            "DTP lineage is collected per-object and merged into a unified dataflow graph.";
        help.flags = {
            {"mermaid", "", "Output Mermaid dataflow diagram instead of JSON", false},
            {"shape", "<catalog|openmetadata>",
             "JSON output shape: catalog (default) or openmetadata", false},
            {"max-depth", "<N>", "Max recursion depth for nested infoareas (default: 10)", false},
            {"types", "<T1,T2,...>",
             "Comma-separated TLOGO type filter (e.g. ADSO,DTPA). Default: all", false},
            {"no-lineage", "", "Skip DTP lineage graph collection (faster)", false},
            {"no-queries", "", "Skip query graph collection", false},
            {"no-search", "", "Skip search supplement, use BFS tree only (faster)", false},
            {"no-xref-edges", "",
             "Skip xref-based INFOPROVIDER→QUERY edge collection (faster, fewer API calls)",
             false},
            {"no-elem-edges", "",
             "Skip orphan ELEM XML parsing for provider edge recovery (faster, fewer API calls)",
             false},
            {"iobj-edges", "",
             "Show InfoObject nodes (dimensions, filters, variables) in Mermaid diagram",
             false},
            {"version", "<a|m>", "Object version: a (active, default) or m (modified)", false},
            {"out-dir", "<dir>",
             "Save {name}_catalog.json and {name}_dataflow.mmd to directory", false},
            {"service-name", "<name>",
             "Service name for openmetadata FQN (default: erpl_adt)", false},
            {"system-id", "<id>", "System ID for openmetadata FQN prefix", false},
        };
        help.examples = {
            "erpl-adt --json bw export-area 0D_NW_DEMO",
            "erpl-adt bw export-area 0D_NW_DEMO --mermaid",
            "erpl-adt --json bw export-area 0D_NW_DEMO --shape openmetadata --system-id A4H",
            "erpl-adt --json bw export-area 0D_NW_DEMO --types ADSO,DTPA --no-lineage",
            "erpl-adt bw export-area 0D_NW_DEMO --out-dir /tmp/bw_export",
        };
        router.Register("bw", "export-area",
                         "Export all objects in a BW InfoArea to JSON/Mermaid",
                         HandleBwExport, std::move(help));
    }

    // bw export-query
    {
        CommandHelp help;
        help.usage =
            "erpl-adt bw export-query <query-name> [--mermaid] [--shape catalog|openmetadata]\n"
            "                         [--no-lineage] [--no-queries] [--version a|m]\n"
            "                         [--no-elem-edges] [--iobj-edges]\n"
            "                         [--out-dir <dir>] [--service-name <name>] [--system-id <id>]";
        help.args_description = "<query-name>  Technical name of the BW query/ELEM";
        help.long_description =
            "Export a single BW query (ELEM) and its connected graph: info provider, "
            "consuming queries discovered via xref, and InfoObject references "
            "(dimensions, filters, variables, key figures). "
            "Produces the same JSON/Mermaid output format as export-area.";
        help.flags = {
            {"mermaid", "", "Output Mermaid dataflow diagram instead of JSON", false},
            {"shape", "<catalog|openmetadata>",
             "JSON output shape: catalog (default) or openmetadata", false},
            {"no-lineage", "", "Skip DTP lineage graph collection (faster)", false},
            {"no-queries", "", "Skip query graph collection", false},
            {"no-xref-edges", "",
             "Skip xref-based edge collection (faster, fewer API calls)", false},
            {"no-elem-edges", "",
             "Skip orphan ELEM XML parsing for provider edge recovery (faster)", false},
            {"iobj-edges", "",
             "Show InfoObject nodes (dimensions, filters, variables) in Mermaid diagram",
             false},
            {"version", "<a|m>", "Object version: a (active, default) or m (modified)", false},
            {"out-dir", "<dir>",
             "Save {name}_catalog.json and {name}_dataflow.mmd to directory", false},
            {"service-name", "<name>",
             "Service name for openmetadata FQN (default: erpl_adt)", false},
            {"system-id", "<id>", "System ID for openmetadata FQN prefix", false},
        };
        help.examples = {
            "erpl-adt --json bw export-query 0D_FC_NW_C01_Q0001",
            "erpl-adt bw export-query 0D_FC_NW_C01_Q0001 --mermaid --iobj-edges",
            "erpl-adt bw export-query 0D_FC_NW_C01_Q0001 --out-dir /tmp",
        };
        router.Register("bw", "export-query",
                         "Export a single BW query and its connected graph to JSON/Mermaid",
                         HandleBwExportQuery, std::move(help));
    }

    // bw export-cube
    {
        CommandHelp help;
        help.usage =
            "erpl-adt bw export-cube <cube-name> [--mermaid] [--shape catalog|openmetadata]\n"
            "                        [--no-lineage] [--version a|m]\n"
            "                        [--no-elem-edges] [--iobj-edges]\n"
            "                        [--out-dir <dir>] [--service-name <name>] [--system-id <id>]";
        help.args_description = "<cube-name>   Technical name of the infoprovider (ADSO, CUBE, MPRO)";
        help.long_description =
            "Export a single BW infoprovider (ADSO, classic CUBE, MultiProvider) and its "
            "connected graph: consuming queries discovered via xref, DTP lineage, and "
            "InfoObject references on queries. "
            "Produces the same JSON/Mermaid output format as export-area.";
        help.flags = {
            {"mermaid", "", "Output Mermaid dataflow diagram instead of JSON", false},
            {"shape", "<catalog|openmetadata>",
             "JSON output shape: catalog (default) or openmetadata", false},
            {"no-lineage", "", "Skip DTP lineage graph collection (faster)", false},
            {"no-xref-edges", "",
             "Skip xref-based edge collection (faster, fewer API calls)", false},
            {"no-elem-edges", "",
             "Skip orphan ELEM XML parsing for provider edge recovery (faster)", false},
            {"iobj-edges", "",
             "Show InfoObject nodes (dimensions, filters, variables) in Mermaid diagram",
             false},
            {"version", "<a|m>", "Object version: a (active, default) or m (modified)", false},
            {"out-dir", "<dir>",
             "Save {name}_catalog.json and {name}_dataflow.mmd to directory", false},
            {"service-name", "<name>",
             "Service name for openmetadata FQN (default: erpl_adt)", false},
            {"system-id", "<id>", "System ID for openmetadata FQN prefix", false},
        };
        help.examples = {
            "erpl-adt --json bw export-cube 0D_NW_C01",
            "erpl-adt bw export-cube 0D_NW_C01 --mermaid",
            "erpl-adt bw export-cube 0D_NW_C01 --out-dir /tmp",
        };
        router.Register("bw", "export-cube",
                         "Export a single BW infoprovider and its connected graph to JSON/Mermaid",
                         HandleBwExportCube, std::move(help));
    }

    // bw lock
    {
        CommandHelp help;
        help.usage = "erpl-adt bw create <type> <name> [flags]";
        help.args_description = "<type>    Object type\n  <name>    Object name";
        help.long_description = "Create a BW modeling object. Some object types require --file XML content or copy-from flags.";
        help.flags = {
            {"package", "<pkg>", "Target package", false},
            {"copy-from-name", "<name>", "Copy source object name", false},
            {"copy-from-type", "<type>", "Copy source object type", false},
            {"file", "<path>", "Optional XML payload file for create request body", false},
        };
        help.examples = {
            "erpl-adt bw create ADSO ZNEW_ADSO --package=ZPKG",
            "erpl-adt bw create IOBJ ZNEW_IOBJ --copy-from-name=0MATERIAL --copy-from-type=IOBJ",
        };
        router.Register("bw", "create", "Create BW object",
                         HandleBwCreate, std::move(help));
    }

    // bw lock
    {
        CommandHelp help;
        help.usage = "erpl-adt bw lock <type> <name> [flags]";
        help.args_description = "<type>    Object type\n  <name>    Object name";
        help.long_description = "Lock a BW object for editing. Returns lock handle "
            "and transport information.";
        help.flags = {
            {"activity", "<code>", "Activity: CHAN (default), DELE, MAIN", false},
            {"parent-name", "<name>", "Parent object name (lock context)", false},
            {"parent-type", "<type>", "Parent object type (lock context)", false},
            {"transport-lock-holder", "<corrnr>", "Explicit Transport-Lock-Holder header", false},
            {"foreign-objects", "<value>", "Foreign-Objects header", false},
            {"foreign-object-locks", "<value>", "Foreign-Object-Locks header", false},
            {"foreign-correction-number", "<corrnr>", "Foreign-Correction-Number header", false},
            {"foreign-package", "<pkg>", "Foreign-Package header", false},
            {"session-file", "<path>", "Save session state for multi-step workflow", false},
        };
        help.examples = {
            "erpl-adt bw lock ADSO ZSALES_DATA",
            "erpl-adt --json bw lock ADSO ZSALES_DATA --session-file=s.json",
        };
        router.Register("bw", "lock", "Lock BW object for editing",
                         HandleBwLock, std::move(help));
    }

    // bw unlock
    {
        CommandHelp help;
        help.usage = "erpl-adt bw unlock <type> <name>";
        help.args_description = "<type>    Object type\n  <name>    Object name";
        help.long_description = "Release a lock on a BW object.";
        help.examples = {
            "erpl-adt bw unlock ADSO ZSALES_DATA",
        };
        router.Register("bw", "unlock", "Release BW object lock",
                         HandleBwUnlock, std::move(help));
    }

    // bw save
    {
        CommandHelp help;
        help.usage = "erpl-adt bw save <type> <name> [flags]";
        help.args_description = "<type>    Object type\n  <name>    Object name";
        help.long_description = "Save modified BW object XML. Reads content from stdin.";
        help.flags = {
            {"lock-handle", "<handle>", "Lock handle from bw lock", true},
            {"transport", "<corrnr>", "Transport request number", false},
            {"timestamp", "<ts>", "Server timestamp from lock response", false},
            {"transport-lock-holder", "<corrnr>", "Explicit Transport-Lock-Holder header", false},
            {"foreign-objects", "<value>", "Foreign-Objects header", false},
            {"foreign-object-locks", "<value>", "Foreign-Object-Locks header", false},
            {"foreign-correction-number", "<corrnr>", "Foreign-Correction-Number header", false},
            {"foreign-package", "<pkg>", "Foreign-Package header", false},
        };
        help.examples = {
            "erpl-adt bw save ADSO ZSALES --lock-handle=ABC123 < modified.xml",
        };
        router.Register("bw", "save", "Save modified BW object",
                         HandleBwSave, std::move(help));
    }

    // bw delete
    {
        CommandHelp help;
        help.usage = "erpl-adt bw delete <type> <name> [flags]";
        help.args_description = "<type>    Object type\n  <name>    Object name";
        help.long_description = "Delete a BW object.";
        help.flags = {
            {"lock-handle", "<handle>", "Lock handle", true},
            {"transport", "<corrnr>", "Transport request number", false},
            {"transport-lock-holder", "<corrnr>", "Explicit Transport-Lock-Holder header", false},
            {"foreign-objects", "<value>", "Foreign-Objects header", false},
            {"foreign-object-locks", "<value>", "Foreign-Object-Locks header", false},
            {"foreign-correction-number", "<corrnr>", "Foreign-Correction-Number header", false},
            {"foreign-package", "<pkg>", "Foreign-Package header", false},
        };
        help.examples = {
            "erpl-adt bw delete ADSO ZSALES --lock-handle=ABC123 --transport=K900001",
        };
        router.Register("bw", "delete", "Delete BW object",
                         HandleBwDelete, std::move(help));
    }

    // bw activate
    {
        CommandHelp help;
        help.usage = "erpl-adt bw activate <type> <name> [<name2> ...] [flags]";
        help.args_description = "<type>     Object type\n"
                                "  <name>     Object name(s) to activate";
        help.long_description = "Activate BW objects. Supports validate, simulate, "
            "and background modes.";
        help.flags = {
            {"validate", "", "Pre-check only, don't activate", false},
            {"simulate", "", "Dry run of activation", false},
            {"background", "", "Run as background job", false},
            {"force", "", "Force activation even with warnings", false},
            {"exec-check", "", "Set execChk=true in activation payload", false},
            {"with-cto", "", "Set withCTO=true in activation payload", false},
            {"sort", "", "Validate mode: sort dependency order", false},
            {"only-ina", "", "Validate mode: only inactive objects", false},
            {"transport", "<corrnr>", "Transport request", false},
        };
        help.examples = {
            "erpl-adt bw activate ADSO ZSALES_DATA",
            "erpl-adt bw activate ADSO ZSALES_DATA --validate",
            "erpl-adt bw activate ADSO ZSALES_DATA --background --transport=K900001",
        };
        router.Register("bw", "activate", "Activate BW objects",
                         HandleBwActivate, std::move(help));
    }

    // bw xref
    {
        CommandHelp help;
        help.usage = "erpl-adt bw xref <type> <name> [flags]";
        help.args_description = "<type>    Object type (ADSO, IOBJ, TRFN, ...)\n"
                                "  <name>    Object name";
        help.long_description = "Show cross-references (dependencies) for a BW object. "
            "Shows which objects use or are used by the specified object.";
        help.flags = {
            {"version", "<v>", "Object version: A (active), M (modified)", false},
            {"association", "<code>", "Filter by association code (001, 002, 003, ...)", false},
            {"assoc-type", "<type>", "Filter by associated object type (IOBJ, ADSO, ...)", false},
            {"max", "<n>", "Maximum number of results to return", false},
        };
        help.examples = {
            "erpl-adt bw xref ADSO ZSALES_DATA",
            "erpl-adt bw xref ADSO ZSALES_DATA --association=001",
            "erpl-adt bw xref IOBJ 0MATERIAL --max=10",
            "erpl-adt --json bw xref IOBJ 0MATERIAL",
        };
        router.Register("bw", "xref", "Show BW cross-references",
                         HandleBwXref, std::move(help));
    }

    // bw nodes
    {
        CommandHelp help;
        help.usage = "erpl-adt bw nodes <type> <name> [flags]";
        help.args_description = "<type>    Object type (ADSO, IOBJ, TRFN, ...)\n"
                                "  <name>    Object name";
        help.long_description = "Show child node structure of a BW object. Lists component "
            "objects (transformations, DTPs, etc.) belonging to the specified object.";
        help.flags = {
            {"datasource", "", "Use DataSource structure path instead of InfoProvider", false},
            {"child-name", "<name>", "Filter by child name", false},
            {"child-type", "<type>", "Filter by child type", false},
        };
        help.examples = {
            "erpl-adt bw nodes ADSO ZSALES_DATA",
            "erpl-adt bw nodes RSDS ZSOURCE --datasource",
            "erpl-adt --json bw nodes ADSO ZSALES --child-type=TRFN",
        };
        router.Register("bw", "nodes", "Show BW object node structure",
                         HandleBwNodes, std::move(help));
    }

    // bw search-md
    {
        CommandHelp help;
        help.usage = "erpl-adt bw search-md";
        help.long_description = "Read BW search metadata definitions used by the BW repository search service.";
        help.examples = {
            "erpl-adt bw search-md",
            "erpl-adt --json bw search-md",
        };
        router.Register("bw", "search-md", "Show BW search metadata",
                         HandleBwSearchMetadata, std::move(help));
    }

    // bw favorites
    {
        CommandHelp help;
        help.usage = "erpl-adt bw favorites [list|clear]";
        help.long_description = "List backend favorites or clear all backend favorites.";
        help.examples = {
            "erpl-adt bw favorites",
            "erpl-adt bw favorites list",
            "erpl-adt bw favorites clear",
        };
        router.Register("bw", "favorites", "List/clear BW backend favorites",
                         HandleBwFavorites, std::move(help));
    }

    // bw nodepath
    {
        CommandHelp help;
        help.usage = "erpl-adt bw nodepath --object-uri <uri>";
        help.long_description = "Resolve repository node path for a BW object URI.";
        help.flags = {
            {"object-uri", "<uri>", "BW object URI (e.g. /sap/bw/modeling/adso/...) ", true},
        };
        help.examples = {
            "erpl-adt bw nodepath --object-uri=/sap/bw/modeling/adso/ZSALES/a",
            "erpl-adt --json bw nodepath /sap/bw/modeling/adso/ZSALES/a",
        };
        router.Register("bw", "nodepath", "Resolve BW object node path",
                         HandleBwNodePath, std::move(help));
    }

    // bw valuehelp
    {
        CommandHelp help;
        help.usage = "erpl-adt bw valuehelp <domain> [flags]";
        help.args_description = "<domain>   Value-help domain path segment (e.g. infoareas, infoobject)";
        help.long_description = "Read BW value-help endpoints under /sap/bw/modeling/is/values/*.";
        help.flags = {
            {"query", "<qs>", "Raw query string (k=v&k2=v2)", false},
            {"max", "<n>", "Max rows", false},
            {"pattern", "<text>", "Pattern filter", false},
            {"type", "<code>", "Object type filter", false},
            {"infoprovider", "<name>", "InfoProvider filter", false},
        };
        help.examples = {
            "erpl-adt bw valuehelp infoareas --max=100",
            "erpl-adt bw valuehelp infoobject --query='feedSize=20&pattern=0*'",
        };
        router.Register("bw", "valuehelp", "BW value-help lookup",
                         HandleBwValueHelp, std::move(help));
    }

    // bw virtualfolders
    {
        CommandHelp help;
        help.usage = "erpl-adt bw virtualfolders [flags]";
        help.long_description = "Read BW virtual folder tree when service is available.";
        help.flags = {
            {"package", "<pkg>", "Package filter", false},
            {"type", "<type>", "Object type filter", false},
            {"user", "<user>", "User filter", false},
        };
        help.examples = {
            "erpl-adt bw virtualfolders",
            "erpl-adt bw virtualfolders --package=ZPKG",
        };
        router.Register("bw", "virtualfolders", "Read BW virtual folders",
                         HandleBwVirtualFolders, std::move(help));
    }

    // bw datavolumes
    {
        CommandHelp help;
        help.usage = "erpl-adt bw datavolumes [flags]";
        help.long_description = "Read BW data-volume service when available.";
        help.flags = {
            {"infoprovider", "<name>", "InfoProvider filter", false},
            {"max", "<n>", "Max rows", false},
        };
        help.examples = {
            "erpl-adt bw datavolumes --infoprovider=ZSALES",
        };
        router.Register("bw", "datavolumes", "Read BW data volumes",
                         HandleBwDataVolumes, std::move(help));
    }

    // bw applog
    {
        CommandHelp help;
        help.usage = "erpl-adt bw applog [flags]";
        help.long_description = "List BW repository application log entries.";
        help.flags = {
            {"username", "<user>", "Filter by user", false},
            {"start", "<timestamp>", "Filter by start timestamp", false},
            {"end", "<timestamp>", "Filter by end timestamp", false},
        };
        help.examples = {
            "erpl-adt bw applog",
            "erpl-adt bw applog --username=DEVELOPER",
            "erpl-adt --json bw applog --start=20260101000000 --end=20261231235959",
        };
        router.Register("bw", "applog", "Read BW repository application logs",
                         HandleBwApplicationLog, std::move(help));
    }

    // bw message
    {
        CommandHelp help;
        help.usage = "erpl-adt bw message <identifier> <textype> [flags]";
        help.args_description = "<identifier>    Message class/identifier\n"
                                "  <textype>      Message text type";
        help.long_description = "Resolve localized BW message text for a message identifier and type.";
        help.flags = {
            {"msgv1", "<value>", "Message variable 1", false},
            {"msgv2", "<value>", "Message variable 2", false},
            {"msgv3", "<value>", "Message variable 3", false},
            {"msgv4", "<value>", "Message variable 4", false},
        };
        help.examples = {
            "erpl-adt bw message RSDHA 001",
            "erpl-adt bw message RSDHA 001 --msgv1=ZOBJ --msgv2=ADSO",
        };
        router.Register("bw", "message", "Resolve BW message text",
                         HandleBwMessage, std::move(help));
    }

    // bw validate
    {
        CommandHelp help;
        help.usage = "erpl-adt bw validate <type> <name> [--action=validate]";
        help.args_description = "<type>    BW object type\n  <name>    BW object name";
        help.long_description = "Run BW validation endpoint for a specific BW object.";
        help.flags = {
            {"action", "<name>", "Validation action name (default: validate)", false},
        };
        help.examples = {
            "erpl-adt bw validate ADSO ZSALES",
            "erpl-adt --json bw validate ADSO ZSALES --action=check",
        };
        router.Register("bw", "validate", "Validate BW object",
                         HandleBwValidate, std::move(help));
    }

    // bw reporting
    {
        CommandHelp help;
        help.usage = "erpl-adt bw reporting <compid> [flags]";
        help.args_description = "<compid>   Query/component id";
        help.long_description = "Run BW reporting metadata request (BICS reporting endpoint).";
        help.flags = {
            {"dbgmode", "", "Set dbgmode=true query parameter", false},
            {"metadata-only", "", "MetadataOnly header", false},
            {"incl-metadata", "", "InclMetadata header", false},
            {"incl-object-values", "", "InclObjectValues header", false},
            {"incl-except-def", "", "InclExceptDef header", false},
            {"compact-mode", "", "CompactMode header", false},
            {"from-row", "<n>", "FromRow header", false},
            {"to-row", "<n>", "ToRow header", false},
        };
        help.examples = {
            "erpl-adt bw reporting 0D_FC_NW_C01_Q0007 --metadata-only --incl-metadata",
        };
        router.Register("bw", "reporting", "Read BW reporting metadata",
                         HandleBwReporting, std::move(help));
    }

    // bw qprops
    {
        CommandHelp help;
        help.usage = "erpl-adt bw qprops";
        help.long_description = "Read BW query-properties rule service (rules/qprops).";
        help.examples = {
            "erpl-adt bw qprops",
            "erpl-adt --json bw qprops",
        };
        router.Register("bw", "qprops", "Read BW query properties rules",
                         HandleBwQueryProperties, std::move(help));
    }

    // bw move
    {
        CommandHelp help;
        help.usage = "erpl-adt bw move [list]";
        help.long_description = "List BW move request entries from the BW move-requests endpoint.";
        help.examples = {
            "erpl-adt bw move",
            "erpl-adt bw move list",
            "erpl-adt --json bw move",
        };
        router.Register("bw", "move", "List BW move requests",
                         HandleBwMove, std::move(help));
    }

    // bw transport
    {
        CommandHelp help;
        help.usage = "erpl-adt bw transport <check|write|list|collect> [args]";
        help.args_description = "<action>    check, write, list, or collect";
        help.long_description = "BW transport operations. 'check' shows transport state "
            "and changeability. 'write' adds objects to a transport. 'list' shows requests. "
            "'collect' gathers dependent objects for transport with dataflow grouping.";
        help.flags = {
            {"transport", "<corrnr>", "Transport number (for write/collect)", false},
            {"package", "<pkg>", "Package name (for write)", false},
            {"own-only", "", "Show only own transport requests", false},
            {"rddetails", "<mode>", "Check/list detail mode: off|objs|all (default all)", false},
            {"rdprops", "", "Check/list include properties section", false},
            {"allmsgs", "", "Include all messages where supported", false},
            {"simulate", "", "Dry run (write only)", false},
            {"mode", "<code>", "Collection mode (e.g. 000,001,002,003,004,005,033)", false},
            {"transport-lock-holder", "<corrnr>", "Explicit Transport-Lock-Holder header", false},
            {"foreign-objects", "<value>", "Foreign-Objects header", false},
            {"foreign-object-locks", "<value>", "Foreign-Object-Locks header", false},
            {"foreign-correction-number", "<corrnr>", "Foreign-Correction-Number header", false},
            {"foreign-package", "<pkg>", "Foreign-Package header", false},
        };
        help.examples = {
            "erpl-adt bw transport check",
            "erpl-adt bw transport list --own-only",
            "erpl-adt bw transport write ADSO ZSALES --transport=K900001",
            "erpl-adt bw transport collect ADSO ZSALES --mode=001",
        };
        router.Register("bw", "transport", "BW transport operations",
                         HandleBwTransport, std::move(help));
    }

    // bw locks
    {
        CommandHelp help;
        help.usage = "erpl-adt bw locks <list|delete> [flags]";
        help.args_description = "<action>    list or delete";
        help.long_description = "Monitor and manage BW object locks. "
            "'list' shows active locks. 'delete' removes a stuck lock (admin operation).";
        help.flags = {
            {"user", "<name>", "Filter/specify lock owner user", false},
            {"search", "<pattern>", "Search pattern for list", false},
            {"max", "<n>", "Maximum results (default: 100)", false},
            {"table-name", "<name>", "Table name from list (for delete)", false},
            {"arg", "<base64>", "Encoded arg from list (for delete)", false},
            {"mode", "<code>", "Lock mode, default: E (for delete)", false},
            {"scope", "<n>", "Lock scope, default: 1 (for delete)", false},
            {"owner1", "<base64>", "Owner1 from list (for delete)", false},
            {"owner2", "<base64>", "Owner2 from list (for delete)", false},
        };
        help.examples = {
            "erpl-adt bw locks list",
            "erpl-adt bw locks list --user=DEVELOPER",
            "erpl-adt --json bw locks list",
            "erpl-adt bw locks delete --user=DEVELOPER --table-name=RSBWOBJ_ENQUEUE --arg=...",
        };
        router.Register("bw", "locks", "Monitor BW object locks",
                         HandleBwLocks, std::move(help));
    }

    // bw dbinfo
    {
        CommandHelp help;
        help.usage = "erpl-adt bw dbinfo";
        help.long_description = "Show HANA database connection info (host, port, schema).";
        help.examples = {
            "erpl-adt bw dbinfo",
            "erpl-adt --json bw dbinfo",
        };
        router.Register("bw", "dbinfo", "Show HANA database info",
                         HandleBwDbInfo, std::move(help));
    }

    // bw sysinfo
    {
        CommandHelp help;
        help.usage = "erpl-adt bw sysinfo";
        help.long_description = "Show BW system properties.";
        help.examples = {
            "erpl-adt bw sysinfo",
            "erpl-adt --json bw sysinfo",
        };
        router.Register("bw", "sysinfo", "Show BW system properties",
                         HandleBwSysInfo, std::move(help));
    }

    // bw changeability
    {
        CommandHelp help;
        help.usage = "erpl-adt bw changeability";
        help.long_description = "Show per-TLOGO changeability and transport settings.";
        help.examples = {
            "erpl-adt bw changeability",
            "erpl-adt --json bw changeability",
        };
        router.Register("bw", "changeability", "Show BW changeability settings",
                         HandleBwChangeability, std::move(help));
    }

    // bw adturi
    {
        CommandHelp help;
        help.usage = "erpl-adt bw adturi";
        help.long_description = "Show BW-to-ADT URI mappings.";
        help.examples = {
            "erpl-adt bw adturi",
            "erpl-adt --json bw adturi",
        };
        router.Register("bw", "adturi", "Show BW-to-ADT URI mappings",
                         HandleBwAdtUri, std::move(help));
    }

    // bw job
    {
        CommandHelp help;
        help.usage = "erpl-adt bw job <action> [args]";
        help.args_description = "<action>    list, result, status, progress, steps, step, messages, cancel, restart, cleanup\n"
                                "  [guid]      25-character job GUID (not required for list)\n"
                                "  [step]      Step name/id (required for 'step')";
        help.long_description = "Monitor and manage BW background jobs. "
            "'restart' restarts a failed job. 'cleanup' removes temporary job resources.";
        help.examples = {
            "erpl-adt bw job list",
            "erpl-adt bw job result ABC12345678901234567890",
            "erpl-adt bw job status ABC12345678901234567890",
            "erpl-adt bw job step ABC12345678901234567890 ACTIVATE",
            "erpl-adt bw job messages ABC12345678901234567890",
            "erpl-adt bw job cancel ABC12345678901234567890",
            "erpl-adt bw job restart ABC12345678901234567890",
            "erpl-adt bw job cleanup ABC12345678901234567890",
        };
        router.Register("bw", "job", "BW background job operations",
                         HandleBwJob, std::move(help));
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

    auto port_result = ParsePort(port_str);
    if (port_result.IsErr()) {
        std::cerr << "Error: " << port_result.Error().message << "\n";
        return 99;
    }

    auto client_result = SapClient::Create(client);
    if (client_result.IsErr()) {
        std::cerr << "Error: Invalid --client: " << client_result.Error() << "\n";
        return 99;
    }

    SavedCredentials creds;
    creds.host = host;
    creds.port = port_result.Value();
    creds.user = user;
    creds.password = password;
    creds.client = std::move(client_result).Value().Value();
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
