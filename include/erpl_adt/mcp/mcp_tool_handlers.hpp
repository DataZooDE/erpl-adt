#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/mcp/tool_registry.hpp>

namespace erpl_adt {

// Register all ADT operation tools with the MCP tool registry.
// Each tool handler captures &session by reference — single-threaded,
// one session shared across all tool calls.
void RegisterAdtTools(ToolRegistry& registry, IAdtSession& session);

} // namespace erpl_adt
