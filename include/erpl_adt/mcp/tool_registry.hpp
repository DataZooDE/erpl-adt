#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ToolSchema — JSON Schema for a tool's input parameters.
// ---------------------------------------------------------------------------
struct ToolSchema {
    std::string name;
    std::string description;
    nlohmann::json input_schema;  // JSON Schema object
};

// ---------------------------------------------------------------------------
// ToolResult — result of executing a tool.
// ---------------------------------------------------------------------------
struct ToolResult {
    bool is_error = false;
    nlohmann::json content;  // array of content blocks
};

// A tool handler takes a JSON params object and returns a ToolResult.
using ToolHandler = std::function<ToolResult(const nlohmann::json& params)>;

// ---------------------------------------------------------------------------
// ToolRegistry — registry of MCP tools.
// ---------------------------------------------------------------------------
class ToolRegistry {
public:
    void Register(const std::string& name,
                  const std::string& description,
                  const nlohmann::json& input_schema,
                  ToolHandler handler);

    [[nodiscard]] const std::vector<ToolSchema>& Tools() const noexcept {
        return schemas_;
    }

    [[nodiscard]] bool HasTool(const std::string& name) const;

    [[nodiscard]] ToolResult Execute(const std::string& name,
                                     const nlohmann::json& params) const;

private:
    std::vector<ToolSchema> schemas_;
    std::map<std::string, ToolHandler> handlers_;
};

} // namespace erpl_adt
