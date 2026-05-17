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

        std::optional<nlohmann::json> response;
        try {
            response = HandleMessage(message);
        } catch (const nlohmann::json::exception& e) {
            // Defense in depth: any unguarded json type-error from a handler
            // must not terminate the long-running server.
            auto id = (message.is_object() && message.contains("id"))
                          ? message["id"]
                          : nlohmann::json(nullptr);
            response = MakeError(id, -32603,
                                 std::string("Internal error: ") + e.what());
        }
        if (response) {
            out_ << response->dump() << "\n";
            out_.flush();
        }
    }
}

std::optional<nlohmann::json> McpServer::HandleMessage(
    const nlohmann::json& message) {
    // Reject anything that is not a JSON object — top-level batches not supported.
    if (!message.is_object()) {
        return MakeError(nullptr, -32600,
                         "Invalid request: message must be an object");
    }

    // "id" must be present and either string/number/null for requests;
    // missing "id" denotes a notification.
    bool is_notification = !message.contains("id");

    // jsonrpc must be the literal string "2.0".
    if (!message.contains("jsonrpc") || !message["jsonrpc"].is_string() ||
        message["jsonrpc"].get<std::string>() != "2.0") {
        if (is_notification) return std::nullopt;
        return MakeError(message["id"], -32600, "Invalid JSON-RPC version");
    }

    // method must be a string.
    if (!message.contains("method") || !message["method"].is_string()) {
        if (is_notification) return std::nullopt;
        return MakeError(message["id"], -32600,
                         "Invalid request: 'method' must be a string");
    }
    auto method = message["method"].get<std::string>();

    // params is optional; if present it must be an object (we don't accept
    // positional params).
    nlohmann::json params = nlohmann::json::object();
    if (message.contains("params")) {
        if (!message["params"].is_object()) {
            if (is_notification) return std::nullopt;
            return MakeError(message["id"], -32602,
                             "Invalid params: 'params' must be an object");
        }
        params = message["params"];
    }

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
    if (!params["name"].is_string()) {
        return MakeError(id, -32602, "'name' parameter must be a string");
    }

    auto tool_name = params["name"].get<std::string>();
    nlohmann::json arguments = nlohmann::json::object();
    if (params.contains("arguments")) {
        if (!params["arguments"].is_object()) {
            return MakeError(id, -32602,
                             "'arguments' parameter must be an object");
        }
        arguments = params["arguments"];
    }

    if (!registry_.HasTool(tool_name)) {
        // JSON-RPC 2.0: -32601 is Method not found. An unknown tool is the
        // tool-namespace equivalent of an unknown method.
        return MakeError(id, -32601, "Unknown tool: " + tool_name);
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
