#include <erpl_adt/adt/rfc_tunnel_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <deque>

using namespace erpl_adt;
namespace {
struct Request { std::string method; std::string path; std::string body; HttpHeaders headers; };
struct State {
    std::vector<Request> requests;
    int closes = 0;
    std::string user = "SNC_MAPPED_USER";
    std::optional<std::string> reconnected_user;
};
class Client final : public IRfcTunnelClient {
public:
    explicit Client(State& state) : state_(state) {}
    std::deque<Result<HttpResponse, Error>> responses;
    Result<void, Error> EnsureConnected() override {
        if (state_.reconnected_user) {
            state_.user = *state_.reconnected_user;
            state_.reconnected_user.reset();
        }
        return Result<void, Error>::Ok();
    }
    Result<HttpResponse, Error> Execute(std::string_view method, std::string_view path,
        std::string_view body, const HttpHeaders& headers) override {
        const auto connected = EnsureConnected();
        if (connected.IsErr()) return Result<HttpResponse, Error>::Err(connected.Error());
        state_.requests.push_back({std::string(method), std::string(path), std::string(body), headers});
        if (responses.empty()) return Result<HttpResponse, Error>::Ok(HttpResponse{200, {}, "ok"});
        auto response = std::move(responses.front()); responses.pop_front(); return response;
    }
    std::string AuthenticatedUser() const override { return state_.user; }
    void Close() override { ++state_.closes; }
private:
    State& state_;
};
}
TEST_CASE("RFC tunnel forwards HTTP semantics and binary bodies", "[rfc][session]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    REQUIRE(session.LogonUserName() == "SNC_MAPPED_USER");
    const std::string bytes("\0\xff\xc3\xa4", 4);
    const auto response = session.Put("/sap/bc/adt/test?version=inactive", bytes,
        "application/octet-stream", {{"Accept", "application/test"}});
    REQUIRE(response.IsOk());
    REQUIRE(state.requests.size() == 1);
    CHECK(state.requests[0].method == "PUT");
    CHECK(state.requests[0].path == "/sap/bc/adt/test?version=inactive");
    CHECK(state.requests[0].body == bytes);
    CHECK(state.requests[0].headers.at("Content-Type") == "application/octet-stream");
    CHECK(state.requests[0].headers.at("Accept") == "application/test");
}
TEST_CASE("RFC tunnel discovery needs Accept but no ICF CSRF preflight", "[rfc][session]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    const auto token = session.FetchCsrfToken();
    REQUIRE(token.IsOk());
    CHECK(token.Value().empty());
    CHECK(state.requests.empty());
    REQUIRE(session.Get("/sap/bc/adt/discovery").IsOk());
    REQUIRE(state.requests.size() == 1);
    CHECK(state.requests[0].headers.at("Accept") == "*/*");
}
TEST_CASE("RFC tunnel never replays uncertain mutations", "[rfc][session]") {
    State state;
    auto client = std::make_unique<Client>(state);
    Error error; error.category = ErrorCategory::Connection; error.message = "connection lost";
    client->responses.push_back(Result<HttpResponse, Error>::Err(error));
    RfcTunnelSession session(std::move(client));
    REQUIRE(session.Post("/sap/bc/adt/test", "data", "text/plain").IsErr());
    REQUIRE(session.Post("/sap/bc/adt/test", "data", "text/plain").IsErr());
    CHECK(state.requests.size() == 1);
}
TEST_CASE("RFC tunnel releases ABAP context at stateful boundary and destruction", "[rfc][session]") {
    State state;
    {
        RfcTunnelSession session(std::make_unique<Client>(state));
        session.SetStateful(true);
        CHECK(session.IsStateful());
        REQUIRE(session.Get("/sap/bc/adt/test").IsOk());
        session.SetStateful(false);
        CHECK_FALSE(session.IsStateful());
        CHECK(state.closes == 1);
    }
    CHECK(state.closes == 2);
}
TEST_CASE("RFC tunnel polls async completion preserving final body", "[rfc][session]") {
    State state;
    auto client = std::make_unique<Client>(state);
    client->responses.push_back(Result<HttpResponse, Error>::Ok(HttpResponse{202, {}, "pending"}));
    client->responses.push_back(Result<HttpResponse, Error>::Ok(HttpResponse{200, {}, "finished"}));
    RfcTunnelSession session(std::move(client), std::chrono::seconds{0});
    const auto result = session.PollUntilComplete("/sap/bc/adt/tasks/1", std::chrono::seconds{2});
    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Completed);
    CHECK(result.Value().body == "finished");
    CHECK(state.requests.size() == 2);
}
TEST_CASE("RFC tunnel preserves server errors without authentication retries", "[rfc][session]") {
    State state;
    auto client = std::make_unique<Client>(state);
    client->responses.push_back(Result<HttpResponse, Error>::Ok(HttpResponse{403,
        {{"Content-Type", "application/xml"}}, "permission denied"}));
    RfcTunnelSession session(std::move(client));
    const auto response = session.Post("/sap/bc/adt/test", "mutation", "text/plain");
    REQUIRE(response.IsOk());
    CHECK(response.Value().status_code == 403);
    CHECK(response.Value().body == "permission denied");
    CHECK(state.requests.size() == 1);
    REQUIRE(session.Get("/sap/bc/adt/test").IsOk());
    CHECK(state.requests.size() == 2);
}
TEST_CASE("RFC tunnel refuses external polling URLs", "[rfc][session]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    REQUIRE(session.Get("https://another-system/sap/bc/adt/test").IsErr());
    REQUIRE(session.Get("//another-system/sap/bc/adt/test").IsErr());
    CHECK(state.requests.empty());
}
TEST_CASE("RFC tunnel polling stops at the deadline", "[rfc][session]") {
    State state;
    auto client = std::make_unique<Client>(state);
    client->responses.push_back(Result<HttpResponse, Error>::Ok(HttpResponse{202, {}, "pending"}));
    RfcTunnelSession session(std::move(client));
    const auto result = session.PollUntilComplete("/sap/bc/adt/tasks/1", std::chrono::seconds{0});
    REQUIRE(result.IsErr());
    CHECK(result.Error().category == ErrorCategory::Timeout);
    CHECK(state.requests.size() == 1);
}
#ifndef ERPL_ADT_WITH_RFC
TEST_CASE("RFC tunnel unavailable builds fail without falling back to HTTP", "[rfc][session]") {
    const auto result = CreateRfcTunnelClient({{"ashost", "localhost"}});
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("ERPL_ADT_WITH_RFC") != std::string::npos);
}
#endif
TEST_CASE("RFC tunnel recovers only between independent operations", "[rfc][session]") {
    State state;
    auto client = std::make_unique<Client>(state);
    Error error; error.category = ErrorCategory::Connection; error.message = "connection lost";
    client->responses.push_back(Result<HttpResponse, Error>::Err(error));
    RfcTunnelSession session(std::move(client));
    session.SetStateful(true);
    REQUIRE(session.Post("/sap/bc/adt/test", "mutation", "text/plain").IsErr());
    REQUIRE(session.Get("/sap/bc/adt/test").IsErr());
    CHECK(state.requests.size() == 1);
    session.RecoverForNextOperation();
    REQUIRE(session.Get("/sap/bc/adt/discovery").IsOk());
    CHECK(state.requests.size() == 2);
    CHECK(state.closes == 1);
    CHECK_FALSE(session.IsStateful());
}
TEST_CASE("RFC tunnel independent healthy operations retain explicit locks", "[rfc][session]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    session.SetStateful(true);
    REQUIRE(session.Post("/sap/bc/adt/test?_action=LOCK", "", "application/xml").IsOk());
    session.RecoverForNextOperation();
    CHECK(session.IsStateful());
    CHECK(state.closes == 0);
}

TEST_CASE("RFC reconnect checks the authenticated identity before sending mutations", "[rfc][session][review]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    session.SetStateful(true);
    REQUIRE(session.Get("/sap/bc/adt/discovery").IsOk());
    session.SetStateful(false);
    SECTION("changed user") { state.reconnected_user = "DIFFERENT_USER"; }
    SECTION("missing user") { state.reconnected_user = ""; }
    const auto changed = session.Post("/sap/bc/adt/test", "mutation", "text/plain");
    REQUIRE(changed.IsErr());
    CHECK(changed.Error().category == ErrorCategory::Authentication);
    CHECK(state.requests.size() == 1);
    CHECK(session.LogonUserName() == "SNC_MAPPED_USER");
    CHECK(session.Get("/sap/bc/adt/test").IsErr());
    CHECK(state.requests.size() == 1);
}

TEST_CASE("RFC reconnect permits the same authenticated identity", "[rfc][session][review]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    session.SetStateful(true);
    REQUIRE(session.Get("/sap/bc/adt/discovery").IsOk());
    session.SetStateful(false);
    state.reconnected_user = "SNC_MAPPED_USER";
    REQUIRE(session.Post("/sap/bc/adt/test", "mutation", "text/plain").IsOk());
    CHECK(state.requests.size() == 2);
}

TEST_CASE("RFC polling preserves failed operation results like HTTP", "[rfc][session][review]") {
    State state;
    auto client = std::make_unique<Client>(state);
    client->responses.push_back(Result<HttpResponse, Error>::Ok(HttpResponse{500, {}, "activation failed"}));
    RfcTunnelSession session(std::move(client));
    const auto result = session.PollUntilComplete("/sap/bc/adt/task/1", std::chrono::seconds{1});
    REQUIRE(result.IsOk());
    CHECK(result.Value().status == PollStatus::Failed);
    CHECK(result.Value().body == "activation failed");
    CHECK(state.requests.size() == 1);
}

TEST_CASE("RFC rejects control characters in resource paths before dispatch", "[rfc][session][review]") {
    State state;
    RfcTunnelSession session(std::make_unique<Client>(state));
    CHECK(session.Get("/sap/bc/adt/test\r\nInjected: header").IsErr());
    const std::string nul_path("/sap/bc/adt/test\0ignored", 24);
    CHECK(session.Get(nul_path).IsErr());
    CHECK(state.requests.empty());
}
