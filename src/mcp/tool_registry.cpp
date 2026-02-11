#include <erpl_adt/mcp/tool_registry.hpp>

namespace erpl_adt {

void ToolRegistry::Register(const std::string& name,
                            const std::string& description,
                            const nlohmann::json& input_schema,
                            ToolHandler handler) {
    schemas_.push_back({name, description, input_schema});
    handlers_[name] = std::move(handler);
}

bool ToolRegistry::HasTool(const std::string& name) const {
    return handlers_.count(name) > 0;
}

ToolResult ToolRegistry::Execute(const std::string& name,
                                  const nlohmann::json& params) const {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        return ToolResult{
            true,
            nlohmann::json::array({
                {{"type", "text"}, {"text", "Unknown tool: " + name}}
            })
        };
    }

    try {
        return it->second(params);
    } catch (const std::exception& e) {
        return ToolResult{
            true,
            nlohmann::json::array({
                {{"type", "text"}, {"text", std::string("Tool error: ") + e.what()}}
            })
        };
    }
}

} // namespace erpl_adt
