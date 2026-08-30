#pragma once

#include <erpl_adt/mcp/http_security.hpp>
#include <erpl_adt/mcp/mcp_server.hpp>
#include <erpl_adt/mcp/tool_registry.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// McpHttpServer — thin JSON-RPC-over-HTTP shim around the same
// McpServer::HandleMessage dispatch the stdio transport uses (BRD.md
// FR-MCP-2): identical tool contract on both transports, not a second
// implementation. POST a JSON-RPC 2.0 message to `/mcp`, get the JSON-RPC
// response body back.
// ---------------------------------------------------------------------------
class McpHttpServer {
public:
    // serve_webui: also serve the embedded Flutter web client (erpl_adt_catalog_kit)
    // at every path other than /mcp and /healthz, same-origin with the JSON-RPC
    // endpoint (erpl-adt catalog webui). If the binary was built without the web
    // UI embedded (see CMakeLists.txt's ERPL_ADT_HAVE_WEBUI guard), those routes
    // return an instructional message instead of 404ing silently.
    explicit McpHttpServer(ToolRegistry registry, bool serve_webui = false);

    // As above, with Origin allowlisting and an optional bearer token; see
    // mcp/http_security.hpp for what the defaults allow and why.
    McpHttpServer(ToolRegistry registry, bool serve_webui,
                  HttpSecurityOptions security);
    ~McpHttpServer();

    // Blocks serving on the given port until Stop() is called (e.g. from a
    // signal handler) or the process is killed.
    [[nodiscard]] bool Run(const std::string& host, uint16_t port);
    void Stop();

    McpHttpServer(const McpHttpServer&) = delete;
    McpHttpServer& operator=(const McpHttpServer&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace erpl_adt
