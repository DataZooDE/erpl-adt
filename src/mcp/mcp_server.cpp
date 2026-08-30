#include <erpl_adt/mcp/mcp_server.hpp>

#include <erpl_adt/core/telemetry.hpp>
#include <erpl_adt/core/version.hpp>

#include <chrono>
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

// Revisions this server can speak, newest first. 2025-06-18 is the one that
// introduced structuredContent, outputSchema and tool annotations — all of
// which this server now emits, so claiming it is honest rather than
// aspirational. Anything newer negotiates down to it.
const char* const kSupportedProtocolVersions[] = {"2025-06-18", "2025-03-26",
                                                  "2024-11-05"};

nlohmann::json McpServer::HandleInitialize(
    const nlohmann::json& params, const nlohmann::json& id) {
    initialized_ = true;

    // Echo the client's version when we speak it, rather than answering
    // "2024-11-05" to everyone regardless of what they asked for. A client
    // that asks for something we don't know gets our newest, which is what
    // the spec prescribes and what lets it decide whether to continue.
    std::string negotiated = kSupportedProtocolVersions[0];
    if (params.is_object() && params.contains("protocolVersion") &&
        params["protocolVersion"].is_string()) {
        const auto requested = params["protocolVersion"].get<std::string>();
        for (const auto* supported : kSupportedProtocolVersions) {
            if (requested == supported) {
                negotiated = requested;
                break;
            }
        }
    }

    nlohmann::json result;
    result["protocolVersion"] = negotiated;
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
        nlohmann::json tool{
            {"name", schema.name},
            {"description", schema.description},
            {"inputSchema", schema.input_schema},
            // Behavioural hints so a host can put a confirmation in front of
            // the destructive tools and not the read-only ones.
            {"annotations",
             {{"readOnlyHint", schema.annotations.read_only},
              {"destructiveHint", schema.annotations.destructive},
              {"idempotentHint", schema.annotations.idempotent}}},
        };
        if (!schema.title.empty()) {
            tool["title"] = schema.title;
        }
        if (!schema.output_schema.is_null()) {
            tool["outputSchema"] = schema.output_schema;
        }
        tools.push_back(std::move(tool));
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
        // -32602 (Invalid params), not -32601: tools/call *is* an implemented
        // method, and the name is one of its parameters. -32601 stays for a
        // method this server does not implement at all, which is also what
        // lets a client tell the two cases apart.
        return MakeError(id, -32602, "Unknown tool: " + tool_name);
    }

    // mcp_tool_called { tool, outcome, duration_ms }. `tool` is the registered
    // (bounded) tool name — never argument values. No object names, source, or
    // ADT payload is ever captured here.
    const auto tel_start = std::chrono::steady_clock::now();
    auto result = registry_.Execute(tool_name, arguments);
    const auto tel_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tel_start)
                            .count();
    Telemetry::Feature(feature::kMcpToolCalled,
                       {TelemetryProp::Str("tool", tool_name),
                        TelemetryProp::Str("outcome", result.is_error ? "error" : "ok"),
                        TelemetryProp::Num("duration_ms", static_cast<long long>(tel_ms))});

    nlohmann::json response_result;
    response_result["content"] = result.content;
    // Both, deliberately: the spec asks a tool returning structured content to
    // also serialize it into a text block, so clients that predate
    // structuredContent keep working unchanged.
    if (!result.structured.is_null()) {
        response_result["structuredContent"] = result.structured;
    }
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
