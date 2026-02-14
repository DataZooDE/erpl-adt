#include <erpl_adt/adt/bw_object.hpp>

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

Result<BwObjectMetadata, Error> ParseObjectResponse(
    std::string_view xml, const std::string& path,
    const std::string& type, const std::string& name) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwObjectMetadata, Error>::Err(Error{
            "BwReadObject", path, std::nullopt,
            "Failed to parse BW object response XML", std::nullopt});
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

    // Try common attribute locations
    meta.description = GetAttr(root, "description", "objectDesc");
    if (meta.description.empty()) {
        meta.description = GetAttr(root, "bwModel:description");
    }
    meta.package_name = GetAttr(root, "packageName", "package");
    meta.last_changed_by = GetAttr(root, "changedBy", "lastChangedBy");
    meta.last_changed_at = GetAttr(root, "changedAt", "lastChangedAt");

    return Result<BwObjectMetadata, Error>::Ok(std::move(meta));
}

Result<BwLockResult, Error> ParseLockResponse(
    std::string_view xml, const HttpHeaders& response_headers,
    const std::string& path) {
    // Lock response is unusual: flat XML elements without a root wrapper.
    // Wrap in a synthetic root for parsing.
    std::string wrapped = "<root>" + std::string(xml) + "</root>";

    tinyxml2::XMLDocument doc;
    if (doc.Parse(wrapped.data(), wrapped.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", path, std::nullopt,
            "Failed to parse BW lock response XML", std::nullopt,
            ErrorCategory::LockConflict});
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
    auto ts_it = response_headers.find("timestamp");
    if (ts_it != response_headers.end()) {
        result.timestamp = ts_it->second;
    }
    auto pkg_it = response_headers.find("Development-Class");
    if (pkg_it == response_headers.end()) {
        pkg_it = response_headers.find("development-class");
    }
    if (pkg_it != response_headers.end()) {
        result.package_name = pkg_it->second;
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

    std::string path;
    if (options.source_system.has_value()) {
        path = BuildObjectPathWithSourceSystem(
            options.object_type, options.object_name,
            *options.source_system, options.version);
    } else {
        path = BuildObjectPath(options.object_type, options.object_name,
                               options.version);
    }

    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.bw.modeling." +
                        ToLower(options.object_type) + "+xml";

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
        return Result<BwObjectMetadata, Error>::Err(
            Error::FromHttpStatus("BwReadObject", path, http.status_code, http.body));
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
        return Result<BwLockResult, Error>::Err(
            Error::FromHttpStatus("BwLockObject", path, http.status_code, http.body));
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
        return Result<void, Error>::Err(
            Error::FromHttpStatus("BwUnlockObject", path, http.status_code, http.body));
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

    auto content_type = "application/vnd.sap.bw.modeling." +
                        ToLower(options.object_type) + "+xml";

    auto response = session.Put(save_url, options.content, content_type);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("BwSaveObject", path, http.status_code, http.body));
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
        return Result<void, Error>::Err(
            Error::FromHttpStatus("BwDeleteObject", path, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
