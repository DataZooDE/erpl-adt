#include <erpl_adt/mcp/tool_registry.hpp>

namespace erpl_adt {

void ToolRegistry::Register(const std::string& name,
                            const std::string& description,
                            const nlohmann::json& input_schema,
                            ToolHandler handler) {
    Register(name, description, input_schema, ToolAnnotations{}, "",
             std::move(handler));
}

void ToolRegistry::Register(const std::string& name,
                            const std::string& description,
                            const nlohmann::json& input_schema,
                            ToolAnnotations annotations,
                            const std::string& title,
                            ToolHandler handler,
                            const nlohmann::json& output_schema) {
    ToolSchema schema;
    schema.name = name;
    schema.description = description;
    schema.input_schema = input_schema;
    schema.title = title;
    schema.annotations = annotations;
    schema.output_schema = output_schema;
    schemas_.push_back(std::move(schema));
    handlers_[name] = std::move(handler);
}

bool ToolRegistry::Annotate(const std::string& name, ToolAnnotations annotations,
                            const std::string& title) {
    for (auto& schema : schemas_) {
        if (schema.name == name) {
            schema.annotations = annotations;
            schema.title = title;
            return true;
        }
    }
    return false;
}

bool ToolRegistry::SetOutputSchema(const std::string& name,
                                   const nlohmann::json& output_schema) {
    for (auto& schema : schemas_) {
        if (schema.name == name) {
            schema.output_schema = output_schema;
            return true;
        }
    }
    return false;
}

std::string ToolRegistry::FamilyOf(const std::string& tool_name) {
    const auto underscore = tool_name.find('_');
    return underscore == std::string::npos ? tool_name
                                           : tool_name.substr(0, underscore);
}

size_t ToolRegistry::RetainFamilies(const std::vector<std::string>& families) {
    if (families.empty()) {
        return 0;
    }
    const auto wanted = [&](const std::string& name) {
        const auto family = FamilyOf(name);
        for (const auto& f : families) {
            if (f == family) return true;
        }
        return false;
    };

    size_t removed = 0;
    std::vector<ToolSchema> kept;
    kept.reserve(schemas_.size());
    for (auto& schema : schemas_) {
        if (wanted(schema.name)) {
            kept.push_back(std::move(schema));
        } else {
            handlers_.erase(schema.name);
            ++removed;
        }
    }
    schemas_ = std::move(kept);
    return removed;
}

bool ToolRegistry::HasTool(const std::string& name) const {
    return handlers_.count(name) > 0;
}

ToolResult ToolRegistry::Execute(const std::string& name,
                                  const nlohmann::json& params) const {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        ToolResult unknown;
        unknown.is_error = true;
        unknown.content = nlohmann::json::array(
            {{{"type", "text"}, {"text", "Unknown tool: " + name}}});
        return unknown;
    }

    try {
        return it->second(params);
    } catch (const std::exception& e) {
        ToolResult failed;
        failed.is_error = true;
        failed.content = nlohmann::json::array(
            {{{"type", "text"}, {"text", std::string("Tool error: ") + e.what()}}});
        return failed;
    }
}

} // namespace erpl_adt
