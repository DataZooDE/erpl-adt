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
        url += "?corrNr=" + *transport;
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
