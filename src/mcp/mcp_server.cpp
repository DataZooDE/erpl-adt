#include <erpl_adt/mcp/mcp_server.hpp>

#include <erpl_adt/core/version.hpp>

#include <optional>
#include <string>

namespace erpl_adt {

McpServer::McpServer(ToolRegistry registry,
                     std::istream& in,
                     std::ostream& out)
    : registry_(std::move(registry)), in_(in), out_(out) {}

void McpServer::Run() {
    std::string line;
    while (std::getline(in_, line)) {
        if (line.empty()) continue;

        nlohmann::json message;
        try {
            message = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception&) {
            auto err = MakeError(nullptr, -32700, "Parse error");
            out_ << err.dump() << "\n";
            out_.flush();
            continue;
        }

        auto response = HandleMessage(message);
        if (response) {
            out_ << response->dump() << "\n";
            out_.flush();
        }
    }
}

std::optional<nlohmann::json> McpServer::HandleMessage(
    const nlohmann::json& message) {
    // Check for JSON-RPC 2.0.
    if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0") {
        if (message.contains("id")) {
            return MakeError(message["id"], -32600, "Invalid JSON-RPC version");
        }
        return std::nullopt;
    }

    // Notifications have no "id".
    bool is_notification = !message.contains("id");
    auto method = message.value("method", "");
    auto params = message.value("params", nlohmann::json::object());

    // Handle notifications (no response).
    if (is_notification) {
        // notifications/initialized — acknowledge, no response.
        return std::nullopt;
    }

    auto id = message["id"];

    if (method == "initialize") {
        return HandleInitialize(params, id);
    } else if (method == "tools/list") {
        return HandleToolsList(id);
    } else if (method == "tools/call") {
        return HandleToolsCall(params, id);
    } else {
        return MakeError(id, -32601, "Method not found: " + method);
    }
}

nlohmann::json McpServer::HandleInitialize(
    const nlohmann::json& /*params*/, const nlohmann::json& id) {
    initialized_ = true;

    nlohmann::json result;
    result["protocolVersion"] = "2024-11-05";
    result["capabilities"] = {
        {"tools", nlohmann::json::object()}
    };
    result["serverInfo"] = {
        {"name", "erpl-adt"},
        {"version", kVersion}
    };

    return MakeResult(id, result);
}

nlohmann::json McpServer::HandleToolsList(const nlohmann::json& id) {
    nlohmann::json tools = nlohmann::json::array();

    for (const auto& schema : registry_.Tools()) {
        tools.push_back({
            {"name", schema.name},
            {"description", schema.description},
            {"inputSchema", schema.input_schema}
        });
    }

    return MakeResult(id, {{"tools", tools}});
}

nlohmann::json McpServer::HandleToolsCall(
    const nlohmann::json& params, const nlohmann::json& id) {
    if (!params.contains("name")) {
        return MakeError(id, -32602, "Missing 'name' parameter");
    }

    auto tool_name = params["name"].get<std::string>();
    auto arguments = params.value("arguments", nlohmann::json::object());

    if (!registry_.HasTool(tool_name)) {
        return MakeError(id, -32602, "Unknown tool: " + tool_name);
    }

    auto result = registry_.Execute(tool_name, arguments);

    nlohmann::json response_result;
    response_result["content"] = result.content;
    if (result.is_error) {
        response_result["isError"] = true;
    }

    return MakeResult(id, response_result);
}

nlohmann::json McpServer::MakeError(
    const nlohmann::json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

nlohmann::json McpServer::MakeResult(
    const nlohmann::json& id, const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

} // namespace erpl_adt
