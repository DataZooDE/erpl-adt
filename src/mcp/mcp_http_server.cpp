#include <erpl_adt/mcp/mcp_http_server.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <string_view>

#ifdef ERPL_ADT_HAVE_WEBUI
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(webui);
#endif

namespace erpl_adt {

namespace {

#ifdef ERPL_ADT_HAVE_WEBUI
// Maps a request path's extension to a Content-Type. Flutter web's build
// output relies on the browser trusting these — a missing/wrong .wasm or
// .js mapping is the kind of thing that fails silently as a blank page.
std::string_view MimeTypeForPath(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }
    auto ext = std::string_view{path}.substr(dot);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".mjs") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".css") return "text/css";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".symbols") return "text/plain";
    return "application/octet-stream";
}
#endif

} // anonymous namespace

struct McpHttpServer::Impl {
    // McpServer's stream parameters are stdio-only concerns (Run()); this
    // transport only ever calls HandleMessage, so the default cin/cout
    // binding is harmless dead weight, not a real dependency.
    McpServer mcp;
    httplib::Server http;
    // httplib::Server dispatches requests on a thread pool by default —
    // McpServer::initialized_ and the underlying DuckDB Connection each
    // catalog_* handler closes over are not safe for concurrent access
    // from multiple threads. Serializing /mcp handling trades request
    // concurrency for correctness; revisit with per-request connections
    // if throughput becomes a bottleneck.
    std::mutex mcp_mutex;

    explicit Impl(ToolRegistry registry) : mcp(std::move(registry)) {}
};

McpHttpServer::McpHttpServer(ToolRegistry registry, bool serve_webui)
    : impl_(std::make_unique<Impl>(std::move(registry))) {
    // CORS: a browser-based client (e.g. erpl_catalog_kit's Flutter web
    // build, HLD.md §4 hosted mode) runs on a different origin than this
    // server and would otherwise be blocked by the browser's same-origin
    // policy before the request even reaches /mcp. Applied to every
    // response via a pre-routing hook rather than per-handler so a new
    // route can't accidentally omit it.
    impl_->http.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    impl_->http.Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(impl_->mcp_mutex);

        nlohmann::json message;
        try {
            message = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::exception&) {
            nlohmann::json err = {{"jsonrpc", "2.0"},
                                  {"id", nullptr},
                                  {"error", {{"code", -32700}, {"message", "Parse error"}}}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        std::optional<nlohmann::json> response;
        try {
            response = impl_->mcp.HandleMessage(message);
        } catch (const nlohmann::json::exception& e) {
            auto id = (message.is_object() && message.contains("id")) ? message["id"]
                                                                        : nlohmann::json(nullptr);
            nlohmann::json err = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}};
            res.status = 500;
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (!response.has_value()) {
            // Notification (e.g. notifications/initialized) — no body per
            // JSON-RPC 2.0, HTTP 204 is the closest honest status.
            res.status = 204;
            return;
        }
        res.set_content(response->dump(), "application/json");
    });

    impl_->http.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    if (serve_webui) {
#ifdef ERPL_ADT_HAVE_WEBUI
        impl_->http.Get(".*", [](const httplib::Request& req, httplib::Response& res) {
            static const auto fs = cmrc::webui::get_filesystem();
            // Strip the leading '/' — cmrc paths are relative (e.g. "index.html",
            // "assets/foo.png"), not absolute.
            std::string path = req.path.empty() ? "" : req.path.substr(1);
            if (!fs.is_file(path)) {
                // SPA fallback: go_router does client-side path routing (e.g.
                // /entity/<id>), so a hard refresh or deep link on any such path
                // must still serve index.html — the app's own router resolves it
                // client-side. A real missing asset (e.g. a stale cached path)
                // also falls through here, but that's indistinguishable from a
                // valid deep link without duplicating the Flutter route table.
                path = "index.html";
            }
            auto file = fs.open(path);
            res.set_content(std::string(file.begin(), file.end()), std::string(MimeTypeForPath(path)));
        });
#else
        impl_->http.Get(".*", [](const httplib::Request&, httplib::Response& res) {
            res.status = 501;
            res.set_content(
                "Web UI not embedded in this build. Run 'make webui' (requires the "
                "Flutter SDK) and rebuild erpl-adt.\n",
                "text/plain");
        });
#endif
    }
}

McpHttpServer::~McpHttpServer() = default;

bool McpHttpServer::Run(const std::string& host, uint16_t port) {
    return impl_->http.listen(host, port);
}

void McpHttpServer::Stop() { impl_->http.stop(); }

} // namespace erpl_adt
