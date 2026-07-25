// Spike 1 — does cpp-httplib do mutual TLS the way erpl-adt would need it?
//
// Asserts:
//   1. httplib::Client("https://host:port", cert, key) completes an mTLS
//      handshake against a server that REQUIRES a client certificate.
//   2. The server sees the client's subject CN — the input SAP's ICM feeds
//      into its USREXTID (DN -> ABAP user) lookup.
//   3. A client WITHOUT a certificate is rejected (so `--auth cert` can't
//      silently degrade to anonymous).
//   4. No Authorization header is sent — the logon is purely transport-level.

#include <httplib.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

} // namespace

int main() {
    constexpr int kPort = 18443;

    // Server requires a client cert signed by ca.crt (4th arg = client CA).
    httplib::SSLServer server("server.crt", "server.key", "ca.crt");
    if (!server.is_valid()) {
        std::printf("  [FAIL] SSLServer failed to initialise\n");
        return 1;
    }

    std::string seen_cn;
    std::string seen_authorization = "<absent>";

    server.Get("/sap/bc/adt/discovery",
               [&](const httplib::Request& req, httplib::Response& res) {
                   auto cert = req.peer_cert();
                   if (cert) seen_cn = cert.subject_cn();
                   if (req.has_header("Authorization")) {
                       seen_authorization = req.get_header_value("Authorization");
                   }
                   res.set_content("<discovery/>", "application/atomsvc+xml");
               });

    std::thread th([&] { server.listen("127.0.0.1", kPort); });
    server.wait_until_ready();

    const std::string url = "https://127.0.0.1:" + std::to_string(kPort);

    // -- 1/2/4: with a client certificate ---------------------------------
    {
        httplib::Client cli(url, "client.crt", "client.key");
        cli.set_ca_cert_path("ca.crt");
        cli.enable_server_hostname_verification(false);

        auto res = cli.Get("/sap/bc/adt/discovery");
        Check(static_cast<bool>(res),
              std::string("mTLS request completed") +
                  (res ? "" : " (transport error: " +
                                  httplib::to_string(res.error()) + ")"));
        Check(res && res->status == 200, "server returned 200");
        Check(seen_cn == "DEVELOPER",
              "server sees client subject CN = DEVELOPER (got: '" + seen_cn + "')");
        Check(seen_authorization == "<absent>",
              "no Authorization header sent (got: " + seen_authorization + ")");
    }

    // -- 3: without a client certificate ----------------------------------
    {
        httplib::Client cli(url);
        cli.set_ca_cert_path("ca.crt");
        cli.enable_server_hostname_verification(false);

        auto res = cli.Get("/sap/bc/adt/discovery");
        const bool rejected = !res || res->status != 200;
        Check(rejected, std::string("client without a certificate is rejected") +
                            (res ? " (got status " + std::to_string(res->status) + ")"
                                 : ""));
    }

    server.stop();
    th.join();

    std::printf("\nSpike 1: %s (%d failure(s))\n",
                failures == 0 ? "VERIFIED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
