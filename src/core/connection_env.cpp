#include <erpl_adt/core/connection_env.hpp>

#include <cstdlib>

namespace erpl_adt {

std::array<std::string, 2> ConnectionEnvNames(const std::string& setting) {
    return {"ERPL_ADT_" + setting, "SAP_" + setting};
}

std::optional<std::string> ConnectionEnvValue(const std::string& setting) {
    for (const auto& name : ConnectionEnvNames(setting)) {
        const char* value = std::getenv(name.c_str());
        if (value != nullptr && *value != '\0') {
            return std::string(value);
        }
    }
    return std::nullopt;
}

}  // namespace erpl_adt
