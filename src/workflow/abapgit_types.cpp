#include "abapgit_types.hpp"

#include <algorithm>
#include <cctype>

namespace erpl_adt {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return Lower(haystack).find(needle) != std::string::npos;
}

}  // namespace

std::vector<std::shared_ptr<const IClassicTypeHandler>> MakeClassicTypeHandlers() {
    return {MakeDomaHandler()};
}

Error EngineFailure(const std::string& what, const EngineOpResult& result) {
    Error err;
    err.operation = "abapGit";
    err.endpoint = "classrun";
    err.message = what + ": " + SummarizeFailure(result);
    err.category = ErrorCategory::ActivationError;
    for (const auto& m : result.messages) {
        const std::string all = m.text + " " + m.id;
        if (Contains(all, "locked") || Contains(all, "currently editing") ||
            Contains(all, "enqueue") || Contains(all, "gesperrt")) {
            err.category = ErrorCategory::LockConflict;
            break;
        }
        if (m.id == "TK" || m.id == "TR" || Contains(m.text, "request") ||
            Contains(m.text, "auftrag")) {
            err.category = ErrorCategory::TransportError;
        }
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
