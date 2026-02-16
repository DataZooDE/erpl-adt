#pragma once

#include <tinyxml2.h>

#include <exception>
#include <string>

namespace erpl_adt::xml_utils {

inline std::string Attr(const tinyxml2::XMLElement* element, const char* name) {
    if (!element || !name) {
        return {};
    }
    const char* value = element->Attribute(name);
    return value ? value : "";
}

inline std::string AttrAny(const tinyxml2::XMLElement* element,
                           const char* first,
                           const char* second) {
    if (!element) {
        return {};
    }
    if (first) {
        const char* value = element->Attribute(first);
        if (value) {
            return value;
        }
    }
    if (second) {
        const char* value = element->Attribute(second);
        if (value) {
            return value;
        }
    }
    return {};
}

inline int ParseIntOrDefault(const std::string& value, int default_value) {
    if (value.empty()) {
        return default_value;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return default_value;
    }
}

inline int AttrIntOr(const tinyxml2::XMLElement* element,
                     const char* name,
                     int default_value) {
    return ParseIntOrDefault(Attr(element, name), default_value);
}

inline int TextIntOr(const tinyxml2::XMLElement* element, int default_value) {
    if (!element || !element->GetText()) {
        return default_value;
    }
    return ParseIntOrDefault(element->GetText(), default_value);
}

} // namespace erpl_adt::xml_utils
