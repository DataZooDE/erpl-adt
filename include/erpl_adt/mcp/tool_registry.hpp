#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ToolAnnotations — behavioural hints a host uses to decide how much
// ceremony a call deserves: a confirmation in front of adt_delete_object,
// none in front of adt_search.
//
// These are a UX affordance, not a security boundary — the spec has clients
// treat annotations as untrusted unless the server itself is trusted. The
// controls that actually gate this server are in mcp/http_security.hpp.
// ---------------------------------------------------------------------------
struct ToolAnnotations {
    // Reads nothing, changes nothing on the SAP system.
    bool read_only = false;
    // May destroy or overwrite something a user cares about.
    bool destructive = false;
    // Calling twice with the same arguments has the same effect as once.
    bool idempotent = false;
};

// ---------------------------------------------------------------------------
// ToolSchema — JSON Schema for a tool's input parameters.
// ---------------------------------------------------------------------------
struct ToolSchema {
    std::string name;
    std::string description;
    nlohmann::json input_schema;  // JSON Schema object
    // Human-readable name for a tool picker. Empty means "use `name`".
    std::string title;
    ToolAnnotations annotations;
    // JSON Schema for `structuredContent`, declared only by the tools whose
    // results a model has to branch on. Null when not declared.
    nlohmann::json output_schema;
};

// ---------------------------------------------------------------------------
// ToolResult — result of executing a tool.
// ---------------------------------------------------------------------------
struct ToolResult {
    bool is_error = false;
    // Default to an empty array, not a default-constructed (null) value: the
    // MCP wire contract requires `content` to be an array of content blocks,
    // and a stray null would be rejected by strict clients.
    nlohmann::json content = nlohmann::json::array();
    // The same payload as data rather than as a JSON string inside a text
    // block, so neither the model nor our own web client has to re-parse it.
    // Null means "not provided" — the text block stays either way, which is
    // what the spec asks for and what keeps older clients working.
    nlohmann::json structured;

    ToolResult() = default;
    // Constructors rather than aggregate initialization: the many existing
    // `ToolResult{false, content}` call sites would otherwise each need a
    // trailing initializer for `structured` to satisfy
    // -Werror=missing-field-initializers.
    ToolResult(bool error, nlohmann::json content_blocks,
               nlohmann::json structured_payload = nlohmann::json())
        : is_error(error),
          content(std::move(content_blocks)),
          structured(std::move(structured_payload)) {}
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

    // As above, plus the metadata a host needs to present the tool well:
    // a title, behavioural annotations, and (optionally) the schema of the
    // structured result.
    void Register(const std::string& name,
                  const std::string& description,
                  const nlohmann::json& input_schema,
                  ToolAnnotations annotations,
                  const std::string& title,
                  ToolHandler handler,
                  const nlohmann::json& output_schema = nlohmann::json());

    [[nodiscard]] const std::vector<ToolSchema>& Tools() const noexcept {
        return schemas_;
    }

    // Attach a title and behavioural annotations to an already-registered
    // tool. Returns false when no such tool exists. See mcp/tool_metadata.hpp
    // for where the classification lives.
    bool Annotate(const std::string& name, ToolAnnotations annotations,
                  const std::string& title);

    // Declare the schema of a tool's structuredContent. Returns false when no
    // such tool exists.
    bool SetOutputSchema(const std::string& name,
                         const nlohmann::json& output_schema);

    // Keep only the tools whose family (the part of the name before the first
    // underscore: "adt", "bw", "catalog") is listed, and drop the rest.
    // Returns the number removed. Used by --tools to cut the prompt cost of a
    // 77-tool list down to the family a session actually needs; the set is
    // fixed per process, never per connection, which is what the spec
    // requires.
    size_t RetainFamilies(const std::vector<std::string>& families);

    // The family of a tool name, i.e. the part before the first underscore.
    [[nodiscard]] static std::string FamilyOf(const std::string& tool_name);

    [[nodiscard]] bool HasTool(const std::string& name) const;

    [[nodiscard]] ToolResult Execute(const std::string& name,
                                     const nlohmann::json& params) const;

private:
    std::vector<ToolSchema> schemas_;
    std::map<std::string, ToolHandler> handlers_;
};

} // namespace erpl_adt
