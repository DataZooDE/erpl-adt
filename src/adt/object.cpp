#include <erpl_adt/adt/object.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <tinyxml2.h>

namespace erpl_adt {

namespace {

Result<ObjectStructure, Error> ParseObjectStructure(
    std::string_view xml, const std::string& uri) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "GetObjectStructure", uri,
            "Failed to parse object metadata XML")) {
        return Result<ObjectStructure, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<ObjectStructure, Error>::Err(Error{
            "GetObjectStructure", uri, std::nullopt,
            "Empty object metadata response", std::nullopt});
    }

    ObjectStructure structure;
    auto& info = structure.info;

    info.name = xml_utils::AttrAny(root, "adtcore:name", "name");
    info.type = xml_utils::AttrAny(root, "adtcore:type", "type");
    info.uri = uri;
    info.description = xml_utils::AttrAny(root, "adtcore:description", "description");
    info.source_uri = xml_utils::AttrAny(root, "abapsource:sourceUri", "sourceUri");
    info.version = xml_utils::AttrAny(root, "adtcore:version", "version");
    info.language = xml_utils::AttrAny(root, "adtcore:language", "language");
    info.responsible = xml_utils::AttrAny(root, "adtcore:responsible", "responsible");
    info.changed_by = xml_utils::AttrAny(root, "adtcore:changedBy", "changedBy");
    info.changed_at = xml_utils::AttrAny(root, "adtcore:changedAt", "changedAt");
    info.created_at = xml_utils::AttrAny(root, "adtcore:createdAt", "createdAt");

    // Parse includes (e.g. class:include elements).
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        // Match elements that are includes (various namespace prefixes).
        std::string el_name = el->Name() ? el->Name() : "";
        if (el_name.find("include") == std::string::npos &&
            el_name.find("Include") == std::string::npos) {
            continue;
        }

        ObjectInclude inc;
        inc.name = xml_utils::AttrAny(el, "adtcore:name", "name");
        inc.type = xml_utils::AttrAny(el, "adtcore:type", "type");
        inc.include_type = xml_utils::AttrAny(el, "class:includeType", "includeType");
        inc.source_uri = xml_utils::AttrAny(el, "abapsource:sourceUri", "sourceUri");

        if (!inc.name.empty()) {
            structure.includes.push_back(std::move(inc));
        }
    }

    return Result<ObjectStructure, Error>::Ok(std::move(structure));
}

// -- Object type → creation path mapping ------------------------------------

struct ObjectTypeInfo {
    const char* type_id;
    const char* creation_path;
    const char* root_element;
    const char* xml_namespace;
    const char* ns_prefix;
};

// Table from protocol spec §10.3.
const ObjectTypeInfo kObjectTypes[] = {
    {"PROG/P",   "programs/programs",     "program:abapProgram",        "http://www.sap.com/adt/programs/programs",     "program"},
    {"CLAS/OC",  "oo/classes",            "class:abapClass",            "http://www.sap.com/adt/oo/classes",            "class"},
    {"INTF/OI",  "oo/interfaces",         "intf:abapInterface",         "http://www.sap.com/adt/oo/interfaces",         "intf"},
    {"PROG/I",   "programs/includes",     "include:abapInclude",        "http://www.sap.com/adt/programs/includes",     "include"},
    {"FUGR/F",   "functions/groups",      "group:abapFunctionGroup",    "http://www.sap.com/adt/functions/groups",      "group"},
    {"DEVC/K",   "packages",             "pak:package",                "http://www.sap.com/adt/packages",              "pak"},
    {"DDLS/DF",  "ddic/ddl/sources",      "ddl:ddlSource",              "http://www.sap.com/adt/ddic/ddl/sources",      "ddl"},
    {"TABL/DT",  "ddic/tables",          "blue:blueSource",            "http://www.sap.com/adt/ddic/tables",           "blue"},
    {"DTEL/DE",  "ddic/dataelements",    "blue:wbobj",                 "http://www.sap.com/adt/ddic/dataelements",     "blue"},
    {"MSAG/N",   "messageclass",         "mc:messageClass",            "http://www.sap.com/adt/messageclass",          "mc"},
};

const ObjectTypeInfo* FindObjectType(const std::string& type_id) {
    for (const auto& t : kObjectTypes) {
        if (t.type_id == type_id) return &t;
    }
    return nullptr;
}

std::string BuildCreateXml(const CreateObjectParams& params,
                           const ObjectTypeInfo& type_info) {
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());

    auto* root = doc.NewElement(type_info.root_element);
    root->SetAttribute(
        (std::string("xmlns:") + type_info.ns_prefix).c_str(),
        type_info.xml_namespace);
    root->SetAttribute("xmlns:adtcore", "http://www.sap.com/adt/core");
    root->SetAttribute("adtcore:description", params.description.c_str());
    root->SetAttribute("adtcore:name", params.name.c_str());
    root->SetAttribute("adtcore:type", params.object_type.c_str());
    if (params.responsible) {
        root->SetAttribute("adtcore:responsible", params.responsible->c_str());
    }

    auto* pkg_ref = doc.NewElement("adtcore:packageRef");
    pkg_ref->SetAttribute("adtcore:name", params.package_name.c_str());
    root->InsertEndChild(pkg_ref);

    doc.InsertEndChild(root);

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return printer.CStr();
}

} // anonymous namespace

Result<ObjectStructure, Error> GetObjectStructure(
    IAdtSession& session,
    const ObjectUri& uri) {
    auto response = session.Get(uri.Value());
    if (response.IsErr()) {
        return Result<ObjectStructure, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<ObjectStructure, Error>::Err(
            Error::FromHttpStatus("GetObjectStructure", uri.Value(), http.status_code, http.body));
    }

    return ParseObjectStructure(http.body, uri.Value());
}

// ---------------------------------------------------------------------------
// CreateObject
// ---------------------------------------------------------------------------
Result<ObjectUri, Error> CreateObject(
    IAdtSession& session,
    const CreateObjectParams& params) {
    const auto* type_info = FindObjectType(params.object_type);
    if (!type_info) {
        return Result<ObjectUri, Error>::Err(Error{
            "CreateObject", "", std::nullopt,
            "Unknown object type: " + params.object_type, std::nullopt});
    }

    std::string url = "/sap/bc/adt/" + std::string(type_info->creation_path);
    if (params.transport_number) {
        url += "?corrNr=" + *params.transport_number;
    }

    auto body = BuildCreateXml(params, *type_info);

    auto response = session.Post(url, body, "application/*");
    if (response.IsErr()) {
        return Result<ObjectUri, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<ObjectUri, Error>::Err(
            Error::FromHttpStatus("CreateObject", url, http.status_code, http.body));
    }

    // Parse response to extract the created object's URI.
    tinyxml2::XMLDocument doc;
    if (doc.Parse(http.body.data(), http.body.size()) == tinyxml2::XML_SUCCESS) {
        auto* root = doc.RootElement();
        if (root) {
            auto uri_str = xml_utils::AttrAny(root, "adtcore:uri", "uri");
            if (!uri_str.empty()) {
                auto uri = ObjectUri::Create(uri_str);
                if (uri.IsOk()) {
                    return Result<ObjectUri, Error>::Ok(std::move(uri).Value());
                }
            }
        }
    }

    // Fallback: construct URI from type creation path + lowercase name.
    std::string name_lower = params.name;
    for (auto& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto fallback_uri = "/sap/bc/adt/" + std::string(type_info->creation_path) + "/" + name_lower;
    auto uri = ObjectUri::Create(fallback_uri);
    if (uri.IsErr()) {
        return Result<ObjectUri, Error>::Err(Error{
            "CreateObject", url, std::nullopt,
            "Failed to construct object URI from response", std::nullopt});
    }
    return Result<ObjectUri, Error>::Ok(std::move(uri).Value());
}

// ---------------------------------------------------------------------------
// DeleteObject
// ---------------------------------------------------------------------------
Result<void, Error> DeleteObject(
    IAdtSession& session,
    const ObjectUri& uri,
    const LockHandle& lock_handle,
    const std::optional<std::string>& transport_number) {
    std::string url = uri.Value() + "?lockHandle=" + lock_handle.Value();
    if (transport_number) {
        url += "&corrNr=" + *transport_number;
    }

    auto response = session.Delete(url);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("DeleteObject", uri.Value(), http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
