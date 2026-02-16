#include <erpl_adt/adt/bw_repo_utils.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>

#include <tinyxml2.h>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace erpl_adt {

namespace {

const char* kSearchMetadataPath = "/sap/bw/modeling/repo/is/bwsearch/metadata";
const char* kBackendFavoritesPath = "/sap/bw/modeling/repo/backendfavorites";
const char* kNodePathPath = "/sap/bw/modeling/repo/nodepath";
const char* kApplicationLogPath = "/sap/bw/modeling/repo/is/applicationlog";
const char* kMessagePath = "/sap/bw/modeling/repo/is/message";

Result<std::string, Error> FetchAtom(IAdtSession& session,
                                     const std::string& path,
                                     const char* operation) {
    HttpHeaders headers;
    headers["Accept"] = "application/atom+xml";

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus(operation, path, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::string, Error>::Err(std::move(error));
    }

    return Result<std::string, Error>::Ok(http.body);
}

std::string AttrOrChild(const tinyxml2::XMLElement* el,
                        std::initializer_list<const char*> attrs,
                        std::initializer_list<const char*> children) {
    if (!el) {
        return "";
    }

    for (const char* attr : attrs) {
        const auto v = xml_utils::Attr(el, attr);
        if (!v.empty()) {
            return v;
        }
    }

    for (const char* child : children) {
        const auto v = atom_parser::ChildTextByLocalName(el, child);
        if (!v.empty()) {
            return v;
        }
    }

    return "";
}

Result<std::vector<BwSearchMetadataEntry>, Error> ParseSearchMetadata(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetSearchMetadata", kSearchMetadataPath,
            "Failed to parse BW search metadata XML")) {
        return Result<std::vector<BwSearchMetadataEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwSearchMetadataEntry> out;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwSearchMetadataEntry>, Error>::Ok(std::move(out));
    }

    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) {
            continue;
        }

        BwSearchMetadataEntry item;
        item.description = atom_parser::ChildTextByLocalName(entry, "title");

        const auto* props = atom_parser::AtomEntryProperties(entry);
        if (props) {
            item.name = AttrOrChild(props,
                {"name", "field", "key", "id"},
                {"name", "field", "key", "id"});
            item.value = AttrOrChild(props,
                {"value", "code", "defaultValue"},
                {"value", "code", "defaultValue"});
            item.category = AttrOrChild(props,
                {"category", "group", "scope"},
                {"category", "group", "scope"});
            if (item.description.empty()) {
                item.description = AttrOrChild(props,
                    {"description", "text", "label"},
                    {"description", "text", "label"});
            }
        }

        if (!item.name.empty() || !item.value.empty() || !item.description.empty()) {
            out.push_back(std::move(item));
        }
    }

    return Result<std::vector<BwSearchMetadataEntry>, Error>::Ok(std::move(out));
}

Result<std::vector<BwFavoriteEntry>, Error> ParseFavorites(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwListBackendFavorites", kBackendFavoritesPath,
            "Failed to parse BW backend favorites XML")) {
        return Result<std::vector<BwFavoriteEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwFavoriteEntry> out;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwFavoriteEntry>, Error>::Ok(std::move(out));
    }

    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) {
            continue;
        }

        BwFavoriteEntry fav;
        fav.description = atom_parser::ChildTextByLocalName(entry, "title");
        fav.uri = atom_parser::ChildTextByLocalName(entry, "id");

        if (const auto* props = atom_parser::AtomEntryProperties(entry)) {
            fav.name = AttrOrChild(props,
                {"objectName", "name", "favoriteName"},
                {"objectName", "name", "favoriteName"});
            fav.type = AttrOrChild(props,
                {"objectType", "type"},
                {"objectType", "type"});
            if (fav.description.empty()) {
                fav.description = AttrOrChild(props,
                    {"objectDesc", "description"},
                    {"objectDesc", "description"});
            }
        }

        if (!fav.name.empty() || !fav.uri.empty()) {
            out.push_back(std::move(fav));
        }
    }

    return Result<std::vector<BwFavoriteEntry>, Error>::Ok(std::move(out));
}

std::vector<BwNodePathEntry> ParseNodePathTree(const tinyxml2::XMLElement* root) {
    std::vector<BwNodePathEntry> out;
    std::function<void(const tinyxml2::XMLElement*)> visit =
        [&](const tinyxml2::XMLElement* el) {
            if (!el) {
                return;
            }

            BwNodePathEntry node;
            node.name = AttrOrChild(el,
                {"name", "objectName", "displayName", "nodeName"},
                {"name", "objectName", "displayName", "nodeName"});
            node.type = AttrOrChild(el,
                {"type", "objectType", "nodeType"},
                {"type", "objectType", "nodeType"});
            node.uri = AttrOrChild(el,
                {"uri", "objectUri", "id"},
                {"uri", "objectUri", "id"});

            if (!node.name.empty() || !node.uri.empty() || !node.type.empty()) {
                out.push_back(std::move(node));
            }

            for (auto* child = el->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                visit(child);
            }
        };

    for (auto* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        visit(child);
    }

    return out;
}

Result<std::vector<BwApplicationLogEntry>, Error> ParseApplicationLog(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetApplicationLog", kApplicationLogPath,
            "Failed to parse BW application log XML")) {
        return Result<std::vector<BwApplicationLogEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwApplicationLogEntry> out;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwApplicationLogEntry>, Error>::Ok(std::move(out));
    }

    for (auto* entry = root->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) {
            continue;
        }

        BwApplicationLogEntry item;
        item.text = atom_parser::ChildTextByLocalName(entry, "title");

        if (const auto* props = atom_parser::AtomEntryProperties(entry)) {
            item.identifier = AttrOrChild(props,
                {"identifier", "id", "logId"},
                {"identifier", "id", "logId"});
            item.user = AttrOrChild(props,
                {"username", "user", "createdBy"},
                {"username", "user", "createdBy"});
            item.timestamp = AttrOrChild(props,
                {"timestamp", "starttimestamp", "createdAt"},
                {"timestamp", "starttimestamp", "createdAt"});
            item.severity = AttrOrChild(props,
                {"severity", "textype", "type"},
                {"severity", "textype", "type"});
            if (item.text.empty()) {
                item.text = AttrOrChild(props,
                    {"text", "message", "description"},
                    {"text", "message", "description"});
            }
        }

        if (!item.identifier.empty() || !item.text.empty()) {
            out.push_back(std::move(item));
        }
    }

    return Result<std::vector<BwApplicationLogEntry>, Error>::Ok(std::move(out));
}

std::string BuildApplicationLogUrl(const BwApplicationLogOptions& options) {
    std::string path = kApplicationLogPath;
    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value()) {
            return;
        }
        path += (has_query ? "&" : "?");
        path += key;
        path += "=";
        path += UrlEncode(*value);
        has_query = true;
    };

    add("username", options.username);
    add("starttimestamp", options.start_timestamp);
    add("endtimestamp", options.end_timestamp);
    return path;
}

std::string BuildMessageUrl(const BwMessageTextOptions& options) {
    std::string path = std::string(kMessagePath) + "/" +
        UrlEncode(options.identifier) + "/" + UrlEncode(options.text_type);

    bool has_query = false;
    auto add = [&](const char* key, const std::optional<std::string>& value) {
        if (!value.has_value()) {
            return;
        }
        path += (has_query ? "&" : "?");
        path += key;
        path += "=";
        path += UrlEncode(*value);
        has_query = true;
    };

    add("msgv1", options.msgv1);
    add("msgv2", options.msgv2);
    add("msgv3", options.msgv3);
    add("msgv4", options.msgv4);
    return path;
}

std::string ParseMessageTextFromXml(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return "";
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return "";
    }

    auto text = AttrOrChild(root,
        {"text", "message", "value"},
        {"text", "message", "value"});
    if (!text.empty()) {
        return text;
    }

    if (const auto* props = atom_parser::AtomEntryProperties(root)) {
        text = AttrOrChild(props,
            {"text", "message", "value"},
            {"text", "message", "value"});
        if (!text.empty()) {
            return text;
        }
    }

    return atom_parser::ChildTextByLocalName(root, "title");
}

}  // namespace

Result<std::vector<BwSearchMetadataEntry>, Error>
BwGetSearchMetadata(IAdtSession& session) {
    auto xml_result = FetchAtom(session, kSearchMetadataPath, "BwGetSearchMetadata");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwSearchMetadataEntry>, Error>::Err(
            std::move(xml_result).Error());
    }
    return ParseSearchMetadata(xml_result.Value());
}

Result<std::vector<BwFavoriteEntry>, Error>
BwListBackendFavorites(IAdtSession& session) {
    auto xml_result = FetchAtom(session, kBackendFavoritesPath, "BwListBackendFavorites");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwFavoriteEntry>, Error>::Err(
            std::move(xml_result).Error());
    }
    return ParseFavorites(xml_result.Value());
}

Result<void, Error> BwDeleteAllBackendFavorites(IAdtSession& session) {
    auto response = session.Delete(kBackendFavoritesPath, HttpHeaders{});
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (!adt_utils::HasStatus(http.status_code, {200, 202, 204})) {
        auto error = Error::FromHttpStatus(
            "BwDeleteAllBackendFavorites", kBackendFavoritesPath,
            http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

Result<std::vector<BwNodePathEntry>, Error>
BwGetNodePath(IAdtSession& session, const std::string& object_uri) {
    if (object_uri.empty()) {
        return Result<std::vector<BwNodePathEntry>, Error>::Err(Error{
            "BwGetNodePath", kNodePathPath, std::nullopt,
            "object_uri must not be empty", std::nullopt});
    }

    const auto path = std::string(kNodePathPath) + "?objectUri=" + UrlEncode(object_uri);
    auto xml_result = FetchAtom(session, path, "BwGetNodePath");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwNodePathEntry>, Error>::Err(
            std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml_result.Value(), "BwGetNodePath", path,
            "Failed to parse BW node path XML")) {
        return Result<std::vector<BwNodePathEntry>, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<BwNodePathEntry>, Error>::Ok({});
    }

    return Result<std::vector<BwNodePathEntry>, Error>::Ok(
        ParseNodePathTree(root));
}

Result<std::vector<BwApplicationLogEntry>, Error>
BwGetApplicationLog(IAdtSession& session, const BwApplicationLogOptions& options) {
    auto path = BuildApplicationLogUrl(options);
    auto xml_result = FetchAtom(session, path, "BwGetApplicationLog");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwApplicationLogEntry>, Error>::Err(
            std::move(xml_result).Error());
    }
    return ParseApplicationLog(xml_result.Value());
}

Result<BwMessageTextResult, Error>
BwGetMessageText(IAdtSession& session, const BwMessageTextOptions& options) {
    if (options.identifier.empty()) {
        return Result<BwMessageTextResult, Error>::Err(Error{
            "BwGetMessageText", kMessagePath, std::nullopt,
            "identifier must not be empty", std::nullopt});
    }
    if (options.text_type.empty()) {
        return Result<BwMessageTextResult, Error>::Err(Error{
            "BwGetMessageText", kMessagePath, std::nullopt,
            "text_type must not be empty", std::nullopt});
    }

    auto path = BuildMessageUrl(options);
    HttpHeaders headers;
    headers["Accept"] = "*/*";

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<BwMessageTextResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwGetMessageText", path,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwMessageTextResult, Error>::Err(std::move(error));
    }

    BwMessageTextResult out;
    out.identifier = options.identifier;
    out.text_type = options.text_type;
    out.raw_response = http.body;

    auto parsed = ParseMessageTextFromXml(http.body);
    out.text = parsed.empty() ? http.body : parsed;

    return Result<BwMessageTextResult, Error>::Ok(std::move(out));
}

}  // namespace erpl_adt
