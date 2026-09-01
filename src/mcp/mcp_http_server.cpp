#include <erpl_adt/mcp/mcp_http_server.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#ifdef ERPL_ADT_HAVE_WEBUI
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(webui);
#endif

namespace erpl_adt {

namespace {

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

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

// The name part of a Host header, for quoting back in the remedy — the flag
// takes a host, and "buildbox.corp:8383" would work but reads like a typo.
// Does this request carry any Sec-Fetch-* header? Browsers set them on every
// request; nothing else does.
bool HasSecFetchHeader(const httplib::Request& req) {
    for (const auto& [name, value] : req.headers) {
        (void)value;
        if (name.size() >= 10 &&
            ToLowerAscii(name.substr(0, 10)) == "sec-fetch-") {
            return true;
        }
    }
    return false;
}

std::string HostOnly(const std::string& host) {
    if (!host.empty() && host.front() == '[') {
        const auto close = host.find(']');
        return close == std::string::npos ? host : host.substr(0, close + 1);
    }
    const auto colon = host.find(':');
    return colon == std::string::npos ? host : host.substr(0, colon);
}

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

    HttpSecurityOptions security;
    // Hosts already warned about, so a scripted attack cannot flood stderr
    // with one line per request. Guarded by warned_hosts_mutex because the
    // warning is emitted from the pre-routing hook, which runs on httplib's
    // thread pool — before /mcp's own lock is taken.
    std::set<std::string> warned_hosts;
    std::mutex warned_hosts_mutex;

    Impl(ToolRegistry registry, HttpSecurityOptions security_options)
        : mcp(std::move(registry)), security(std::move(security_options)) {}

    // Say something about an unrecognised Host once per distinct value, so a
    // scripted attack cannot flood stderr with one line per request.
    void ReportHostOnce(const std::string& host, bool refused) {
        {
            std::lock_guard<std::mutex> lock(warned_hosts_mutex);
            if (!warned_hosts.insert(host).second) {
                return;
            }
        }
        if (refused) {
            std::cerr << "Refused a browser request for Host '" << host
                      << "': not loopback, an IP literal, or a configured host. "
                         "If that is an address you serve on, allow it with "
                         "--allowed-hosts " << HostOnly(host)
                      << " (or ERPL_ADT_ALLOWED_HOSTS). If it is not, this is "
                         "what a DNS-rebinding attempt looks like.\n";
            return;
        }
        std::cerr << "Note: served a non-browser request for Host '" << host
                  << "', which is not loopback, an IP literal, or a configured "
                     "host. Browsers using that name are refused — allow it "
                     "with --allowed-hosts " << HostOnly(host) << ".\n";
    }
};

McpHttpServer::McpHttpServer(ToolRegistry registry, bool serve_webui)
    : McpHttpServer(std::move(registry), serve_webui, HttpSecurityOptions{}) {}

McpHttpServer::McpHttpServer(ToolRegistry registry, bool serve_webui,
                             HttpSecurityOptions security)
    : impl_(std::make_unique<Impl>(std::move(registry), std::move(security))) {
    // CORS + Origin validation. A browser-based client (erpl_catalog_kit's
    // Flutter web build, HLD.md §4 hosted mode) needs CORS headers to reach
    // /mcp at all — but this endpoint executes SAP writes, so the origin
    // asking has to be one we accept, and the answer is never "*" unless the
    // operator asked for it. What is allowed by default (same-origin,
    // loopback, no Origin at all) is in mcp/http_security.hpp; the effect is
    // that non-browser clients and the embedded UI are unaffected, and a
    // random page the developer visits is not.
    //
    // Applied via a pre-routing hook rather than per-handler so a new route
    // can't accidentally omit it.
    impl_->http.set_pre_routing_handler([this](const httplib::Request& req,
                                               httplib::Response& res) {
        // Host first, and on its own: DNS rebinding hands the attacker both
        // the Host and the Origin, so the same-origin rule below cannot see
        // it — checking Host first is what makes a rebound name refusable
        // whatever Origin it claims.
        //
        // Only browser-originated requests are held to it. Rebinding is a
        // browser attack, and a browser announces itself: Origin on every
        // POST, Sec-Fetch-* on everything modern. curl and native MCP clients
        // may go on using whatever hostname they like.
        const auto host = req.get_header_value("Host");
        const auto origin = req.get_header_value("Origin");
        if (!IsAllowed(ClassifyHost(host, impl_->security))) {
            const bool browser = IsBrowserRequest(origin, HasSecFetchHeader(req));
            impl_->ReportHostOnce(host, browser);
            if (browser) {
                res.status = 403;
                res.set_content(
                    nlohmann::json{
                        {"error", "host '" + host +
                                      "' not allowed; start the server with "
                                      "--allowed-hosts " + HostOnly(host) +
                                      " (or set ERPL_ADT_ALLOWED_HOSTS) if that "
                                      "is an address you serve on"}}
                        .dump(),
                    "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        const auto verdict = ClassifyOrigin(origin, host, impl_->security);

        if (!IsAllowed(verdict)) {
            res.status = 403;
            res.set_content(
                R"({"error":"origin not allowed; pass --cors-origin to allow it"})",
                "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }

        if (!origin.empty()) {
            // Echo the caller's origin, never "*" — except when the operator
            // explicitly configured the wildcard escape hatch.
            res.set_header("Access-Control-Allow-Origin",
                           verdict == OriginVerdict::Wildcard ? "*" : origin);
            res.set_header("Vary", "Origin");
        }
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    impl_->http.Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        // Authenticate before taking the lock and before parsing: a rejected
        // request must not execute a tool or hold up a legitimate one.
        if (!BearerTokenMatches(req.get_header_value("Authorization"),
                                impl_->security.auth_token)) {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Bearer");
            res.set_content(
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,)"
                R"("message":"Unauthorized: missing or invalid bearer token"}})",
                "application/json");
            return;
        }

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
            // JSON-RPC 2.0. The transport spec asks for 202 Accepted: the
            // message was taken, and there is nothing to answer with.
            res.status = 202;
            return;
        }

        // An unimplemented method gets HTTP 404 alongside its -32601 body.
        // The pairing is what a client probes to tell a modern server from a
        // legacy HTTP+SSE one, and it costs nothing to be honest about.
        if (response->contains("error") && response->at("error").is_object() &&
            response->at("error").value("code", 0) == -32601) {
            res.status = 404;
        }
        res.set_content(response->dump(), "application/json");
    });

    // /mcp answers POST only. Registered here, before the web UI's catch-all
    // Get(".*") below, or that catch-all would swallow GET /mcp and serve the
    // SPA's index.html instead of a 405.
    const auto method_not_allowed = [](const httplib::Request&,
                                       httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST, OPTIONS");
        res.set_content(
            R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,)"
            R"("message":"/mcp accepts POST only"}})",
            "application/json");
    };
    impl_->http.Get("/mcp", method_not_allowed);
    impl_->http.Delete("/mcp", method_not_allowed);

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
