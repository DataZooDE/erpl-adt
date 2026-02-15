#include <erpl_adt/adt/bw_system.hpp>

#include "adt_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kDbInfoPath     = "/sap/bw/modeling/repo/is/dbinfo";
const char* kSystemInfoPath = "/sap/bw/modeling/repo/is/systeminfo";
const char* kChgInfoPath    = "/sap/bw/modeling/repo/is/chginfo";
const char* kAdtUriPath     = "/sap/bw/modeling/repo/is/adturi";

// Try both namespaced and plain attribute names.
std::string GetAttr(const tinyxml2::XMLElement* el,
                    const char* ns_name, const char* plain_name) {
    const char* val = el->Attribute(ns_name);
    if (!val) val = el->Attribute(plain_name);
    return val ? val : "";
}

// Find child element by suffix, return an attribute value from it.
std::string ElementAttr(const tinyxml2::XMLElement* parent,
                        const char* child_suffix, const char* attr_name) {
    for (auto* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const char* name = child->Name();
        if (!name) continue;
        std::string n(name);
        if (n == child_suffix || n.find(std::string(":") + child_suffix) != std::string::npos) {
            const char* val = child->Attribute(attr_name);
            return val ? val : "";
        }
    }
    return "";
}

std::string ElementText(const tinyxml2::XMLElement* parent, const char* child_suffix) {
    for (auto* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const char* name = child->Name();
        if (!name) continue;
        std::string n(name);
        if (n == child_suffix || n.find(std::string(":") + child_suffix) != std::string::npos) {
            return child->GetText() ? child->GetText() : "";
        }
    }
    return "";
}

Result<std::string, Error> FetchAtom(IAdtSession& session,
                                      const char* path,
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwGetDbInfo
// ---------------------------------------------------------------------------
Result<BwDbInfo, Error> BwGetDbInfo(IAdtSession& session) {
    auto xml_result = FetchAtom(session, kDbInfoPath, "BwGetDbInfo");
    if (xml_result.IsErr()) {
        return Result<BwDbInfo, Error>::Err(std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    const auto& xml = xml_result.Value();
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetDbInfo", kDbInfoPath,
            "Failed to parse database info XML")) {
        return Result<BwDbInfo, Error>::Err(std::move(*parse_error));
    }

    BwDbInfo info;
    auto* root = doc.RootElement();
    if (root) {
        // Atom feed: look for entry or content elements with db attributes
        // Try direct attributes first (flat response)
        info.host = GetAttr(root, "dbHost", "host");
        info.port = GetAttr(root, "dbPort", "port");
        info.schema = GetAttr(root, "dbSchema", "schema");
        info.database_type = GetAttr(root, "dbType", "databaseType");

        // Also check Atom entry/content pattern
        for (auto* entry = root->FirstChildElement(); entry;
             entry = entry->NextSiblingElement()) {
            const char* name = entry->Name();
            if (!name) continue;
            std::string n(name);
            if (n == "entry" || n.find(":entry") != std::string::npos) {
                for (auto* child = entry->FirstChildElement(); child;
                     child = child->NextSiblingElement()) {
                    const char* cn = child->Name();
                    if (!cn) continue;
                    std::string cs(cn);
                    if (cs == "content" || cs.find(":content") != std::string::npos) {
                        auto* props = child->FirstChildElement();
                        if (props) {
                            // Try attributes on properties element
                            if (info.host.empty())
                                info.host = GetAttr(props, "dbHost", "host");
                            if (info.port.empty())
                                info.port = GetAttr(props, "dbPort", "port");
                            if (info.schema.empty())
                                info.schema = GetAttr(props, "dbSchema", "schema");
                            if (info.database_type.empty())
                                info.database_type = GetAttr(props, "dbType", "databaseType");

                            // OData child-element text (e.g., <d:dbHost>val</d:dbHost>)
                            if (info.host.empty())
                                info.host = ElementText(props, "dbHost");
                            if (info.host.empty())
                                info.host = ElementText(props, "host");
                            if (info.port.empty())
                                info.port = ElementText(props, "dbPort");
                            if (info.port.empty())
                                info.port = ElementText(props, "port");
                            if (info.schema.empty())
                                info.schema = ElementText(props, "dbSchema");
                            if (info.schema.empty())
                                info.schema = ElementText(props, "schema");
                            if (info.database_type.empty())
                                info.database_type = ElementText(props, "dbType");
                            if (info.database_type.empty())
                                info.database_type = ElementText(props, "databaseType");
                            if (info.database_type.empty())
                                info.database_type = ElementText(props, "type");

                            // Real SAP format: host/port/instance/user are
                            // attrs on <dbInfo:connect>
                            if (info.host.empty())
                                info.host = ElementAttr(props, "connect", "host");
                            if (info.port.empty())
                                info.port = ElementAttr(props, "connect", "port");
                            if (info.instance.empty())
                                info.instance = ElementAttr(props, "connect", "instance");
                            if (info.user.empty())
                                info.user = ElementAttr(props, "connect", "user");

                            // Additional fields
                            if (info.database_name.empty())
                                info.database_name = ElementText(props, "name");
                            if (info.version.empty())
                                info.version = ElementAttr(props, "version", "server");
                            if (info.patchlevel.empty())
                                info.patchlevel = ElementText(props, "patchlevel");
                        }
                    }
                }
            }
        }

        // Fallback: child element text directly on root (non-Atom format)
        if (info.host.empty())
            info.host = ElementText(root, "dbHost");
        if (info.host.empty())
            info.host = ElementText(root, "host");
        if (info.port.empty())
            info.port = ElementText(root, "dbPort");
        if (info.port.empty())
            info.port = ElementText(root, "port");
        if (info.schema.empty())
            info.schema = ElementText(root, "dbSchema");
        if (info.schema.empty())
            info.schema = ElementText(root, "schema");
        if (info.database_type.empty())
            info.database_type = ElementText(root, "dbType");
        if (info.database_type.empty())
            info.database_type = ElementText(root, "databaseType");
        if (info.database_type.empty())
            info.database_type = ElementText(root, "type");

        // Root-level connect element attrs
        if (info.host.empty())
            info.host = ElementAttr(root, "connect", "host");
        if (info.port.empty())
            info.port = ElementAttr(root, "connect", "port");
        if (info.instance.empty())
            info.instance = ElementAttr(root, "connect", "instance");
        if (info.user.empty())
            info.user = ElementAttr(root, "connect", "user");

        // Root-level additional fields
        if (info.database_name.empty())
            info.database_name = ElementText(root, "name");
        if (info.version.empty())
            info.version = ElementAttr(root, "version", "server");
        if (info.patchlevel.empty())
            info.patchlevel = ElementText(root, "patchlevel");
    }

    return Result<BwDbInfo, Error>::Ok(std::move(info));
}

// ---------------------------------------------------------------------------
// BwGetSystemInfo
// ---------------------------------------------------------------------------
Result<std::vector<BwSystemProperty>, Error> BwGetSystemInfo(
    IAdtSession& session) {
    auto xml_result = FetchAtom(session, kSystemInfoPath, "BwGetSystemInfo");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwSystemProperty>, Error>::Err(
            std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    const auto& xml = xml_result.Value();
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetSystemInfo", kSystemInfoPath,
            "Failed to parse system info XML")) {
        return Result<std::vector<BwSystemProperty>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwSystemProperty> properties;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* entry = root->FirstChildElement(); entry;
             entry = entry->NextSiblingElement()) {
            const char* name = entry->Name();
            if (!name) continue;
            std::string n(name);

            // Check for entry elements in Atom feed
            if (n == "entry" || n.find(":entry") != std::string::npos) {
                BwSystemProperty prop;
                prop.description = ElementText(entry, "title");

                for (auto* child = entry->FirstChildElement(); child;
                     child = child->NextSiblingElement()) {
                    const char* cn = child->Name();
                    if (!cn) continue;
                    std::string cs(cn);
                    if (cs == "content" || cs.find(":content") != std::string::npos) {
                        auto* props = child->FirstChildElement();
                        if (props) {
                            prop.key = GetAttr(props, "key", "name");
                            prop.value = GetAttr(props, "value", "val");
                            if (prop.description.empty()) {
                                prop.description = GetAttr(props, "description", "desc");
                            }
                            // OData child-element text fallback
                            if (prop.key.empty())
                                prop.key = ElementText(props, "key");
                            if (prop.key.empty())
                                prop.key = ElementText(props, "name");
                            if (prop.value.empty())
                                prop.value = ElementText(props, "value");
                            if (prop.value.empty())
                                prop.value = ElementText(props, "val");
                            if (prop.description.empty())
                                prop.description = ElementText(props, "description");
                            if (prop.description.empty())
                                prop.description = ElementText(props, "desc");
                        }
                    }
                }
                if (!prop.key.empty()) {
                    properties.push_back(std::move(prop));
                }
            }
            // Also handle flat property elements
            else if (n == "property" || n.find(":property") != std::string::npos) {
                BwSystemProperty prop;
                prop.key = GetAttr(entry, "key", "name");
                prop.value = GetAttr(entry, "value", "val");
                prop.description = GetAttr(entry, "description", "desc");
                if (entry->GetText() && prop.value.empty()) {
                    prop.value = entry->GetText();
                }
                if (!prop.key.empty()) {
                    properties.push_back(std::move(prop));
                }
            }
        }
    }

    return Result<std::vector<BwSystemProperty>, Error>::Ok(std::move(properties));
}

// ---------------------------------------------------------------------------
// BwGetChangeability
// ---------------------------------------------------------------------------
Result<std::vector<BwChangeabilityEntry>, Error> BwGetChangeability(
    IAdtSession& session) {
    auto xml_result = FetchAtom(session, kChgInfoPath, "BwGetChangeability");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwChangeabilityEntry>, Error>::Err(
            std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    const auto& xml = xml_result.Value();
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetChangeability", kChgInfoPath,
            "Failed to parse changeability XML")) {
        return Result<std::vector<BwChangeabilityEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwChangeabilityEntry> entries;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* entry = root->FirstChildElement(); entry;
             entry = entry->NextSiblingElement()) {
            const char* name = entry->Name();
            if (!name) continue;
            std::string n(name);

            if (n == "entry" || n.find(":entry") != std::string::npos) {
                BwChangeabilityEntry e;
                e.description = ElementText(entry, "title");

                for (auto* child = entry->FirstChildElement(); child;
                     child = child->NextSiblingElement()) {
                    const char* cn = child->Name();
                    if (!cn) continue;
                    std::string cs(cn);
                    if (cs == "content" || cs.find(":content") != std::string::npos) {
                        auto* props = child->FirstChildElement();
                        if (props) {
                            e.object_type = GetAttr(props, "objectType", "tlogo");
                            e.changeable = GetAttr(props, "changeable", "changeability");
                            e.transportable = GetAttr(props, "transportable", "transport");
                            if (e.description.empty()) {
                                e.description = GetAttr(props, "description", "desc");
                            }
                            // OData child-element text fallback
                            if (e.object_type.empty())
                                e.object_type = ElementText(props, "objectType");
                            if (e.object_type.empty())
                                e.object_type = ElementText(props, "tlogo");
                            if (e.changeable.empty())
                                e.changeable = ElementText(props, "changeable");
                            if (e.changeable.empty())
                                e.changeable = ElementText(props, "changeability");
                            if (e.transportable.empty())
                                e.transportable = ElementText(props, "transportable");
                            if (e.transportable.empty())
                                e.transportable = ElementText(props, "transport");
                            if (e.description.empty())
                                e.description = ElementText(props, "description");
                            if (e.description.empty())
                                e.description = ElementText(props, "desc");
                        }
                    }
                }
                if (!e.object_type.empty()) {
                    entries.push_back(std::move(e));
                }
            }
            // Flat element format
            else if (n == "chginfo" || n == "changeability" ||
                     n.find(":chginfo") != std::string::npos) {
                BwChangeabilityEntry e;
                e.object_type = GetAttr(entry, "objectType", "tlogo");
                e.changeable = GetAttr(entry, "changeable", "changeability");
                e.transportable = GetAttr(entry, "transportable", "transport");
                e.description = GetAttr(entry, "description", "desc");
                if (!e.object_type.empty()) {
                    entries.push_back(std::move(e));
                }
            }
        }
    }

    return Result<std::vector<BwChangeabilityEntry>, Error>::Ok(std::move(entries));
}

// ---------------------------------------------------------------------------
// BwGetAdtUriMappings
// ---------------------------------------------------------------------------
Result<std::vector<BwAdtUriMapping>, Error> BwGetAdtUriMappings(
    IAdtSession& session) {
    auto xml_result = FetchAtom(session, kAdtUriPath, "BwGetAdtUriMappings");
    if (xml_result.IsErr()) {
        return Result<std::vector<BwAdtUriMapping>, Error>::Err(
            std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    const auto& xml = xml_result.Value();
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwGetAdtUriMappings", kAdtUriPath,
            "Failed to parse ADT URI mappings XML")) {
        return Result<std::vector<BwAdtUriMapping>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwAdtUriMapping> mappings;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* entry = root->FirstChildElement(); entry;
             entry = entry->NextSiblingElement()) {
            const char* name = entry->Name();
            if (!name) continue;
            std::string n(name);

            if (n == "entry" || n.find(":entry") != std::string::npos) {
                BwAdtUriMapping m;

                for (auto* child = entry->FirstChildElement(); child;
                     child = child->NextSiblingElement()) {
                    const char* cn = child->Name();
                    if (!cn) continue;
                    std::string cs(cn);
                    if (cs == "content" || cs.find(":content") != std::string::npos) {
                        auto* props = child->FirstChildElement();
                        if (props) {
                            m.bw_type = GetAttr(props, "bwType", "bwObjectType");
                            m.adt_type = GetAttr(props, "adtType", "adtObjectType");
                            m.bw_uri_template = GetAttr(props, "bwUri", "bwUriTemplate");
                            m.adt_uri_template = GetAttr(props, "adtUri", "adtUriTemplate");
                            // OData child-element text fallback
                            if (m.bw_type.empty())
                                m.bw_type = ElementText(props, "bwType");
                            if (m.bw_type.empty())
                                m.bw_type = ElementText(props, "bwObjectType");
                            if (m.adt_type.empty())
                                m.adt_type = ElementText(props, "adtType");
                            if (m.adt_type.empty())
                                m.adt_type = ElementText(props, "adtObjectType");
                            if (m.bw_uri_template.empty())
                                m.bw_uri_template = ElementText(props, "bwUri");
                            if (m.bw_uri_template.empty())
                                m.bw_uri_template = ElementText(props, "bwUriTemplate");
                            if (m.adt_uri_template.empty())
                                m.adt_uri_template = ElementText(props, "adtUri");
                            if (m.adt_uri_template.empty())
                                m.adt_uri_template = ElementText(props, "adtUriTemplate");
                        }
                    }
                }
                if (!m.bw_type.empty() || !m.adt_type.empty()) {
                    mappings.push_back(std::move(m));
                }
            }
            // Flat element format
            else if (n == "mapping" || n.find(":mapping") != std::string::npos) {
                BwAdtUriMapping m;
                m.bw_type = GetAttr(entry, "bwType", "bwObjectType");
                m.adt_type = GetAttr(entry, "adtType", "adtObjectType");
                m.bw_uri_template = GetAttr(entry, "bwUri", "bwUriTemplate");
                m.adt_uri_template = GetAttr(entry, "adtUri", "adtUriTemplate");
                if (!m.bw_type.empty() || !m.adt_type.empty()) {
                    mappings.push_back(std::move(m));
                }
            }
        }
    }

    return Result<std::vector<BwAdtUriMapping>, Error>::Ok(std::move(mappings));
}

} // namespace erpl_adt
