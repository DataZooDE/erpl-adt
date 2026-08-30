#include <erpl_adt/adt/bw_object.hpp>

#include "atom_parser.hpp"
#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_context_headers.hpp>
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/adt/bw_media_types.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwModelingBase = "/sap/bw/modeling/";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Default media type for a BW object type — shared with the typed readers.
std::string GetDefaultAcceptType(const std::string& tlogo) {
    return BwDefaultMediaType(tlogo);
}

std::string BuildObjectPath(const std::string& type, const std::string& name,
                            const std::string& version = "") {
    std::string path = std::string(kBwModelingBase) + ToLower(type) + "/" + ToLower(name);
    if (!version.empty()) {
        path += "/" + version;
    }
    return path;
}

std::string BuildObjectPathWithSourceSystem(const std::string& type,
                                            const std::string& name,
                                            const std::string& source_system,
                                            const std::string& version) {
    return std::string(kBwModelingBase) + ToLower(type) + "/" + ToLower(name) +
           "/" + source_system + "/" + version;
}

// Get child element text, trying multiple element names.
std::string GetChildText(const tinyxml2::XMLElement* parent, const char* name1,
                         const char* name2 = nullptr) {
    auto text = atom_parser::ChildTextByLocalName(parent, name1);
    if (!text.empty()) {
        return text;
    }
    if (name2 != nullptr) {
        return atom_parser::ChildTextByLocalName(parent, name2);
    }
    return "";
}

std::string BuildLockUrl(const BwLockOptions& options) {
    auto url = BuildObjectPath(options.object_type, options.object_name) + "?action=lock";
    if (options.parent_name.has_value() && !options.parent_name->empty()) {
        url += "&parent_name=" + UrlEncode(*options.parent_name);
    }
    if (options.parent_type.has_value() && !options.parent_type->empty()) {
        url += "&parent_type=" + UrlEncode(*options.parent_type);
    }
    return url;
}

std::string BuildCreateUrl(const BwCreateOptions& options) {
    std::string url = BuildObjectPath(options.object_type, options.object_name);
    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value() || value->empty()) {
            return;
        }
        url += (has_query ? "&" : "?");
        url += key;
        url += "=";
        url += UrlEncode(*value);
        has_query = true;
    };

    // "package" is deliberately absent: it is not among the query parameters
    // the discovery template advertises, and the backend takes the package
    // from <adtcore:packageRef> in the body instead.
    add("copyFromObjectName", options.copy_from_name);
    add("copyFromObjectType", options.copy_from_type);
    return url;
}

// True when `c` can appear inside a BW object name, and therefore means a
// match that ends (or starts) next to it is part of a longer name.
bool IsNameChar(char c) {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

// Replace whole-name occurrences of `from` with `to`. Whole-name matters:
// copying 0CALMONTH renamed the referenced 0CALMONTH2 as well when this was a
// plain substring replace, and the copy then failed activation with
// "Attribute ...2 not (actively) available" — a corrupted reference to an
// object that has nothing to do with the copy.
std::string ReplaceAll(std::string haystack, const std::string& from,
                       const std::string& to) {
    if (from.empty()) {
        return haystack;
    }
    std::string out;
    out.reserve(haystack.size());
    size_t pos = 0;
    for (;;) {
        auto hit = haystack.find(from, pos);
        if (hit == std::string::npos) {
            out.append(haystack, pos, std::string::npos);
            return out;
        }
        const bool left_ok = hit == 0 || !IsNameChar(haystack[hit - 1]);
        const auto after = hit + from.size();
        const bool right_ok =
            after >= haystack.size() || !IsNameChar(haystack[after]);

        out.append(haystack, pos, hit - pos);
        if (left_ok && right_ok) {
            out += to;
        } else {
            out.append(haystack, hit, from.size());
        }
        pos = after;
    }
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

// Rewrite a copied object's XML to carry the new name.  BW spells the name
// upper-case in attributes and lower-case in the self URIs, so both forms are
// rewritten.
std::string RenameObjectXml(const std::string& xml, const std::string& from,
                            const std::string& to) {
    auto out = ReplaceAll(xml, ToUpper(from), ToUpper(to));
    return ReplaceAll(out, ToLower(from), ToLower(to));
}

// Fetch the source object of a copy-create.  The copyFrom* query parameters
// alone create an empty shell, so the source definition is read here and sent
// as the request body.  Active version first, inactive as fallback.
Result<std::string, Error> FetchCopySource(IAdtSession& session,
                                           const std::string& type,
                                           const std::string& name,
                                           const std::string& media_type) {
    HttpHeaders headers;
    headers["Accept"] = media_type;

    Error last{"BwCreateObject", "", std::nullopt,
               "Copy source not found: " + type + " " + name, std::nullopt,
               ErrorCategory::NotFound};
    for (const char* version : {"a", "m"}) {
        auto path = BuildObjectPath(type, name, version);
        auto response = session.Get(path, headers);
        if (response.IsErr()) {
            last = std::move(response).Error();
            continue;
        }
        const auto& http = response.Value();
        if (http.status_code == 200 && !http.body.empty()) {
            return Result<std::string, Error>::Ok(http.body);
        }
        last = Error::FromHttpStatus("BwCreateObject", path, http.status_code,
                                     http.body);
        if (http.status_code == 404) {
            // Naming only the last version tried ("Version 'M' ... does not
            // exist") reads like a version problem when the object is simply
            // not there at all.
            last.message = "Copy source not found: " + type + " " + name;
        }
    }
    AddBwHint(last);
    return Result<std::string, Error>::Err(std::move(last));
}

// Put the target package into the body.  BW takes the package from
// <adtcore:packageRef>, not from a query parameter.
std::string ApplyPackageRef(const std::string& xml, const std::string& package) {
    const std::string ref =
        "<adtcore:packageRef adtcore:name=\"" + package + "\" adtcore:type=\"DEVC/K\"/>";

    auto open = xml.find("<adtcore:packageRef");
    if (open != std::string::npos) {
        auto close = xml.find('>', open);
        if (close == std::string::npos) {
            return xml;
        }
        // Self-closing element: replace it wholesale.
        return xml.substr(0, open) + ref + xml.substr(close + 1);
    }

    // No packageRef yet — insert one as the first child of <tlogoProperties>.
    auto props = xml.find("<tlogoProperties");
    if (props == std::string::npos) {
        return xml;
    }
    auto close = xml.find('>', props);
    if (close == std::string::npos) {
        return xml;
    }
    return xml.substr(0, close + 1) + ref + xml.substr(close + 1);
}

// Minimal creatable body for object types we know the shape of.  Verified
// against a4h: without schemaVersion the backend answers HTTP 500
// "Attribute 'schemaVersion' expected".
Result<std::string, Error> BuildMinimalObjectXml(const BwCreateOptions& options) {
    const auto lower_type = ToLower(options.object_type);
    const auto name = ToUpper(options.object_name);

    if (lower_type == "adso") {
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<adso:dataStore xmlns:adso=\"http://www.sap.com/bw/modeling/adso.ecore\""
            " xmlns:adtcore=\"http://www.sap.com/adt/core\""
            " schemaVersion=\"\" name=\"" + name + "\""
            " activateData=\"true\" writeChangelog=\"false\""
            " uniqueDataRecords=\"false\">"
            "<endUserTexts label=\"" + name + "\"/>"
            "<tlogoProperties adtcore:name=\"" + name + "\" adtcore:type=\"ADSO\""
            " adtcore:masterLanguage=\"EN\">"
            "<infoArea>NODESNOTCONNECTED</infoArea>"
            "<objectVersion>M</objectVersion>"
            "</tlogoProperties>"
            "</adso:dataStore>";
        return Result<std::string, Error>::Ok(std::move(xml));
    }

    Error error{"BwCreateObject", "", std::nullopt,
                "Creating " + ToUpper(options.object_type) +
                    " needs a request body", std::nullopt,
                ErrorCategory::Internal};
    error.hint = "Pass the object definition with --file <path>, or copy an "
                 "existing object with --copy-from-name/--copy-from-type.";
    return Result<std::string, Error>::Err(std::move(error));
}

// True when `uri` already addresses a specific version — i.e. its last path
// segment is something other than the bare object name.  Used to decide
// whether a requested version still has to be appended to a URI that came
// from a discovery template.
bool UriHasVersionSegment(const std::string& uri, const std::string& object_name) {
    auto path = uri.substr(0, uri.find('?'));
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return true;
    }
    return ToLower(path.substr(slash + 1)) != ToLower(object_name);
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
    meta.sub_type = xml_utils::Attr(root, "xsi:type");

    // Try common attribute locations
    meta.description = xml_utils::AttrAny(root, "description", "objectDesc");
    if (meta.description.empty()) {
        meta.description = xml_utils::Attr(root, "bwModel:description");
    }
    meta.package_name = xml_utils::AttrAny(root, "packageName", "package");
    meta.last_changed_by = xml_utils::AttrAny(root, "changedBy", "lastChangedBy");
    meta.last_changed_at = xml_utils::AttrAny(root, "changedAt", "lastChangedAt");

    // Short/long descriptions from child elements
    meta.short_description = GetChildText(root, "shortDescription");
    meta.long_description = GetChildText(root, "longDescription");

    // tlogoProperties — present on most BW object types
    const auto* tlp = atom_parser::FirstChildByLocalName(root, "tlogoProperties");
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

// Depth-first search for an element by local name (namespace prefix ignored).
const tinyxml2::XMLElement* FindDescendantByLocalName(
    const tinyxml2::XMLElement* parent, const char* local_name) {
    for (const auto* child = parent->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (atom_parser::LocalName(child->Name()) == local_name) {
            return child;
        }
        if (const auto* found = FindDescendantByLocalName(child, local_name)) {
            return found;
        }
    }
    return nullptr;
}

Result<BwLockResult, Error> ParseLockResponse(
    std::string_view xml, const HttpHeaders& response_headers,
    const std::string& path) {
    // The lock response comes in two shapes: bare <LOCK_HANDLE>... elements
    // with no root, and a full ASXML document
    // (<?xml ...?><asx:abap><asx:values><DATA><LOCK_HANDLE>...).  Wrapping
    // both in a synthetic root keeps one parse path — but the declaration has
    // to come off first, or tinyxml2 rejects it as a misplaced declaration.
    std::string body(xml);
    auto first = body.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (first != std::string::npos) {
        body.erase(0, first);
    }
    if (body.compare(0, 5, "<?xml") == 0) {
        auto decl_end = body.find("?>");
        if (decl_end != std::string::npos) {
            body.erase(0, decl_end + 2);
        }
    }
    std::string wrapped = "<root>" + body + "</root>";

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

    // The ASXML shape nests the values under asx:abap/asx:values/DATA, so a
    // direct-children lookup finds nothing.
    auto get_text = [&](const char* name) -> std::string {
        const auto* el = FindDescendantByLocalName(root, name);
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
    auto foreign_object_locks = adt_utils::FindHeaderValueCi(response_headers,
                                                             "Foreign-Object-Locks");
    if (foreign_object_locks.has_value()) {
        result.foreign_object_locks = *foreign_object_locks;
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
        // A discovery "self" template expands without a {version} segment.
        // Requesting a version must still reach .../{version} — otherwise the
        // backend silently answers with the active version (or 404s) while the
        // output claims the requested one.
        if (!options.version.empty() && !options.object_name.empty() &&
            !UriHasVersionSegment(path, options.object_name)) {
            path += "/" + options.version;
        }
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
        auto error = Error::FromHttpStatus("BwReadObject", path, 404, http.body);
        error.message = "BW object not found: " + options.object_type + " " + options.object_name;
        return Result<BwObjectMetadata, Error>::Err(std::move(error));
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
// BwCreateObject
// ---------------------------------------------------------------------------
Result<BwCreateResult, Error> BwCreateObject(
    IAdtSession& session,
    const BwCreateOptions& options) {
    if (options.object_type.empty()) {
        return Result<BwCreateResult, Error>::Err(Error{
            "BwCreateObject", "", std::nullopt,
            "Object type must not be empty", std::nullopt});
    }
    if (options.object_name.empty()) {
        return Result<BwCreateResult, Error>::Err(Error{
            "BwCreateObject", "", std::nullopt,
            "Object name must not be empty", std::nullopt});
    }

    const auto ct = (options.content_type.has_value() && !options.content_type->empty())
        ? *options.content_type
        : GetDefaultAcceptType(options.object_type);

    // The backend always parses a body: creating with an empty one answers
    // HTTP 500 "Object MODEL <name> not found", and the copyFrom* query
    // parameters do not fill it in.  Build the body here, from the file the
    // caller passed, from the copy source, or from a minimal template.
    std::string body;
    if (options.content.has_value() && !options.content->empty()) {
        body = *options.content;
    } else if (options.copy_from_name.has_value() &&
               !options.copy_from_name->empty()) {
        auto source_type = (options.copy_from_type.has_value() &&
                            !options.copy_from_type->empty())
            ? *options.copy_from_type
            : options.object_type;
        // Read the source with *its* media type, which differs from the
        // target's when copying across types.
        const auto source_ct = (ToLower(source_type) == ToLower(options.object_type))
            ? ct
            : GetDefaultAcceptType(source_type);
        auto copied = FetchCopySource(session, source_type,
                                      *options.copy_from_name, source_ct);
        if (copied.IsErr()) {
            return Result<BwCreateResult, Error>::Err(std::move(copied).Error());
        }
        body = RenameObjectXml(copied.Value(), *options.copy_from_name,
                               options.object_name);
    } else {
        auto templated = BuildMinimalObjectXml(options);
        if (templated.IsErr()) {
            return Result<BwCreateResult, Error>::Err(std::move(templated).Error());
        }
        body = std::move(templated).Value();
    }

    if (options.package_name.has_value() && !options.package_name->empty()) {
        body = ApplyPackageRef(body, *options.package_name);
    }

    const auto create_url = BuildCreateUrl(options);

    // Accept, not just Content-Type: without it the backend sees */* and
    // answers HTTP 415 (issue #41).
    HttpHeaders headers;
    headers["Accept"] = ct;

    auto response = session.Post(create_url, body, ct, headers);
    if (response.IsErr()) {
        return Result<BwCreateResult, Error>::Err(std::move(response).Error());
    }

    // Which verb creates depends on the object type, and the backend tells us
    // which one it wanted. ADSO creates with POST; InfoObject's resource
    // controller implements only get(), so a POST for a name that does not
    // exist yet comes back 404 "Resource IOBJ <name> does not exist" and PUT
    // is the create verb there. A 404 means nothing was created, so retrying
    // the same body with PUT is safe — and this reads the answer off the
    // system instead of hard-coding a verb per type for the 40-odd types
    // whose behaviour we have not measured.
    if (response.Value().status_code == 404) {
        auto put_response = session.Put(create_url, body, ct, headers);
        if (put_response.IsErr()) {
            return Result<BwCreateResult, Error>::Err(
                std::move(put_response).Error());
        }
        if (adt_utils::HasStatus(put_response.Value().status_code,
                                 {200, 201, 202, 204})) {
            response = std::move(put_response);
        }
    }

    const auto& http = response.Value();
    if (!adt_utils::HasStatus(http.status_code, {200, 201, 202, 204})) {
        auto error = Error::FromHttpStatus(
            "BwCreateObject", create_url, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwCreateResult, Error>::Err(std::move(error));
    }

    if (!options.keep_lock) {
        // Creating implicitly locks the new object for the creating session,
        // and that enqueue outlives the session — a later command, delete
        // included, would fail with "is locked by user ...".  Best-effort: the
        // create itself already succeeded.
        (void)BwUnlockObject(session, options.object_type, options.object_name);
    }

    BwCreateResult out;
    out.http_status = http.status_code;
    auto location = adt_utils::FindHeaderValueCi(http.headers, "Location");
    if (location.has_value()) {
        out.uri = *location;
    } else {
        // A created object exists only in its inactive (M) version until it is
        // activated, so point at that rather than at the version-less URI,
        // which 404s.
        out.uri = BuildObjectPath(options.object_type, options.object_name, "m");
    }
    return Result<BwCreateResult, Error>::Ok(std::move(out));
}

// ---------------------------------------------------------------------------
// BwLockObject
// ---------------------------------------------------------------------------
Result<BwLockResult, Error> BwLockObject(
    IAdtSession& session,
    const BwLockOptions& options) {
    if (options.object_type.empty()) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", "", std::nullopt,
            "Object type must not be empty", std::nullopt});
    }
    if (options.object_name.empty()) {
        return Result<BwLockResult, Error>::Err(Error{
            "BwLockObject", "", std::nullopt,
            "Object name must not be empty", std::nullopt});
    }

    auto path = BuildObjectPath(options.object_type, options.object_name);
    auto lock_url = BuildLockUrl(options);

    // The lock/unlock action routes negotiate on the *object's* media type,
    // not a generic one: "application/xml" is read as version 0.0.0 and
    // rejected with "Your BW client is outdated ... client requested -v0_0_0".
    HttpHeaders headers;
    headers["Accept"] = GetDefaultAcceptType(options.object_type);
    if (options.activity != "CHAN") {
        headers["activity_context"] = options.activity;
    }
    ApplyBwContextHeaders(options.context_headers, headers);

    auto response = session.Post(lock_url, "",
                                 GetDefaultAcceptType(options.object_type),
                                 headers);
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
        if (http.status_code == 400 && !error.hint.has_value()) {
            error.hint = "BW lock requires a stateful session. "
                         "Use --session-file to persist session state across commands.";
        }
        return Result<BwLockResult, Error>::Err(std::move(error));
    }

    return ParseLockResponse(http.body, http.headers, path);
}

Result<BwLockResult, Error> BwLockObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& activity) {
    BwLockOptions options;
    options.object_type = object_type;
    options.object_name = object_name;
    options.activity = activity;
    return BwLockObject(session, options);
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

    HttpHeaders headers;
    headers["Accept"] = GetDefaultAcceptType(object_type);

    auto response = session.Post(unlock_url, "",
                                 GetDefaultAcceptType(object_type), headers);
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

    // The version belongs in the path, not in a query parameter: a PUT to
    // /sap/bw/modeling/{tlogo}/{name} answers HTTP 400 "Parameter version
    // could not be found", and ?version=M does not satisfy it either.
    const auto version = options.version.empty() ? std::string("m") : options.version;
    auto path = BuildObjectPath(options.object_type, options.object_name, version);
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

    HttpHeaders headers;
    auto context = options.context_headers;
    if (!options.transport.empty() &&
        (!context.transport_lock_holder.has_value() ||
         context.transport_lock_holder->empty())) {
        context.transport_lock_holder = options.transport;
    }
    headers["Accept"] = ct;
    ApplyBwContextHeaders(context, headers);

    auto response = session.Put(save_url, options.content, ct, headers);
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
    const BwDeleteOptions& options) {
    auto path = BuildObjectPath(options.object_type, options.object_name);
    auto delete_url = path + "?lockHandle=" + options.lock_handle;
    if (!options.transport.empty()) {
        delete_url += "&corrNr=" + options.transport;
    }

    HttpHeaders headers;
    headers["Accept"] = GetDefaultAcceptType(options.object_type);
    auto context = options.context_headers;
    if (!options.transport.empty() &&
        (!context.transport_lock_holder.has_value() ||
         context.transport_lock_holder->empty())) {
        context.transport_lock_holder = options.transport;
    }
    ApplyBwContextHeaders(context, headers);

    auto response = session.Delete(delete_url, headers);
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

Result<void, Error> BwDeleteObject(
    IAdtSession& session,
    const std::string& object_type,
    const std::string& object_name,
    const std::string& lock_handle,
    const std::string& transport) {
    BwDeleteOptions options;
    options.object_type = object_type;
    options.object_name = object_name;
    options.lock_handle = lock_handle;
    options.transport = transport;
    return BwDeleteObject(session, options);
}

} // namespace erpl_adt
