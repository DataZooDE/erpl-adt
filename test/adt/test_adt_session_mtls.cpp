#include <catch2/catch_test_macros.hpp>
#include <erpl_adt/adt/adt_session.hpp>
#include <httplib.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <atomic>
#include <array>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

using namespace erpl_adt;

namespace {
// Fresh certificates make expiry tests independent of the date CI runs.
// This self-signed test identity is trusted explicitly by both TLS peers.
class TestIdentity {
public:
    explicit TestIdentity(long validity = 3600) {
        const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory_ = std::filesystem::temp_directory_path() / ("erpl-mtls-" + nonce);
        REQUIRE(std::filesystem::create_directory(directory_));
        cert = (directory_ / "cert.pem").string();
        key = (directory_ / "key.pem").string();
        const std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
            EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
        REQUIRE(context);
        REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
        REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1);
        EVP_PKEY* generated = nullptr;
        REQUIRE(EVP_PKEY_keygen(context.get(), &generated) == 1);
        const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> identity(generated, EVP_PKEY_free);
        const std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
        REQUIRE(certificate);
        REQUIRE(X509_set_version(certificate.get(), 2) == 1);
        REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1);
        REQUIRE(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -7200));
        REQUIRE(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), validity));
        REQUIRE(X509_set_pubkey(certificate.get(), identity.get()) == 1);
        auto* subject = X509_get_subject_name(certificate.get());
        const unsigned char common_name[] = "localhost";
        REQUIRE(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                          common_name, -1, -1, 0) == 1);
        REQUIRE(X509_set_issuer_name(certificate.get(), subject) == 1);
        AddExtension(certificate.get(), NID_basic_constraints, "critical,CA:TRUE");
        AddExtension(certificate.get(), NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
        REQUIRE(X509_sign(certificate.get(), identity.get(), EVP_sha256()) > 0);
        const std::unique_ptr<BIO, decltype(&BIO_free)> cert_file(BIO_new_file(cert.c_str(), "w"), BIO_free);
        const std::unique_ptr<BIO, decltype(&BIO_free)> key_file(BIO_new_file(key.c_str(), "w"), BIO_free);
        REQUIRE(cert_file);
        REQUIRE(key_file);
        REQUIRE(PEM_write_bio_X509(cert_file.get(), certificate.get()) == 1);
        REQUIRE(PEM_write_bio_PrivateKey(key_file.get(), identity.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1);
    }
    ~TestIdentity() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }
    std::string cert;
    std::string key;
private:
    static void AddExtension(X509* certificate, int nid, const char* value) {
        const std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
            X509V3_EXT_conf_nid(nullptr, nullptr, nid, value), X509_EXTENSION_free);
        REQUIRE(extension);
        REQUIRE(X509_add_ext(certificate, extension.get(), -1) == 1);
    }
    std::filesystem::path directory_;
};

class RunningServer {
public:
    explicit RunningServer(httplib::Server& server) : server_(server) {
        port = server_.bind_to_any_port("127.0.0.1");
        REQUIRE(port > 0);
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }
    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }
    int port;
private:
    httplib::Server& server_;
    std::thread thread_;
};

AdtSessionOptions CertificateOptions(const TestIdentity& identity) {
    AdtSessionOptions options;
    options.client_certificate_path = identity.cert;
    options.client_key_path = identity.key;
    options.ca_certificate_path = identity.cert;
    options.connect_timeout = std::chrono::seconds(2);
    options.read_timeout = std::chrono::seconds(2);
    return options;
}

std::unique_ptr<AdtSession> Session(int port, bool https, const AdtSessionOptions& options) {
    return std::make_unique<AdtSession>("127.0.0.1", static_cast<uint16_t>(port), https,
        "AMBIENT_USER", "ambient-password", SapClient::Create("001").Value(), options);
}
} // namespace

TEST_CASE("mTLS authenticates with a client certificate and never sends Basic", "[adt][session][mtls]") {
    TestIdentity identity;
    httplib::SSLServer server(identity.cert.c_str(), identity.key.c_str(), identity.cert.c_str());
    REQUIRE(server.is_valid());
    std::atomic<int> calls{0};
    std::atomic<bool> authorization{false};
    server.Get("/sap/bc/adt/discovery", [&](const httplib::Request& request, httplib::Response& response) {
        ++calls;
        authorization = request.has_header("Authorization");
        response.set_content("<discovery/>", "application/xml");
    });
    RunningServer running(server);
    auto session = Session(running.port, true, CertificateOptions(identity));
    auto result = session->Get("/sap/bc/adt/discovery");
    REQUIRE(result.IsOk());
    CHECK(result.Value().status_code == 200);
    CHECK(result.Value().body == "<discovery/>");
    CHECK(calls == 1);
    CHECK_FALSE(authorization);
}

TEST_CASE("mTLS configuration fails before any plaintext request", "[adt][session][mtls]") {
    TestIdentity identity;
    auto options = CertificateOptions(identity);
    httplib::Server server;
    std::atomic<int> calls{0};
    server.Get(".*", [&](const httplib::Request&, httplib::Response&) { ++calls; });
    RunningServer running(server);
    auto session = Session(running.port, false, options);
    const auto result = session->Get("/sap/bc/adt/discovery");
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("HTTPS") != std::string::npos);
    CHECK(calls == 0);
    CHECK(session->Post("/target", "body", "text/plain").IsErr());
    CHECK(session->Put("/target", "body", "text/plain").IsErr());
    CHECK(session->Delete("/target").IsErr());
    CHECK(session->FetchCsrfToken().IsErr());
    CHECK(session->Get("/sap/public/bc/icf/logoff").IsErr());
    CHECK(calls == 0);
}

TEST_CASE("mTLS invalid identity and trust never reach an HTTP handler", "[adt][session][mtls]") {
    TestIdentity identity;
    TestIdentity other;
    httplib::SSLServer server(identity.cert.c_str(), identity.key.c_str(), identity.cert.c_str());
    REQUIRE(server.is_valid());
    std::atomic<int> calls{0};
    server.Get(".*", [&](const httplib::Request&, httplib::Response&) { ++calls; });
    RunningServer running(server);
    auto options = CertificateOptions(identity);
    SECTION("missing key") { options.client_key_path.reset(); }
    SECTION("missing certificate") { options.client_certificate_path.reset(); }
    SECTION("unreadable certificate") { options.client_certificate_path = identity.cert + ".missing"; }
    SECTION("mismatched key") { options.client_key_path = other.key; }
    SECTION("untrusted client") {
        options.client_certificate_path = other.cert;
        options.client_key_path = other.key;
    }
    SECTION("untrusted server") { options.ca_certificate_path = other.cert; }
    SECTION("missing CA file") { options.ca_certificate_path = identity.cert + ".missing"; }
    SECTION("empty certificate") { options.client_certificate_path = ""; }
    SECTION("encrypted key cannot prompt") {
        std::ofstream key_file(other.key);
        key_file << "-----BEGIN ENCRYPTED PRIVATE KEY-----\n";
        key_file.close();
        options.client_key_path = other.key;
    }
    auto session = Session(running.port, true, options);
    const auto result = session->Get("/sap/bc/adt/discovery");
    REQUIRE(result.IsErr());
    CHECK(calls == 0);
}

TEST_CASE("mTLS rejects an expired server certificate", "[adt][session][mtls]") {
    TestIdentity expired(-60);
    TestIdentity client;
    httplib::SSLServer server(expired.cert.c_str(), expired.key.c_str(), client.cert.c_str());
    REQUIRE(server.is_valid());
    std::atomic<int> calls{0};
    server.Get(".*", [&](const httplib::Request&, httplib::Response&) { ++calls; });
    RunningServer running(server);
    auto options = CertificateOptions(client);
    options.ca_certificate_path = expired.cert;
    auto session = Session(running.port, true, options);
    const auto result = session->Get("/sap/bc/adt/discovery");
    REQUIRE(result.IsErr());
    CHECK(calls == 0);
}

TEST_CASE("mTLS carries CSRF and session cleanup without Basic credentials", "[adt][session][mtls]") {
    TestIdentity identity;
    httplib::SSLServer server(identity.cert.c_str(), identity.key.c_str(), identity.cert.c_str());
    REQUIRE(server.is_valid());
    std::atomic<bool> authorization{false};
    std::atomic<int> writes{0};
    std::atomic<int> logoffs{0};
    server.Get("/sap/bc/adt/discovery", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.has_header("Authorization")) authorization = true;
        response.set_header("x-csrf-token", "certificate-token");
        response.set_header("Set-Cookie", "SAP_SESSIONID_TEST_001=test-session; Path=/; Secure");
        response.set_content("<discovery/>", "application/xml");
    });
    server.Post("/sap/bc/adt/test", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.has_header("Authorization")) authorization = true;
        if (request.get_header_value("x-csrf-token") != "certificate-token" ||
            request.get_header_value("Cookie").find("test-session") == std::string::npos ||
            request.get_header_value("X-sap-adt-sessiontype") != "stateful") {
            response.status = 400;
            return;
        }
        ++writes;
        response.status = 201;
    });
    server.Get("/sap/public/bc/icf/logoff", [&](const httplib::Request& request, httplib::Response&) {
        if (request.has_header("Authorization")) authorization = true;
        if (request.get_header_value("Cookie").find("test-session") != std::string::npos) ++logoffs;
    });
    RunningServer running(server);
    {
        auto session = Session(running.port, true, CertificateOptions(identity));
        session->SetStateful(true);
        const auto result = session->Post("/sap/bc/adt/test", "<test/>", "application/xml");
        REQUIRE(result.IsOk());
        CHECK(result.Value().status_code == 201);
    }
    CHECK(writes == 1);
    CHECK(logoffs == 1);
    CHECK_FALSE(authorization);
}

TEST_CASE("Windows certificate mode never falls back to HTTP or PEM", "[adt][session][mtls][windows-certificate]") {
    AdtSessionOptions options;
    options.windows_certificate_thumbprint = std::string(40, 'A');
    SECTION("HTTPS required") {
        auto session = Session(1, false, options);
        const auto result = session->Get("/sap/bc/adt/discovery");
        REQUIRE(result.IsErr());
        CHECK(result.Error().message.find("HTTPS") != std::string::npos);
    }
    SECTION("PEM conflict") {
        options.client_certificate_path = "certificate.pem";
        options.client_key_path = "key.pem";
        auto session = Session(1, true, options);
        const auto result = session->Get("/sap/bc/adt/discovery");
        REQUIRE(result.IsErr());
        CHECK(result.Error().message.find("combine") != std::string::npos);
    }
#ifndef _WIN32
    SECTION("unsupported platform") {
        auto session = Session(1, true, options);
        const auto result = session->Get("/sap/bc/adt/discovery");
        REQUIRE(result.IsErr());
        CHECK(result.Error().message.find("Windows") != std::string::npos);
    }
#endif
}

#ifdef _WIN32
namespace {
// The test creates its own non-exportable key and removes both the certificate
// and key container. It never changes a Windows root trust store.
class WindowsTestIdentity {
public:
    WindowsTestIdentity() {
        try {
            Initialize();
        } catch (...) {
            Cleanup();
            throw;
        }
    }
    ~WindowsTestIdentity() { Cleanup(); }
    std::string thumbprint;
    std::string cert_path;
private:
    void Initialize() {
        container_ = L"erpl-adt-mtls-" + std::to_wstring(
            std::chrono::steady_clock::now().time_since_epoch().count());
        REQUIRE(CryptAcquireContextW(&provider_, container_.c_str(), MS_ENH_RSA_AES_PROV_W,
                                      PROV_RSA_AES, CRYPT_NEWKEYSET));
        REQUIRE(CryptGenKey(provider_, AT_KEYEXCHANGE, 2048U << 16, &key_));
        std::array<BYTE, 4096> exported{};
        DWORD export_size = static_cast<DWORD>(exported.size());
        CHECK_FALSE(CryptExportKey(key_, 0, PRIVATEKEYBLOB, 0, exported.data(), &export_size));
        const wchar_t subject[] = L"CN=erpl-adt non-exportable client";
        DWORD subject_size = 0;
        REQUIRE(CertStrToNameW(X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
                               nullptr, nullptr, &subject_size, nullptr));
        std::vector<BYTE> encoded_subject(subject_size);
        REQUIRE(CertStrToNameW(X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
                               nullptr, encoded_subject.data(), &subject_size, nullptr));
        CERT_NAME_BLOB subject_blob{};
        subject_blob.cbData = subject_size;
        subject_blob.pbData = encoded_subject.data();
        CRYPT_KEY_PROV_INFO provider_info{};
        provider_info.pwszContainerName = container_.data();
        provider_info.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        provider_info.dwProvType = PROV_RSA_AES;
        provider_info.dwKeySpec = AT_KEYEXCHANGE;
        certificate_ = CertCreateSelfSignCertificate(provider_, &subject_blob, 0,
            &provider_info, nullptr, nullptr, nullptr, nullptr);
        REQUIRE(certificate_);
        store_ = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
        REQUIRE(store_);
        REQUIRE(CertAddCertificateContextToStore(store_, certificate_, CERT_STORE_ADD_NEW, &stored_));
        std::array<BYTE, 20> hash{};
        DWORD hash_size = static_cast<DWORD>(hash.size());
        REQUIRE(CertGetCertificateContextProperty(certificate_, CERT_HASH_PROP_ID, hash.data(), &hash_size));
        const char hex[] = "0123456789ABCDEF";
        for (const auto byte : hash) {
            thumbprint += hex[byte >> 4];
            thumbprint += hex[byte & 15];
        }
        cert_path = (std::filesystem::temp_directory_path() /
            ("erpl-adt-public-" + thumbprint + ".pem")).string();
        const unsigned char* encoded = certificate_->pbCertEncoded;
        const std::unique_ptr<X509, decltype(&X509_free)> certificate(
            d2i_X509(nullptr, &encoded, static_cast<long>(certificate_->cbCertEncoded)), X509_free);
        REQUIRE(certificate);
        const std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new_file(cert_path.c_str(), "w"), BIO_free);
        REQUIRE(output);
        REQUIRE(PEM_write_bio_X509(output.get(), certificate.get()) == 1);
    }
    void Cleanup() {
        if (stored_) CertDeleteCertificateFromStore(stored_);
        if (certificate_) CertFreeCertificateContext(certificate_);
        if (store_) CertCloseStore(store_, 0);
        if (key_) CryptDestroyKey(key_);
        if (provider_) CryptReleaseContext(provider_, 0);
        HCRYPTPROV deleted = 0;
        CryptAcquireContextW(&deleted, container_.c_str(), MS_ENH_RSA_AES_PROV_W,
                              PROV_RSA_AES, CRYPT_DELETEKEYSET);
        std::error_code ignored;
        std::filesystem::remove(cert_path, ignored);
    }
    std::wstring container_;
    HCRYPTPROV provider_ = 0;
    HCRYPTKEY key_ = 0;
    PCCERT_CONTEXT certificate_ = nullptr;
    PCCERT_CONTEXT stored_ = nullptr;
    HCERTSTORE store_ = nullptr;
};
} // namespace

TEST_CASE("Windows store certificate uses a non-exportable key for TLS and session cleanup",
          "[adt][session][mtls][windows-certificate]") {
    TestIdentity server_identity;
    WindowsTestIdentity client;
    httplib::SSLServer server(server_identity.cert.c_str(), server_identity.key.c_str(), client.cert_path.c_str());
    REQUIRE(server.is_valid());
    std::atomic<bool> authorization{false};
    std::atomic<int> logoffs{0};
    server.Get("/sap/bc/adt/discovery", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.has_header("Authorization")) authorization = true;
        response.set_header("x-csrf-token", "native-token");
        response.set_header("Set-Cookie", "SAP_SESSIONID_TEST_001=native-session; Path=/; Secure");
        response.set_header("Set-Cookie", "sap-contextid=native-context; Path=/; Secure");
        response.set_content("<discovery/>", "application/xml");
    });
    server.Post("/sap/bc/adt/test", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.has_header("Authorization")) authorization = true;
        const auto cookie = request.get_header_value("Cookie");
        if (request.get_header_value("x-csrf-token") != "native-token" ||
            cookie.find("native-session") == std::string::npos ||
            cookie.find("native-context") == std::string::npos) {
            response.status = 400;
            return;
        }
        response.set_content(request.body, "application/octet-stream");
    });
    server.Get("/sap/public/bc/icf/logoff", [&](const httplib::Request& request, httplib::Response&) {
        if (request.has_header("Authorization")) authorization = true;
        ++logoffs;
    });
    RunningServer running(server);
    {
        AdtSessionOptions options;
        options.windows_certificate_thumbprint = client.thumbprint;
        options.disable_tls_verify = true; // Only this ephemeral local server certificate.
        auto session = Session(running.port, true, options);
        session->SetStateful(true);
        const std::string body("a\0b", 3);
        const auto result = session->Post("/sap/bc/adt/test", body, "application/octet-stream");
        REQUIRE(result.IsOk());
        CHECK(result.Value().status_code == 200);
        CHECK(result.Value().body == body);
    }
    CHECK_FALSE(authorization);
    CHECK(logoffs == 1);
}
#endif

TEST_CASE("Certificate session files cannot import another authentication identity", "[adt][session][mtls][session-binding]") {
    TestIdentity identity;
    TestIdentity other;
    const auto path = identity.cert + ".session";
    auto options = CertificateOptions(identity);
    auto session = Session(50001, true, options);
    SECTION("unbound legacy cookies") {
        std::ofstream saved(path);
        saved << "{\"cookies\":{\"SAP_SESSIONID_TEST_001\":\"wrong-user\"}}";
        saved.close();
        const auto loaded = session->LoadSession(path);
        REQUIRE(loaded.IsErr());
    }
    SECTION("Basic session") {
        auto basic = Session(50001, true, AdtSessionOptions{});
        REQUIRE(basic->SaveSession(path).IsOk());
        REQUIRE(session->LoadSession(path).IsErr());
    }
    SECTION("another certificate") {
        auto different = Session(50001, true, CertificateOptions(other));
        REQUIRE(different->SaveSession(path).IsOk());
        REQUIRE(session->LoadSession(path).IsErr());
    }
    SECTION("another origin") {
        auto different = Session(50002, true, options);
        REQUIRE(different->SaveSession(path).IsOk());
        REQUIRE(session->LoadSession(path).IsErr());
    }
    SECTION("same certificate and origin") {
        REQUIRE(session->SaveSession(path).IsOk());
        auto same = Session(50001, true, options);
        REQUIRE(same->LoadSession(path).IsOk());
    }
}

TEST_CASE("HTTP terminal session recovery occurs only between operations after cleanup", "[adt][session][mtls][recovery]") {
    bool with_cookie = false;
    bool fail_first_logoff = false;
    SECTION("no cookie") {}
    SECTION("cookie cleanup") { with_cookie = true; }
    SECTION("failed cleanup preserves latch") { with_cookie = true; fail_first_logoff = true; }
    httplib::Server server;
    std::atomic<int> calls{0};
    std::atomic<int> logoffs{0};
    server.Get("/test", [&](const httplib::Request&, httplib::Response& response) {
        if (++calls == 1) {
            response.status = 429;
            response.set_content("Too Many Logons", "text/plain");
            if (with_cookie) response.set_header("Set-Cookie", "SAP_SESSIONID_TEST_001=held; Path=/");
        } else {
            response.set_content("recovered", "text/plain");
        }
    });
    server.Get("/sap/public/bc/icf/logoff", [&](const httplib::Request&, httplib::Response& response) {
        if (++logoffs == 1 && fail_first_logoff) response.status = 503;
    });
    RunningServer running(server);
    auto session = Session(running.port, false, AdtSessionOptions{});
    const auto first = session->Get("/test");
    REQUIRE(first.IsOk());
    CHECK(first.Value().status_code == 429);
    const auto stopped = session->Get("/test");
    REQUIRE(stopped.IsErr());
    CHECK(stopped.Error().IsSessionFailure());
    CHECK(calls == 1);
    session->RecoverForNextOperation();
    if (fail_first_logoff) {
        CHECK(session->Get("/test").IsErr());
        CHECK(calls == 1);
        session->RecoverForNextOperation();
    }
    const auto recovered = session->Get("/test");
    REQUIRE(recovered.IsOk());
    CHECK(recovered.Value().body == "recovered");
    CHECK(logoffs == (with_cookie ? (fail_first_logoff ? 2 : 1) : 0));
}

TEST_CASE("HTTP exchange confines absolute locations to the configured origin", "[adt][session][mtls][origin]") {
    httplib::Server server;
    std::atomic<int> calls{0};
    server.Get(".*", [&](const httplib::Request& request, httplib::Response& response) {
        ++calls;
        response.set_content(request.path, "text/plain");
    });
    RunningServer running(server);
    auto session = Session(running.port, false, AdtSessionOptions{});
    const auto base = "http://127.0.0.1:" + std::to_string(running.port);
    SECTION("same-origin absolute location") {
        const auto response = session->Get(base + "/poll?state=1");
        REQUIRE(response.IsOk());
        CHECK(response.Value().body == "/poll");
        CHECK(calls == 1);
    }
    SECTION("another origin") {
        CHECK(session->Get("http://elsewhere.invalid/poll").IsErr());
        CHECK(calls == 0);
    }
    SECTION("changed port") {
        CHECK(session->Get("http://127.0.0.1:1/poll").IsErr());
        CHECK(calls == 0);
    }
    SECTION("scheme relative location") {
        CHECK(session->Get("//elsewhere.invalid/poll").IsErr());
        CHECK(calls == 0);
    }
}

TEST_CASE("mTLS absolute locations preserve HTTPS and reject downgrade", "[adt][session][mtls][origin]") {
    TestIdentity identity;
    httplib::SSLServer server(identity.cert.c_str(), identity.key.c_str(), identity.cert.c_str());
    REQUIRE(server.is_valid());
    std::atomic<int> calls{0};
    server.Get("/poll", [&](const httplib::Request&, httplib::Response& response) {
        ++calls;
        response.set_content("complete", "text/plain");
    });
    RunningServer running(server);
    auto session = Session(running.port, true, CertificateOptions(identity));
    const auto address = "127.0.0.1:" + std::to_string(running.port) + "/poll";
    const auto response = session->Get("https://" + address);
    REQUIRE(response.IsOk());
    CHECK(response.Value().body == "complete");
    CHECK(session->Get("http://" + address).IsErr());
    CHECK(calls == 1);
}
