#pragma once

#include <erpl_adt/mcp/tool_registry.hpp>

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// McpServer — MCP 2024-11-05 server over stdin/stdout.
//
// Implements JSON-RPC 2.0 protocol with MCP methods:
//   - initialize
//   - tools/list
//   - tools/call
//   - notifications/initialized (notification, no response)
// ---------------------------------------------------------------------------
class McpServer {
public:
    explicit McpServer(ToolRegistry registry,
                       std::istream& in = std::cin,
                       std::ostream& out = std::cout);

    // Run the server loop (blocks until EOF on stdin).
    void Run();

    // Process a single JSON-RPC message and return the response (if any).
    // Returns nullopt for notifications.
    [[nodiscard]] std::optional<nlohmann::json> HandleMessage(
        const nlohmann::json& message);

private:
    nlohmann::json HandleInitialize(const nlohmann::json& params,
                                     const nlohmann::json& id);
    nlohmann::json HandleToolsList(const nlohmann::json& id);
    nlohmann::json HandleToolsCall(const nlohmann::json& params,
                                    const nlohmann::json& id);
    nlohmann::json MakeError(const nlohmann::json& id,
                              int code, const std::string& message);
    nlohmann::json MakeResult(const nlohmann::json& id,
                               const nlohmann::json& result);

    ToolRegistry registry_;
    std::istream& in_;
    std::ostream& out_;
    bool initialized_ = false;
};

} // namespace erpl_adt
