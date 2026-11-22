#include <erpl_adt/core/url.hpp>
#include <erpl_adt/adt/function_group.hpp>

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>

namespace erpl_adt {

namespace {

constexpr const char* kGroups = "/sap/bc/adt/functions/groups/";
constexpr const char* kNsAdtCore = "http://www.sap.com/adt/core";

std::string UriSegment(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/') {
            out += "%2f";
        } else {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string BuildCreateXml(const char* root_name, const char* prefix, const char* ns,
                           const char* type, const std::string& group, const std::string& name,
                           const std::string& description) {
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    auto* root = doc.NewElement(root_name);
    root->SetAttribute((std::string("xmlns:") + prefix).c_str(), ns);
    root->SetAttribute("xmlns:adtcore", kNsAdtCore);
    root->SetAttribute("adtcore:description", description.c_str());
    root->SetAttribute("adtcore:name", Upper(name).c_str());
    root->SetAttribute("adtcore:type", type);
    auto* container = doc.NewElement("adtcore:containerRef");
    container->SetAttribute("adtcore:name", Upper(group).c_str());
    container->SetAttribute("adtcore:type", "FUGR/F");
    container->SetAttribute("adtcore:uri", FunctionGroupUri(group).c_str());
    root->InsertEndChild(container);
    doc.InsertEndChild(root);
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return printer.CStr();
}

Result<void, Error> PostCreate(IAdtSession& session, const std::string& operation,
                               std::string url, const std::string& body,
                               const std::string& content_type,
                               const std::optional<std::string>& transport) {
    if (transport && !transport->empty()) {
        url += "?corrNr=" + UrlEncode(*transport);
    }
    auto response = session.Post(url, body, content_type);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }
    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus(operation, url, http.status_code, http.body));
    }
    return Result<void, Error>::Ok();
}

}  // namespace

std::string FunctionGroupUri(const std::string& group) {
    return kGroups + UriSegment(group);
}

std::string FunctionGroupMainProgramName(const std::string& group) {
    std::string upper = group;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (!upper.empty() && upper[0] == '/') {
        const auto end = upper.find('/', 1);
        if (end != std::string::npos) {
            return upper.substr(0, end + 1) + "SAPL" + upper.substr(end + 1);
        }
    }
    return "SAPL" + upper;
}

std::vector<ActivateObjectParams> FunctionGroupMainProgramActivation(
    const std::string& group, const std::string& main_program) {
    const auto group_uri = FunctionGroupUri(group);
    ActivateObjectParams group_ref;
    group_ref.uri = group_uri;
    group_ref.type = "FUGR/F";
    group_ref.name = group;
    ActivateObjectParams main_ref;
    main_ref.uri = group_uri + "/source/main";
    main_ref.type = "FUGR/I";
    main_ref.name = main_program;
    main_ref.parent_uri = group_uri;
    return {group_ref, main_ref};
}

std::string FunctionModuleUri(const std::string& group, const std::string& name) {
    return FunctionGroupUri(group) + "/fmodules/" + UriSegment(name);
}

std::string FunctionGroupIncludeUri(const std::string& group, const std::string& include) {
    return FunctionGroupUri(group) + "/includes/" + UriSegment(include);
}

Result<void, Error> CreateFunctionModule(IAdtSession& session, const std::string& group,
                                         const std::string& name, const std::string& description,
                                         const std::optional<std::string>& transport) {
    return PostCreate(session, "CreateFunctionModule", FunctionGroupUri(group) + "/fmodules",
                      BuildCreateXml("fmodule:abapFunctionModule", "fmodule",
                                     "http://www.sap.com/adt/functions/fmodules", "FUGR/FF",
                                     group, name, description),
                      "application/vnd.sap.adt.functions.fmodules.v3+xml", transport);
}

Result<void, Error> SetFunctionModuleProcessingType(IAdtSession& session, const std::string& group,
                                                    const std::string& name,
                                                    const std::string& processing_type,
                                                    const LockHandle& lock_handle,
                                                    const std::optional<std::string>& transport,
                                                    const std::string& update_task_kind) {
    using R = Result<void, Error>;
    static const std::string kMetaType = "application/vnd.sap.adt.functions.fmodules.v3+xml";
    const auto uri = FunctionModuleUri(group, name);
    HttpHeaders headers;
    headers["Accept"] = kMetaType;
    auto read = session.Get(uri, headers);
    if (read.IsErr()) {
        return R::Err(std::move(read).Error());
    }
    if (read.Value().status_code != 200) {
        return R::Err(Error::FromHttpStatus("SetFunctionModuleProcessingType", uri,
                                            read.Value().status_code, read.Value().body));
    }
    std::string meta = read.Value().body;
    static const std::string kAttr = "fmodule:processingType=\"";
    const auto at = meta.find(kAttr);
    const auto end = at == std::string::npos ? std::string::npos : meta.find('"', at + kAttr.size());
    if (end == std::string::npos) {
        Error e;
        e.operation = "SetFunctionModuleProcessingType";
        e.endpoint = uri;
        e.message = "Function module " + name +
                    ": its metadata names no fmodule:processingType; not guessing where it goes";
        e.category = ErrorCategory::Internal;
        return R::Err(std::move(e));
    }
    meta.replace(at + kAttr.size(), end - at - kAttr.size(), processing_type);
    // fmodule:updateTaskKind: only an update module has it
    static const std::string kKind = " fmodule:updateTaskKind=\"";
    const auto kind_at = meta.find(kKind);
    if (kind_at != std::string::npos) {
        const auto kind_end = meta.find('"', kind_at + kKind.size());
        if (kind_end != std::string::npos) {
            meta.erase(kind_at, kind_end + 1 - kind_at);
        }
    }
    if (!update_task_kind.empty()) {
        const auto type_end = meta.find('"', at + kAttr.size());
        meta.insert(type_end + 1, kKind + update_task_kind + "\"");
    }
    std::string url = uri + "?lockHandle=" + UrlEncode(lock_handle.Value());
    if (transport && !transport->empty()) {
        url += "&corrNr=" + UrlEncode(*transport);
    }
    auto written = session.Put(url, meta, kMetaType);
    if (written.IsErr()) {
        return R::Err(std::move(written).Error());
    }
    if (written.Value().status_code != 200 && written.Value().status_code != 204) {
        return R::Err(Error::FromHttpStatus("SetFunctionModuleProcessingType", url,
                                            written.Value().status_code, written.Value().body));
    }
    return R::Ok();
}

Result<void, Error> CreateFunctionGroupInclude(IAdtSession& session, const std::string& group,
                                               const std::string& include,
                                               const std::string& description,
                                               const std::optional<std::string>& transport) {
    return PostCreate(session, "CreateFunctionGroupInclude", FunctionGroupUri(group) + "/includes",
                      BuildCreateXml("finclude:abapFunctionGroupInclude", "finclude",
                                     "http://www.sap.com/adt/functions/fincludes", "FUGR/I",
                                     group, include, description),
                      "application/vnd.sap.adt.functions.fincludes.v2+xml", transport);
}

}  // namespace erpl_adt
