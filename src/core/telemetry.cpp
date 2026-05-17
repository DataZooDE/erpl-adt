#include <erpl_adt/core/telemetry.hpp>

#include <httplib.h>
#include <openssl/evp.h>

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#ifdef __linux__
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#endif

#ifdef _WIN32
#include <iphlpapi.h>
#include <windows.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace erpl_adt {

namespace {

// Global telemetry state.
std::atomic<bool> g_enabled{false};
std::string       g_version;
Telemetry::Backend g_backend;
std::mutex        g_mutex;

static const char* kApiKey   = "phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t";
static const char* kEndpoint = "https://eu.posthog.com";
static const char* kPath     = "/batch/";

bool EnvDisabled(const char* name) {
    const char* val = std::getenv(name);
    if (!val) return false;
    std::string s{val};
    return s == "1" || s == "true" || s == "yes";
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    EVP_MD_CTX*   ctx        = EVP_MD_CTX_new();
    if (!ctx) return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) &&
        EVP_DigestUpdate(ctx, input.data(), input.size()) &&
        EVP_DigestFinal_ex(ctx, digest, &digest_len)) {
        EVP_MD_CTX_free(ctx);
        std::ostringstream oss;
        oss << std::hex;
        for (unsigned int i = 0; i < digest_len; ++i) {
            oss << (digest[i] >> 4) << (digest[i] & 0xF);
        }
        return oss.str();
    }
    EVP_MD_CTX_free(ctx);
    return {};
}

// ---------------------------------------------------------------------------
// Platform: machine ID
// ---------------------------------------------------------------------------

#ifdef __linux__
bool IsPhysicalDevice(const std::string& device) {
    std::string path = "/sys/class/net/" + device + "/device";
    return access(path.c_str(), F_OK) == 0;
}

std::string GetMachineIdLinux() {
    for (const char* path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream f(path);
        if (!f) continue;
        std::string id;
        std::getline(f, id);
        if (!id.empty()) return id;
    }
    // Fallback: first physical NIC MAC address.
    DIR* dir = opendir("/sys/class/net");
    if (!dir) return {};
    std::string mac;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name{entry->d_name};
        if (name == "." || name == "..") continue;
        if (!IsPhysicalDevice(name)) continue;
        std::ifstream f("/sys/class/net/" + name + "/address");
        if (!f) continue;
        std::getline(f, mac);
        if (!mac.empty()) break;
    }
    closedir(dir);
    return mac;
}
#endif // __linux__

#ifdef __APPLE__
std::string GetMachineIdMac() {
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (!service) return {};
    CFStringRef uuid_ref = static_cast<CFStringRef>(
        IORegistryEntryCreateCFProperty(service, CFSTR("IOPlatformUUID"),
                                        kCFAllocatorDefault, 0));
    IOObjectRelease(service);
    if (!uuid_ref) return {};
    char buf[64];
    bool ok = CFStringGetCString(uuid_ref, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(uuid_ref);
    if (!ok) return {};
    return std::string(buf);
}
#endif // __APPLE__

#ifdef _WIN32
std::string GetMachineIdWindows() {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    char buf[64];
    DWORD size = sizeof(buf);
    DWORD type;
    bool ok = (RegQueryValueExA(key, "MachineGuid", nullptr, &type,
                                reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS);
    RegCloseKey(key);
    return ok ? std::string(buf) : std::string{};
}
#endif // _WIN32

std::string GetMachineId() {
#ifdef __linux__
    return GetMachineIdLinux();
#elif defined(__APPLE__)
    return GetMachineIdMac();
#elif defined(_WIN32)
    return GetMachineIdWindows();
#else
    return {};
#endif
}

std::string NowISO8601() {
    std::time_t t = std::time(nullptr);
    std::tm     tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%FT%TZ", &tm);
    return std::string(buf);
}

} // namespace

// ---------------------------------------------------------------------------
// Telemetry public API
// ---------------------------------------------------------------------------

void Telemetry::Initialize(bool user_disabled, const std::string& version) {
    if (user_disabled || EnvDisabled("ERPL_ADT_NO_TELEMETRY") ||
        EnvDisabled("DATAZOO_DISABLE_TELEMETRY")) {
        g_enabled.store(false);
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_version = version;
    g_enabled.store(true);
}

bool Telemetry::IsEnabled() noexcept {
    return g_enabled.load();
}

void Telemetry::SetBackendForTesting(Backend backend) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backend = std::move(backend);
}

std::future<void> Telemetry::TrackCommand(const std::string& group,
                                          const std::string& action) {
    if (!g_enabled.load()) {
        return {};
    }
    // Detached worker, not std::async: a future returned by std::async with
    // launch::async has a special destructor that blocks until the task
    // completes, which would defeat the wait_for(2s) in main() and stretch
    // CLI exit to the full HTTP timeout on slow/unreachable endpoints.
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    std::thread([promise = std::move(promise), group, action]() {
        SendToPostHog(group, action);
        promise->set_value();
    }).detach();
    return future;
}

// static
std::string Telemetry::GetDistinctId() {
    static std::string cached;
    static std::once_flag flag;
    std::call_once(flag, [] {
        std::string id = GetMachineId();
        cached = id.empty() ? "unknown" : Sha256Hex(id);
    });
    return cached;
}

// static
std::string Telemetry::DetectPlatform() {
#if   defined(_WIN32) && defined(_M_ARM64)
    return "windows_arm64";
#elif defined(_WIN32)
    return "windows_amd64";
#elif defined(__APPLE__) && defined(__arm64__)
    return "osx_arm64";
#elif defined(__APPLE__)
    return "osx_amd64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux_arm64";
#elif defined(__linux__)
    return "linux_amd64";
#else
    return "unknown";
#endif
}

// static
void Telemetry::SendToPostHog(const std::string& group, const std::string& action) {
    // Allow test backend to intercept without making real HTTP calls.
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_backend) {
            g_backend(group, action);
            return;
        }
    }

    // Cross-product kill-switch (checked at send time, same as posthog-telemetry lib).
    if (EnvDisabled("DATAZOO_DISABLE_TELEMETRY")) return;

    const std::string distinct_id = GetDistinctId();
    const std::string platform    = DetectPlatform();
    std::string version;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        version = g_version;
    }

    // Build PostHog batch payload (same format as posthog-telemetry library).
    // Property values are simple strings — no user content, no paths, no flags.
    auto esc = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };

    std::string payload =
        "{"
        "\"api_key\":\"" + std::string(kApiKey) + "\","
        "\"batch\":[{"
        "\"event\":\"command_execution\","
        "\"distinct_id\":\"" + esc(distinct_id) + "\","
        "\"properties\":{"
        "\"app_name\":\"erpl-adt\","
        "\"app_version\":\"" + esc(version) + "\","
        "\"platform\":\"" + platform + "\","
        "\"command\":\"" + esc(group) + "\","
        "\"subcommand\":\"" + esc(action) + "\""
        "},"
        "\"timestamp\":\"" + NowISO8601() + "\""
        "}]}";

    try {
        httplib::Client cli(kEndpoint);
        cli.set_connection_timeout(3);
        cli.set_read_timeout(5);
        auto res = cli.Post(kPath, payload, "application/json");
        (void)res; // silent on failure
    } catch (...) {
        // Never propagate — telemetry must not crash the app.
    }
}

} // namespace erpl_adt
