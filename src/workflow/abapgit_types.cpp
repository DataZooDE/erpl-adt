#include "abapgit_types.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace erpl_adt {

namespace {

// T100 messages (class, number) that mean "someone else holds the object":
// enqueue and editor locks. Read from T100 on A4H (SE91); the text is
// translated, the id is not.
bool IsLockMessage(const std::string& id, const std::string& number) {
    static const std::set<std::string> kLock = {
        "MC601", "MC520", "MC359", "MC361",                    // enqueue
        "EU220", "EU290", "EU315", "EU367", "EU510", "EU521", "EU884",  // workbench
        "DS063", "DS068", "DS401", "DS413", "DS414", "DS425", "DS495",  // editor
        "DS518", "DS554", "DS596", "ED545",                    // text pool, includes
        "DT653",                                               // DDIC activation
        "TK022", "TK051", "TK053", "TK106", "TK133", "TK864",  // CTS: being edited
        "TR059", "TR068", "TR134",
    };
    return kLock.count(id + number) > 0;
}

// Message classes of the Change and Transport System; "locked in request X"
// (TK247, TK310, ...) is a CTS conflict, not an edit lock.
bool IsCtsMessage(const std::string& id) { return id == "TK" || id == "TR" || id == "TO"; }

}  // namespace

std::vector<std::shared_ptr<const IClassicTypeHandler>> MakeClassicTypeHandlers() {
    return {MakeDomaHandler(), MakeDtelHandler(), MakeFugrHandler(), MakeProgHandler(),
            MakeTranHandler()};
}

Error EngineFailure(const std::string& what, const EngineOpResult& result) {
    Error err;
    err.operation = "abapGit";
    err.endpoint = "classrun";
    err.message = what + ": " + SummarizeFailure(result);
    err.category = ErrorCategory::ActivationError;
    // 1. The engine's own verdict from the FM exception (@op KIND ...).
    auto kind = result.values.find("KIND");
    if (kind != result.values.end() && !kind->second.empty()) {
        const auto& k = kind->second.back();
        if (k == "LOCK") {
            err.category = ErrorCategory::LockConflict;
        } else if (k == "CTS") {
            err.category = ErrorCategory::TransportError;
        } else if (k == "AUTH") {
            err.category = ErrorCategory::Authorization;
        } else if (k == "CHECK") {
            err.category = ErrorCategory::CheckError;
        }
        return err;
    }
    // 2. SAP's message ids: a lock wins over CTS, CTS over everything else.
    bool cts = false;
    for (const auto& m : result.messages) {
        if (IsLockMessage(m.id, m.number)) {
            err.category = ErrorCategory::LockConflict;
            return err;
        }
        cts = cts || IsCtsMessage(m.id);
    }
    if (cts) {
        err.category = ErrorCategory::TransportError;
    }
    return err;
}

Result<std::vector<EngineOpResult>, Error> RunChecked(IEngineRunner& engine,
                                                      const std::vector<EngineOp>& ops,
                                                      const std::string& what) {
    auto run = engine.Run(ops);
    if (run.IsErr()) {
        return run;
    }
    for (const auto& r : run.Value()) {
        if (r.rc != 0) {
            return Result<std::vector<EngineOpResult>, Error>::Err(EngineFailure(what, r));
        }
    }
    return run;
}

std::map<std::string, std::string> ChangedParts(const EngineOpResult& result) {
    std::map<std::string, std::string> out;
    auto it = result.values.find("CHANGED");
    if (it == result.values.end()) {
        return out;
    }
    for (const auto& line : it->second) {
        const auto space = line.rfind(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string user = line.substr(space + 1);
        out[line.substr(0, space)] = user == "?" ? "" : user;
    }
    return out;
}

std::string CommonChanger(const std::vector<std::string>& users) {
    if (users.empty()) {
        return "";
    }
    for (const auto& u : users) {
        if (u.empty() || u != users.front()) {
            return "";
        }
    }
    return users.front();
}

std::string FileName(const AbapGitObjectKey& key, const std::string& part) {
    return ObjectFileStem(key) + "." + part;
}

Result<std::string, Error> EnginePayload(const ObjectFileSet& files,
                                         const std::string& file_name) {
    auto it = files.find(file_name);
    if (it == files.end()) {
        Error err;
        err.operation = "abapGit";
        err.message = "Missing file " + file_name;
        err.category = ErrorCategory::Internal;
        return Result<std::string, Error>::Err(err);
    }
    auto parsed = ParseAbapGitXml(it->second);
    if (parsed.IsErr()) {
        return Result<std::string, Error>::Err(parsed.Error());
    }
    return Result<std::string, Error>::Ok(WrapAsxValues(parsed.Value().values_xml));
}

Result<std::string, Error> RenderFromEngine(const std::string& asxml,
                                            const std::string& serializer) {
    auto values = ExtractAsxValues(asxml);
    if (values.IsErr()) {
        return values;
    }
    AbapGitXml xml;
    xml.serializer = serializer;
    xml.serializer_version = "v1.0.0";
    xml.values_xml = values.Value();
    return Result<std::string, Error>::Ok(RenderAbapGitXml(xml));
}

namespace {

std::string_view LocalName(const char* name) {
    std::string_view n(name ? name : "");
    const auto colon = n.find(':');
    return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

size_t Utf8Chars(std::string_view s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

std::string SiblingText(const tinyxml2::XMLElement* el, const char* name) {
    const auto* parent = el->Parent() ? el->Parent()->ToElement() : nullptr;
    if (!parent) return "";
    for (auto* s = parent->FirstChildElement(); s; s = s->NextSiblingElement()) {
        if (LocalName(s->Name()) == name) return s->GetText() ? s->GetText() : "";
    }
    return "";
}

void CollectTooLong(const tinyxml2::XMLElement* el, std::vector<std::string_view>& path,
                    const std::vector<TextLimit>& limits, std::vector<std::string>& problems) {
    const auto name = LocalName(el->Name());
    for (const auto& rule : limits) {
        if (name != rule.field) continue;
        if (std::find(path.begin(), path.end(), std::string_view(rule.container)) == path.end()) {
            continue;
        }
        if (rule.id && SiblingText(el, "ID") != rule.id) continue;
        const std::string value = el->GetText() ? el->GetText() : "";
        const size_t chars = Utf8Chars(value);
        if (chars <= rule.limit) continue;
        std::string what = rule.label ? rule.label : rule.field;
        const auto fm = SiblingText(el, "FUNCNAME");
        if (!fm.empty()) what = "function module " + fm + " " + what;
        // Inside a translation section, say which language.
        const bool translation = std::any_of(path.begin(), path.end(), [](std::string_view p) {
            return p.rfind("I18N", 0) == 0 ||
                   (p.size() > 6 && p.compare(p.size() - 6, 6, "_TEXTS") == 0);
        });
        if (translation) {
            std::string language = SiblingText(el, "DDLANGUAGE");
            if (language.empty()) language = SiblingText(el, "LANGU");
            if (language.empty()) {
                // I18N_TPOOL: <item><LANGUAGE>D</LANGUAGE><TEXTPOOL><item>...
                for (const auto* up = el->Parent(); up && language.empty(); up = up->Parent()) {
                    const auto* e = up->ToElement();
                    if (!e) break;
                    for (auto* s = e->FirstChildElement(); s; s = s->NextSiblingElement()) {
                        if (LocalName(s->Name()) == "LANGUAGE" && s->GetText()) {
                            language = s->GetText();
                        }
                    }
                }
            }
            if (!language.empty()) what += " (language " + language + ")";
        }
        problems.push_back(what + " '" + value + "' has " + std::to_string(chars) +
                           " characters; SAP's field holds " + std::to_string(rule.limit));
    }
    path.push_back(name);
    for (auto* child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        CollectTooLong(child, path, limits, problems);
    }
    path.pop_back();
}

}  // namespace

Result<void, Error> CheckTextLimits(const std::string& object, const std::string& xml,
                                    const std::vector<TextLimit>& limits) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) {
        return Result<void, Error>::Ok();  // the payload checks report unreadable files
    }
    std::vector<std::string_view> path;
    std::vector<std::string> problems;
    CollectTooLong(doc.RootElement(), path, limits, problems);
    if (problems.empty()) return Result<void, Error>::Ok();
    std::string message = object + ": ";
    for (size_t i = 0; i < problems.size(); ++i) {
        if (i) message += "; ";
        message += problems[i];
    }
    message += ". Shorten it in the files - SAP would cut it, and every later deploy would see "
               "a change. Nothing was changed.";
    Error error;
    error.operation = "abapGit";
    error.message = std::move(message);
    error.category = ErrorCategory::CheckError;
    return Result<void, Error>::Err(std::move(error));
}

}  // namespace erpl_adt
