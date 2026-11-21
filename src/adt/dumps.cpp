#include <erpl_adt/adt/dumps.hpp>

#include <erpl_adt/core/url.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace erpl_adt {

namespace {

constexpr const char* kFeedPath = "/sap/bc/adt/runtime/dumps";
constexpr const char* kDumpPath = "/sap/bc/adt/runtime/dump/";

Error Invalid(const std::string& operation, const std::string& message) {
    return Error{operation, "", std::nullopt, message, std::nullopt, ErrorCategory::Validation};
}

std::string Trim(std::string_view s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(begin, end - begin + 1));
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
long DaysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void CivilFromDays(long z, int& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

// Zero-padded decimal, e.g. Pad(7, 2) == "07".
std::string Pad(int value, size_t width) {
    std::string out = std::to_string(value);
    if (out.size() < width) out.insert(0, width - out.size(), '0');
    return out;
}

bool AllDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(),
                                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

// The server's `from` (YYYYMMDDhhmmss): a day before `since`, because the
// server compares in system time; the exact cut is made on the UTC time.
std::string ServerFrom(const std::string& since) {
    int y = std::stoi(since.substr(0, 4));
    int m = std::stoi(since.substr(5, 2));
    int d = std::stoi(since.substr(8, 2));
    CivilFromDays(DaysFromCivil(y, m, d) - 1, y, m, d);
    return Pad(y, 4) + Pad(m, 2) + Pad(d, 2) + since.substr(11, 2) + since.substr(14, 2) + since.substr(17, 2);
}

// SAP user names: letters, digits and a few specials (SAP*, DDIC, A&B, /X/).
// Not ',', '(' or ')' - they would end the $query expression; the value is
// URL-encoded on top.
bool IsUserName(std::string_view s) {
    return !s.empty() && s.size() <= 12 &&
           std::all_of(s.begin(), s.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
                      c == '-' || c == '@' || c == '*' || c == '&' || c == '/';
           });
}

// "ZCL_X" or ".../oo/classes/zcl_x" -> "ZCL_X"; "%2f" in a URI is "/".
std::string ClassNameOf(const std::string& target) {
    std::string name = target;
    if (const auto at = name.find("/oo/classes/"); at != std::string::npos) {
        name = name.substr(at + 12);
        name = name.substr(0, name.find('/'));
    }
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        if (i + 2 < name.size() && name.compare(i, 3, "%2f") == 0) {
            out += '/';
            i += 2;
        } else if (i + 2 < name.size() && name.compare(i, 3, "%2F") == 0) {
            out += '/';
            i += 2;
        } else {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
        }
    }
    return out;
}

// The class pool of `class_name`: the name, '=' padding, "CP".
bool IsClassPoolOf(const std::string& program, const std::string& class_name) {
    if (class_name.empty() || program.rfind(class_name, 0) != 0) return false;
    const auto rest = std::string_view(program).substr(class_name.size());
    if (rest.size() < 2 || rest.substr(rest.size() - 2) != "CP") return false;
    const auto padding = rest.substr(0, rest.size() - 2);
    return std::all_of(padding.begin(), padding.end(), [](char c) { return c == '='; });
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// The id after ".../runtime/dump/" in an ADT link.
std::string IdFromLink(std::string_view href) {
    const auto at = href.find(kDumpPath);
    if (at == std::string_view::npos) return "";
    return std::string(href.substr(at + std::string_view(kDumpPath).size()));
}

Result<std::string, Error> GetText(IAdtSession& session, const std::string& path,
                                   const char* accept, const char* operation) {
    auto response = session.Get(path, HttpHeaders{{"Accept", accept}});
    if (response.IsErr()) return Result<std::string, Error>::Err(response.Error());
    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::string, Error>::Err(
            Error::FromHttpStatus(operation, path, http.status_code, http.body));
    }
    return Result<std::string, Error>::Ok(http.body);
}

// The keyed rows of the unformatted dump: "|ABAP: Line   |AL|3".
std::string UnformattedValue(const std::string& text, std::string_view key) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2 || line[0] != '|') continue;
        const auto second = line.find('|', 1);
        if (second == std::string::npos) continue;
        const auto third = line.find('|', second + 1);
        if (third == std::string::npos) continue;
        if (Trim(std::string_view(line).substr(second + 1, third - second - 1)) == key) {
            return Trim(std::string_view(line).substr(third + 1));
        }
    }
    return "";
}

std::optional<int> ToInt(const std::string& s) {
    if (!AllDigits(s)) return std::nullopt;
    return std::stoi(s);
}

// The body of the formatted chapter that starts at 1-based `title_line`:
// the boxed lines after the title up to the closing rule.
std::string ChapterBody(const std::string& text, int title_line) {
    std::istringstream in(text);
    std::string line;
    std::string body;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (number <= title_line) continue;
        if (line.rfind("---", 0) == 0) break;
        auto content = Trim(line);
        if (!content.empty() && content.front() == '|') content.erase(0, 1);
        if (!content.empty() && content.back() == '|') content.pop_back();
        content = Trim(content);
        if (content.empty()) continue;
        if (!body.empty()) body += ' ';
        body += content;
    }
    return body;
}

}  // namespace

Result<std::string, Error> ParseDumpSince(std::string_view since) {
    const auto bad = [&] {
        return Result<std::string, Error>::Err(Invalid(
            "ParseDumpSince", "--since '" + std::string(since) +
                                  "' is not a UTC time; use 2026-11-21T16:12:00Z, 2026-11-21T16:12, "
                                  "2026-11-21 or 20261121161200"));
    };
    std::string s(since);
    std::string digits;
    if (s.size() == 14 && AllDigits(s)) {
        digits = s;
    } else {
        if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) s.pop_back();
        // YYYY-MM-DD[THH:MM[:SS]]
        if (s.size() != 10 && s.size() != 16 && s.size() != 19) return bad();
        if (s[4] != '-' || s[7] != '-') return bad();
        digits = s.substr(0, 4) + s.substr(5, 2) + s.substr(8, 2);
        if (s.size() == 10) {
            digits += "000000";
        } else {
            if ((s[10] != 'T' && s[10] != 't' && s[10] != ' ') || s[13] != ':') return bad();
            digits += s.substr(11, 2) + s.substr(14, 2);
            if (s.size() == 19) {
                if (s[16] != ':') return bad();
                digits += s.substr(17, 2);
            } else {
                digits += "00";
            }
        }
        if (!AllDigits(digits)) return bad();
    }
    const int y = std::stoi(digits.substr(0, 4));
    const int mo = std::stoi(digits.substr(4, 2));
    const int d = std::stoi(digits.substr(6, 2));
    const int h = std::stoi(digits.substr(8, 2));
    const int mi = std::stoi(digits.substr(10, 2));
    const int se = std::stoi(digits.substr(12, 2));
    if (mo < 1 || mo > 12 || d < 1 || h > 23 || mi > 59 || se > 59) return bad();
    int cy = 0, cm = 0, cd = 0;
    CivilFromDays(DaysFromCivil(y, mo, d), cy, cm, cd);
    if (cy != y || cm != mo || cd != d) return bad();  // e.g. 2026-02-30
    return Result<std::string, Error>::Ok(Pad(y, 4) + "-" + Pad(mo, 2) + "-" + Pad(d, 2) + "T" +
                                          Pad(h, 2) + ":" + Pad(mi, 2) + ":" + Pad(se, 2) + "Z");
}

Result<std::string, Error> NormalizeDumpId(std::string_view id) {
    std::string value(id);
    if (const auto at = value.find(kDumpPath); at != std::string::npos) {
        value = value.substr(at + std::string_view(kDumpPath).size());
    }
    const bool ok = !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '%' || c == '_' || c == '-' ||
               c == '.';
    });
    if (!ok) {
        return Result<std::string, Error>::Err(Invalid(
            "GetDump", "'" + std::string(id) +
                           "' is not a dump id; take the id from 'erpl-adt dump list'"));
    }
    return Result<std::string, Error>::Ok(value);
}

Result<std::vector<DumpEntry>, Error> ListDumps(IAdtSession& session,
                                                const DumpListOptions& options) {
    using R = Result<std::vector<DumpEntry>, Error>;
    std::string path = std::string(kFeedPath) + "?$top=" + std::to_string(options.max);
    if (options.owner && !options.owner->empty()) {
        if (!IsUserName(*options.owner)) {
            return R::Err(Invalid("ListDumps", "--owner '" + *options.owner + "' is not a user name"));
        }
        path += "&$query=and%28equals%28user%2c" + UrlEncode(Upper(*options.owner)) + "%29%29";
    }
    std::optional<std::string> since;
    if (options.since) {
        auto parsed = ParseDumpSince(*options.since);
        if (parsed.IsErr()) return R::Err(parsed.Error());
        since = parsed.Value();
        path += "&from=" + ServerFrom(*since);
    }

    auto body = GetText(session, path, "application/atom+xml;type=feed", "ListDumps");
    if (body.IsErr()) return R::Err(body.Error());

    return adt_utils::ParseXmlWithRoot<std::vector<DumpEntry>>(
        body.Value(), "ListDumps", path, "Failed to parse the runtime dump feed",
        [&](const tinyxml2::XMLElement* root) {
            std::vector<DumpEntry> dumps;
            for (auto* entry = root->FirstChildElement(); entry;
                 entry = entry->NextSiblingElement()) {
                if (!atom_parser::HasLocalName(entry, "entry")) continue;
                DumpEntry d;
                if (const auto* author = atom_parser::FirstChildByLocalName(entry, "author")) {
                    d.user = atom_parser::ChildTextByLocalName(author, "name");
                }
                // Categories in order: the runtime error, then the terminated
                // program (their labels are translated, so position decides).
                int category = 0;
                for (auto* child = entry->FirstChildElement(); child;
                     child = child->NextSiblingElement()) {
                    if (atom_parser::HasLocalName(child, "category")) {
                        const char* term = child->Attribute("term");
                        if (category == 0) d.error = term ? term : "";
                        if (category == 1) d.program = term ? term : "";
                        ++category;
                    } else if (atom_parser::HasLocalName(child, "link")) {
                        const char* rel = child->Attribute("rel");
                        const char* href = child->Attribute("href");
                        if (rel && href && std::string_view(rel) == "self") d.id = IdFromLink(href);
                    }
                }
                d.short_text = atom_parser::ChildTextByLocalName(entry, "title");
                d.time = atom_parser::ChildTextByLocalName(entry, "published");
                if (d.id.empty()) continue;
                if (since && d.time < *since) continue;  // both ISO 8601 UTC
                dumps.push_back(std::move(d));
            }
            return R::Ok(std::move(dumps));
        });
}

Result<DumpDetail, Error> GetDump(IAdtSession& session, std::string_view id, bool full_text) {
    using R = Result<DumpDetail, Error>;
    auto normalized = NormalizeDumpId(id);
    if (normalized.IsErr()) return R::Err(normalized.Error());
    const std::string path = std::string(kDumpPath) + normalized.Value();

    auto header = GetText(session, path, "application/vnd.sap.adt.runtime.dump.v1+xml", "GetDump");
    if (header.IsErr()) return R::Err(header.Error());

    DumpDetail d;
    d.id = normalized.Value();
    int short_text_line = 0;
    {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(header.Value().data(), header.Value().size()) != tinyxml2::XML_SUCCESS ||
            !doc.RootElement()) {
            return R::Err(Error{"GetDump", path, 200, "Failed to parse the dump header", std::nullopt,
                                ErrorCategory::Internal});
        }
        const auto* root = doc.RootElement();
        auto attr = [&](const char* name) {
            const char* v = root->Attribute(name);
            return std::string(v ? v : "");
        };
        d.error = attr("error");
        d.exception = attr("exception");
        d.program = attr("terminatedProgram");
        d.user = attr("author");
        d.time = attr("datetime");
        if (const auto* chapters = atom_parser::FirstChildByLocalName(root, "chapters")) {
            for (auto* ch = chapters->FirstChildElement(); ch; ch = ch->NextSiblingElement()) {
                const char* name = ch->Attribute("name");
                if (name && std::string_view(name) == "kap0") short_text_line = ch->IntAttribute("line");
            }
        }
    }

    auto unformatted = GetText(session, path + "/unformatted", "text/plain", "GetDump");
    if (unformatted.IsErr()) return R::Err(unformatted.Error());
    const auto& rows = unformatted.Value();
    if (d.error.empty()) d.error = UnformattedValue(rows, "FC");
    if (d.exception.empty()) d.exception = UnformattedValue(rows, "XC");
    d.include = UnformattedValue(rows, "AI");
    d.include_line = ToInt(UnformattedValue(rows, "AL"));
    d.event = UnformattedValue(rows, "EV");
    d.main_program = UnformattedValue(rows, "AM");

    // Best effort from here: the short text and the source position help,
    // but a dump without them is still worth showing - unless the full text
    // was asked for.
    auto formatted = GetText(session, path + "/formatted", "text/plain", "GetDump");
    if (formatted.IsOk()) {
        if (short_text_line > 0) d.short_text = ChapterBody(formatted.Value(), short_text_line);
        if (full_text) d.text = formatted.Value();
    } else if (full_text) {
        return R::Err(formatted.Error());
    }

    auto summary = GetText(session, path + "/summary", "text/html", "GetDump");
    if (summary.IsOk()) {
        // The source extract links the termination point as ADT source:
        // href="adt://A4H/sap/bc/adt/oo/classes/zcl_x/source/main#start=9".
        const auto& html = summary.Value();
        const auto section = html.find("id=\"SOURCE\"");
        if (section != std::string::npos) {
            const auto href = html.find("href=\"adt://", section);
            if (href != std::string::npos) {
                const auto end = html.find('"', href + 6);
                const std::string link = html.substr(href + 6, end - href - 6);
                const auto adt = link.find("/sap/bc/adt/");
                const auto position = adt_utils::ParseUriStartPosition(link);
                if (adt != std::string::npos && position) {
                    const auto local = link.substr(adt);
                    d.uri = local.substr(0, local.find('#'));
                    d.line = position->first;
                }
            }
        }
    }
    return R::Ok(std::move(d));
}

std::optional<DumpEntry> FindDumpSince(IAdtSession& session, const std::string& user,
                                       const std::string& since, const std::string& class_name) {
    DumpListOptions options;
    options.owner = user;
    options.since = since;
    options.max = 5;  // the user may have dumped elsewhere in the meantime
    auto dumps = ListDumps(session, options);
    if (dumps.IsErr()) return std::nullopt;
    const auto name = ClassNameOf(class_name);
    for (const auto& d : dumps.Value()) {
        if (IsClassPoolOf(d.program, name)) return d;
    }
    return std::nullopt;
}

std::string DumpTime(std::chrono::system_clock::time_point when) {
    const auto days = std::chrono::duration_cast<std::chrono::hours>(when.time_since_epoch()).count() / 24;
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count() -
                      days * 86400;
    int y = 0, m = 0, d = 0;
    CivilFromDays(static_cast<long>(days), y, m, d);
    const int sec = static_cast<int>(secs);
    return Pad(y, 4) + "-" + Pad(m, 2) + "-" + Pad(d, 2) + "T" + Pad(sec / 3600, 2) + ":" +
           Pad(sec / 60 % 60, 2) + ":" + Pad(sec % 60, 2) + "Z";
}

void AttachRunDump(IAdtSession& session, const std::string& since, const std::string& class_name,
                   Error& error) {
    const auto user = session.LogonUserName();
    if (user.empty()) return;
    const auto dump = FindDumpSince(session, user, since, class_name);
    if (!dump) return;
    error.category = ErrorCategory::RuntimeDump;
    std::string hint = "The run ended in runtime error " + dump->error;
    if (!dump->short_text.empty()) hint += " (" + dump->short_text + ")";
    if (!dump->program.empty()) hint += " in " + dump->program;
    hint += ". See where and why: erpl-adt dump show " + dump->id;
    error.hint = std::move(hint);
}

nlohmann::json DumpEntryToJson(const DumpEntry& e) {
    return {{"id", e.id},       {"error", e.error}, {"short_text", e.short_text},
            {"program", e.program}, {"user", e.user},   {"time", e.time}};
}

nlohmann::json DumpDetailToJson(const DumpDetail& d) {
    nlohmann::json j = {{"id", d.id},
                        {"error", d.error},
                        {"exception", d.exception},
                        {"short_text", d.short_text},
                        {"program", d.program},
                        {"include", d.include},
                        {"event", d.event},
                        {"main_program", d.main_program},
                        {"user", d.user},
                        {"time", d.time}};
    if (d.include_line) j["include_line"] = *d.include_line;
    if (d.uri) j["uri"] = *d.uri;
    if (d.line) j["line"] = *d.line;
    if (!d.text.empty()) j["text"] = d.text;
    return j;
}

} // namespace erpl_adt
