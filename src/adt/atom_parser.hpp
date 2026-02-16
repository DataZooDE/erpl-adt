#pragma once

#include <tinyxml2.h>

#include <string>
#include <string_view>

namespace erpl_adt::atom_parser {

inline std::string_view LocalName(std::string_view qname) {
    const auto pos = qname.find(':');
    if (pos == std::string_view::npos) {
        return qname;
    }
    return qname.substr(pos + 1);
}

inline bool HasLocalName(const tinyxml2::XMLElement* element,
                         std::string_view local_name) {
    if (element == nullptr) {
        return false;
    }
    const char* name = element->Name();
    if (name == nullptr) {
        return false;
    }
    return LocalName(name) == local_name;
}

inline const tinyxml2::XMLElement* FirstChildByLocalName(
    const tinyxml2::XMLElement* parent, std::string_view local_name) {
    if (parent == nullptr) {
        return nullptr;
    }
    for (auto* child = parent->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (HasLocalName(child, local_name)) {
            return child;
        }
    }
    return nullptr;
}

inline std::string ChildTextByLocalName(const tinyxml2::XMLElement* parent,
                                        std::string_view local_name) {
    const auto* child = FirstChildByLocalName(parent, local_name);
    if (child == nullptr || child->GetText() == nullptr) {
        return "";
    }
    return child->GetText();
}

inline const tinyxml2::XMLElement* AtomEntryProperties(
    const tinyxml2::XMLElement* entry) {
    const auto* content = FirstChildByLocalName(entry, "content");
    if (content == nullptr) {
        return nullptr;
    }
    return content->FirstChildElement();
}

} // namespace erpl_adt::atom_parser
