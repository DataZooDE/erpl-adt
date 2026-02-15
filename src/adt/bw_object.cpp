#include <erpl_adt/adt/bw_object.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwModelingBase = "/sap/bw/modeling/";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Default content-type versions for known BW object types.
// These match what SAP BW/4HANA systems advertise in discovery.
// The Eclipse BW plugin uses the same approach: hardcoded defaults
// overridable by discovery-resolved content types.
std::string GetDefaultAcceptType(const std::string& tlogo) {
    auto lower = ToLower(tlogo);
    // Object-type versions from the BW content-type catalog
    if (lower == "adso")   return "application/vnd.sap.bw.modeling.adso-v1_2_0+xml";
    if (lower == "iobj")   return "application/xml";  // InfoObject uses plain XML
    if (lower == "hcpr")   return "application/vnd.sap.bw.modeling.hcpr-v1_2_0+xml";
    if (lower == "trfn")   return "application/vnd.sap.bw.modeling.trfn-v1_0_0+xml";
    if (lower == "dtpa")   return "application/vnd.sap.bw.modeling.dtpa-v1_0_0+xml";
    if (lower == "rsds")   return "application/vnd.sap.bw.modeling.rsds+xml";
    if (lower == "lsys")   return "application/vnd.sap.bw.modeling.lsys-v1_1_0+xml";
    if (lower == "query")  return "application/vnd.sap.bw.modeling.query-v1_10_0+xml";
    if (lower == "dest")   return "application/vnd.sap.bw.modeling.dest-v1_0_0+xml";
    if (lower == "fbp")    return "application/vnd.sap.bw.modeling.fbp-v1_0_0+xml";
    if (lower == "dmod")   return "application/vnd.sap.bw.modeling.dmod-v1_0_0+xml";
    if (lower == "trcs")   return "application/vnd.sap.bw.modeling.trcs-v1_0_0+xml";
    if (lower == "doca")   return "application/vnd.sap.bw.modeling.doca-v1_0_0+xml";
    if (lower == "segr")   return "application/vnd.sap.bw.modeling.segr-v1_0_0+xml";
    if (lower == "area")   return "application/vnd.sap.bw.modeling.area-v1_0_0+xml";
    if (lower == "ctrt")   return "application/vnd.sap.bw.modeling.ctrt-v1_0_0+xml";
    if (lower == "uomt")   return "application/vnd.sap.bw.modeling.uomt-v1_0_0+xml";
    if (lower == "thjt")   return "application/vnd.sap.bw.modeling.thjt-v1_0_0+xml";
    // Fallback: unversioned vendor type
    return "application/vnd.sap.bw.modeling." + lower + "+xml";
}

std::string BuildObjectPath(const std::string& type, const std::string& name,
                            const std::string& version = "") {
    std::string path = std::string(kBwModelingBase) + ToLower(type) + "/" + name;
    if (!version.empty()) {
        path += "/" + version;
    }
    return path;
}

std::string BuildObjectPathWithSourceSystem(const std::string& type,
                                            const std::string& name,
                                            const std::string& source_system,
                                            const std::string& version) {
    return std::string(kBwModelingBase) + ToLower(type) + "/" + name +
           "/" + source_system + "/" + version;
}

// Get attribute value, trying both namespaced and plain.
std::string GetAttr(const tinyxml2::XMLElement* el,
                    const char* ns_name, const char* plain_name = nullptr) {
    const char* val = el->Attribute(ns_name);
    if (!val && plain_name) val = el->Attribute(plain_name);
    return val ? val : "";
}

// Get child element text, trying multiple element names.
std::string GetChildText(const tinyxml2::XMLElement* parent, const char* name1,
                         const char* name2 = nullptr) {
    auto* el = parent->FirstChildElement(name1);
    if (!el && name2) el = parent->FirstChildElement(name2);
    if (el && el->GetText()) return el->GetText();
    return "";
}

// Check if an attribute name is namespace noise we should skip.
bool IsNamespaceAttr(const char* name) {
    if (!name) return true;
    std::string s(name);
    return s.find("xmlns") == 0 || s.find("xsi:") == 0;
}

// Known root attributes already extracted into named fields.
bool IsKnownAttr(const char* name) {
    static const char* known[] = {
        "name", "description", "objectDesc", "packageName", "package",
        "changedBy", "lastChangedBy", "changedAt", "lastChangedAt",
        "bwModel:description", nullptr
    };
    for (auto* k = known; *k; ++k) {
        if (std::strcmp(name, *k) == 0) return true;
    }
    return false;
}

Result<BwObjectMetadata, Error> ParseObjectResponse(
    std::string_view xml, const std::string& path,
    const std::string& type, const std::string& name) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwReadObject", path,
            "Failed to parse BW object response XML")) {
        return Result<BwObjectMetadata, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwObjectMetadata, Error>::Err(Error{
            "BwReadObject", path, std::nullopt,
            "Empty BW object response", std::nullopt,
            ErrorCategory::NotFound});
    }

    BwObjectMetadata meta;
    meta.name = name;
    meta.type = type;
    meta.raw_xml = std::string(xml);

    // xsi:type → sub_type
    meta.sub_type = GetAttr(root, "xsi:type");

    // Try common attribute locations
    meta.description = GetAttr(root, "description", "objectDesc");
    if (meta.description.empty()) {
        meta.description = GetAttr(root, "bwModel:description");
    }
    meta.package_name = GetAttr(root, "packageName", "package");
    meta.last_changed_by = GetAttr(root, "changedBy", "lastChangedBy");
    meta.last_changed_at = GetAttr(root, "changedAt", "lastChangedAt");

    // Short/long descriptions from child elements
    meta.short_description = GetChildText(root, "shortDescription");
    meta.long_description = GetChildText(root, "longDescription");

    // tlogoProperties — present on most BW object types
    auto* tlp = root->FirstChildElement("tlogoProperties");
    if (tlp) {
        meta.responsible = GetChildText(tlp, "adtcore:responsible", "responsible");
        meta.created_at = GetChildText(tlp, "adtcore:createdAt", "createdAt");
        meta.language = GetChildText(tlp, "adtcore:language", "language");
        meta.info_area = GetChildText(tlp, "infoArea");
        meta.status = GetChildText(tlp, "objectStatus");
        meta.content_state = GetChildText(tlp, "contentState");

        // Override changed-by/at from tlogoProperties if root attrs were empty
        if (meta.last_changed_by.empty()) {
            meta.last_changed_by = GetChildText(tlp, "adtcore:changedBy", "changedBy");
        }
        if (meta.last_changed_at.empty()) {
            meta.last_changed_at = GetChildText(tlp, "adtcore:changedAt", "changedAt");
        }
    }

    // Collect interesting root attributes into properties map
    for (auto* attr = root->FirstAttribute(); attr; attr = attr->Next()) {
        const char* attr_name = attr->Name();
        if (IsNamespaceAttr(attr_name) || IsKnownAttr(attr_name)) continue;
        meta.properties[attr_name] = attr->Value();
    }

    // Collect key child element text values into properties
    // (type-specific elements like infoObjectType, dataType)
    static const char* child_props[] = {
        "infoObjectType", "dataType", "aggregationType",
        "compounding", nullptr
    };
    for (auto* cp = child_props; *cp; ++cp) {
        auto val = GetChildText(root, *cp);
        if (!val.empty()) {
            meta.properties[*cp] = val;
        }
    }

    return Result<BwObjectMetadata, Error>::Ok(std::move(meta));
}

Result<BwLockResult, Error> ParseLockResponse(
    std::string_view xml, const HttpHeaders& response_headers,
    const std::string& path) {
    // Lock response is unusual: flat XML elements without a root wrapper.
    // Wrap in a synthetic root for parsing.
    std::string wrapped = "<root>" + std::string(xml) + "</root>";

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, wrapped, "BwLockObject", path,
            "Failed to parse BW lock response XML",
            ErrorCategory::LockConflict)) {
        return Result<BwLockResult, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", path, std::nullopt,
            "Empty BW lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    auto get_text = [&](const char* name) -> std::string {
        auto* el = root->FirstChildElement(name);
        if (el && el->GetText()) return el->GetText();
        return "";
    };

    BwLockResult result;
    result.lock_handle = get_text("LOCK_HANDLE");
    result.transport_number = get_text("CORRNR");
    result.transport_text = get_text("CORRTEXT");
    result.transport_owner = get_text("CORRUSER");
    result.is_local = (get_text("IS_LOCAL") == "X");

    if (result.lock_handle.empty()) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", path, std::nullopt,
            "Empty LOCK_HANDLE in BW lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    // Extract headers
    auto timestamp = adt_utils::FindHeaderValueCi(response_headers,
                                                  "timestamp");
    if (timestamp.has_value()) {
        result.timestamp = *timestamp;
    }
    auto package_name = adt_utils::FindHeaderValueCi(response_headers,
                                                     "Development-Class");
    if (package_name.has_value()) {
        result.package_name = *package_name;
    }

    return Result<BwLockResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwReadObject
// ---------------------------------------------------------------------------
Result<BwObjectMetadata, Error> BwReadObject(
    IAdtSession& session,
    const BwReadOptions& options) {
    bool has_uri = options.uri.has_value() && !options.uri->empty();

    if (!has_uri) {
        if (options.object_type.empty()) {
            return Result<BwObjectMetadata, Error>::Err(Error{
                "BwReadObject", "", std::nullopt,
                "Object type must not be empty", std::nullopt});
        }
        if (options.object_name.empty()) {
            return Result<BwObjectMetadata, Error>::Err(Error{
                "BwReadObject", "", std::nullopt,
                "Object name must not be empty", std::nullopt});
        }
    }

    std::string path;
    if (has_uri) {
        path = *options.uri;
    } else if (options.source_system.has_value()) {
        path = BuildObjectPathWithSourceSystem(
            options.object_type, options.object_name,
            *options.source_system, options.version);
    } else {
        path = BuildObjectPath(options.object_type, options.object_name,
                               options.version);
    }

    HttpHeaders headers;
    if (options.content_type.has_value() && !options.content_type->empty()) {
        headers["Accept"] = *options.content_type;
    } else if (options.object_type.empty()) {
        headers["Accept"] = "application/xml";
    } else {
        headers["Accept"] = GetDefaultAcceptType(options.object_type);
    }

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<BwObjectMetadata, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<BwObjectMetadata, Error>::Err(Error{
            "BwReadObject", path, 404,
            "BW object not found: " + options.object_type + " " + options.object_name,
            std::nullopt, ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwReadObject", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwObjectMetadata, Error>::Err(std::move(error));
    }

    if (options.raw) {
        BwObjectMetadata meta;
        meta.name = options.object_name;
        meta.type = options.object_type;
        meta.version = options.version;
        meta.raw_xml = http.body;
        return Result<BwObjectMetadata, Error>::Ok(std::move(meta));
    }

    auto result = ParseObjectResponse(http.body, path,
                                       options.object_type, options.object_name);
    if (result.IsOk()) {
        auto& meta = const_cast<BwObjectMetadata&>(result.Value());
        meta.version = options.version;
    }
    return result;
}

// ---------------------------------------------------------------------------
// BwLockObject
// ---------------------------------------------------------------------------
Result<BwLockResult, Error> BwLockObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& activity) {
    auto path = BuildObjectPath(object_type, object_name);
    auto lock_url = path + "?action=lock";

    HttpHeaders headers;
    if (activity != "CHAN") {
        headers["activity_context"] = activity;
    }

    auto response = session.Post(lock_url, "", "application/xml", headers);
    if (response.IsErr()) {
        return Result<BwLockResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 409 || http.status_code == 423) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", path, http.status_code,
            "Object is locked by another user", std::nullopt,
            ErrorCategory::LockConflict});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwLockObject", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwLockResult, Error>::Err(std::move(error));
    }

    return ParseLockResponse(http.body, http.headers, path);
}

// ---------------------------------------------------------------------------
// BwUnlockObject
// ---------------------------------------------------------------------------
Result<void, Error> BwUnlockObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name) {
    auto path = BuildObjectPath(object_type, object_name);
    auto unlock_url = path + "?action=unlock";

    auto response = session.Post(unlock_url, "", "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwUnlockObject", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// BwSaveObject
// ---------------------------------------------------------------------------
Result<void, Error> BwSaveObject(
    IAdtSession& session,
    const BwSaveOptions& options) {
    if (options.lock_handle.empty()) {
        return Result<void, Error>::Err(Error{
            "BwSaveObject", "", std::nullopt,
            "Lock handle must not be empty", std::nullopt});
    }

    auto path = BuildObjectPath(options.object_type, options.object_name);
    auto save_url = path + "?lockHandle=" + options.lock_handle;
    if (!options.transport.empty()) {
        save_url += "&corrNr=" + options.transport;
    }
    if (!options.timestamp.empty()) {
        save_url += "&timestamp=" + options.timestamp;
    }

    auto ct = (options.content_type.has_value() && !options.content_type->empty())
        ? *options.content_type
        : GetDefaultAcceptType(options.object_type);

    auto response = session.Put(save_url, options.content, ct);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwSaveObject", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// BwDeleteObject
// ---------------------------------------------------------------------------
Result<void, Error> BwDeleteObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& lock_handle,
    const std::string& transport) {
    auto path = BuildObjectPath(object_type, object_name);
    auto delete_url = path + "?lockHandle=" + lock_handle;
    if (!transport.empty()) {
        delete_url += "&corrNr=" + transport;
    }

    auto response = session.Delete(delete_url);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwDeleteObject", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
