#include "abapgit_types.hpp"

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

}  // namespace erpl_adt
