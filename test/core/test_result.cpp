#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/result.hpp>

#include <memory>
#include <string>
#include <utility>

using namespace erpl_adt;

// ===========================================================================
// Basic Ok / Err
// ===========================================================================

TEST_CASE("Result: Ok result holds value", "[result]") {
    auto r = Result<int, std::string>::Ok(42);
    REQUIRE(r.IsOk());
    REQUIRE_FALSE(r.IsErr());
    CHECK(static_cast<bool>(r));
    CHECK(r.Value() == 42);
}

TEST_CASE("Result: Err result holds error", "[result]") {
    auto r = Result<int, std::string>::Err("failure");
    REQUIRE(r.IsErr());
    REQUIRE_FALSE(r.IsOk());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(r.Error() == "failure");
}

// ===========================================================================
// ValueOr
// ===========================================================================

TEST_CASE("Result: ValueOr returns value on Ok", "[result]") {
    auto r = Result<int, std::string>::Ok(42);
    CHECK(r.ValueOr(0) == 42);
}

TEST_CASE("Result: ValueOr returns default on Err", "[result]") {
    auto r = Result<int, std::string>::Err("fail");
    CHECK(r.ValueOr(99) == 99);
}

// ===========================================================================
// AndThen
// ===========================================================================

TEST_CASE("Result: AndThen chains on Ok", "[result]") {
    auto r = Result<int, std::string>::Ok(10);
    auto r2 = r.AndThen([](int v) -> Result<std::string, std::string> {
        return Result<std::string, std::string>::Ok(std::to_string(v * 2));
    });
    REQUIRE(r2.IsOk());
    CHECK(r2.Value() == "20");
}

TEST_CASE("Result: AndThen short-circuits on Err", "[result]") {
    auto r = Result<int, std::string>::Err("bad");
    bool called = false;
    auto r2 = r.AndThen([&called](int v) -> Result<std::string, std::string> {
        called = true;
        return Result<std::string, std::string>::Ok(std::to_string(v));
    });
    CHECK_FALSE(called);
    REQUIRE(r2.IsErr());
    CHECK(r2.Error() == "bad");
}

TEST_CASE("Result: AndThen chains multiple", "[result]") {
    auto r = Result<int, std::string>::Ok(5)
        .AndThen([](int v) -> Result<int, std::string> {
            return Result<int, std::string>::Ok(v + 10);
        })
        .AndThen([](int v) -> Result<int, std::string> {
            return Result<int, std::string>::Ok(v * 2);
        });
    REQUIRE(r.IsOk());
    CHECK(r.Value() == 30);
}

TEST_CASE("Result: AndThen chain stops at first Err", "[result]") {
    auto r = Result<int, std::string>::Ok(5)
        .AndThen([](int) -> Result<int, std::string> {
            return Result<int, std::string>::Err("stop here");
        })
        .AndThen([](int v) -> Result<int, std::string> {
            return Result<int, std::string>::Ok(v * 100);
        });
    REQUIRE(r.IsErr());
    CHECK(r.Error() == "stop here");
}

// ===========================================================================
// Map
// ===========================================================================

TEST_CASE("Result: Map transforms value on Ok", "[result]") {
    auto r = Result<int, std::string>::Ok(7);
    auto r2 = r.Map([](int v) { return v * 3; });
    REQUIRE(r2.IsOk());
    CHECK(r2.Value() == 21);
}

TEST_CASE("Result: Map passes through Err", "[result]") {
    auto r = Result<int, std::string>::Err("nope");
    bool called = false;
    auto r2 = r.Map([&called](int v) {
        called = true;
        return v * 3;
    });
    CHECK_FALSE(called);
    REQUIRE(r2.IsErr());
    CHECK(r2.Error() == "nope");
}

TEST_CASE("Result: Map changes type", "[result]") {
    auto r = Result<int, std::string>::Ok(42);
    auto r2 = r.Map([](int v) -> std::string { return std::to_string(v); });
    REQUIRE(r2.IsOk());
    CHECK(r2.Value() == "42");
}

// ===========================================================================
// Copy semantics
// ===========================================================================

TEST_CASE("Result: copy Ok", "[result]") {
    auto r1 = Result<std::string, int>::Ok("hello");
    auto r2 = r1;
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    CHECK(r1.Value() == "hello");
    CHECK(r2.Value() == "hello");
}

TEST_CASE("Result: copy Err", "[result]") {
    auto r1 = Result<std::string, int>::Err(404);
    auto r2 = r1;
    REQUIRE(r1.IsErr());
    REQUIRE(r2.IsErr());
    CHECK(r1.Error() == 404);
    CHECK(r2.Error() == 404);
}

// ===========================================================================
// Move semantics
// ===========================================================================

TEST_CASE("Result: move Ok value out", "[result]") {
    auto r = Result<std::string, int>::Ok("moveable");
    auto val = std::move(r).Value();
    CHECK(val == "moveable");
}

TEST_CASE("Result: move Err value out", "[result]") {
    auto r = Result<int, std::string>::Err("moved error");
    auto err = std::move(r).Error();
    CHECK(err == "moved error");
}

TEST_CASE("Result: move construct", "[result]") {
    auto r1 = Result<std::string, int>::Ok("data");
    auto r2 = std::move(r1);
    REQUIRE(r2.IsOk());
    CHECK(r2.Value() == "data");
}

TEST_CASE("Result: move-only type in Ok", "[result]") {
    auto r = Result<std::unique_ptr<int>, std::string>::Ok(std::make_unique<int>(42));
    REQUIRE(r.IsOk());
    auto ptr = std::move(r).Value();
    REQUIRE(ptr != nullptr);
    CHECK(*ptr == 42);
}

TEST_CASE("Result: ValueOr with move-only rvalue", "[result]") {
    auto r = Result<std::string, int>::Ok("original");
    auto val = std::move(r).ValueOr("default");
    CHECK(val == "original");
}

TEST_CASE("Result: ValueOr rvalue returns default on Err", "[result]") {
    auto r = Result<std::string, int>::Err(1);
    auto val = std::move(r).ValueOr("default");
    CHECK(val == "default");
}

// ===========================================================================
// Error struct
// ===========================================================================

TEST_CASE("Error: ToString with all fields", "[error]") {
    Error e{"Clone", "/sap/bc/adt/abapgit/repos", 500, "Internal Server Error",
            "ABAP runtime error"};
    auto s = e.ToString();
    CHECK(s.find("Clone") != std::string::npos);
    CHECK(s.find("/sap/bc/adt/abapgit/repos") != std::string::npos);
    CHECK(s.find("HTTP 500") != std::string::npos);
    CHECK(s.find("Internal Server Error") != std::string::npos);
    CHECK(s.find("ABAP runtime error") != std::string::npos);
}

TEST_CASE("Error: ToString without optional fields", "[error]") {
    Error e{"Connect", "", std::nullopt, "timeout", std::nullopt};
    auto s = e.ToString();
    CHECK(s.find("Connect") != std::string::npos);
    CHECK(s.find("timeout") != std::string::npos);
    CHECK(s.find("HTTP") == std::string::npos);
    CHECK(s.find("SAP") == std::string::npos);
}

TEST_CASE("Error: equality", "[error]") {
    Error e1{"Op", "/ep", 200, "ok", std::nullopt};
    Error e2{"Op", "/ep", 200, "ok", std::nullopt};
    Error e3{"Op", "/ep", 201, "ok", std::nullopt};
    CHECK(e1 == e2);
    CHECK(e1 != e3);
}

TEST_CASE("Result with Error type", "[result][error]") {
    auto r = Result<std::string, Error>::Err(
        Error{"Fetch", "/sap/bc/adt/discovery", 401, "Unauthorized", std::nullopt});
    REQUIRE(r.IsErr());
    CHECK(r.Error().http_status == 401);
    CHECK(r.Error().operation == "Fetch");
}

// ===========================================================================
// ErrorCategory & ExitCode
// ===========================================================================

TEST_CASE("Error: default category is Internal", "[error]") {
    Error e{"Op", "", std::nullopt, "msg", std::nullopt};
    CHECK(e.category == ErrorCategory::Internal);
    CHECK(e.ExitCode() == 99);
}

TEST_CASE("Error: ExitCode mapping", "[error]") {
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Connection}.ExitCode() == 1);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Authentication}.ExitCode() == 1);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::CsrfToken}.ExitCode() == 1);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::NotFound}.ExitCode() == 2);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::PackageError}.ExitCode() == 2);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::CloneError}.ExitCode() == 3);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::PullError}.ExitCode() == 4);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::ActivationError}.ExitCode() == 5);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::LockConflict}.ExitCode() == 6);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::TestFailure}.ExitCode() == 7);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::CheckError}.ExitCode() == 8);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::TransportError}.ExitCode() == 9);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Timeout}.ExitCode() == 10);
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Internal}.ExitCode() == 99);
}

TEST_CASE("Error: CategoryName", "[error]") {
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Connection}.CategoryName() == "connection");
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Timeout}.CategoryName() == "timeout");
    CHECK(Error{"", "", std::nullopt, "", std::nullopt, ErrorCategory::Internal}.CategoryName() == "internal");
}

TEST_CASE("Error: ToJson contains required fields", "[error]") {
    Error e{"Clone", "/sap/bc/adt/repos", 500, "failed", "ABAP dump",
            ErrorCategory::CloneError};
    auto json = e.ToJson();
    CHECK(json.find("\"category\":\"clone\"") != std::string::npos);
    CHECK(json.find("\"operation\":\"Clone\"") != std::string::npos);
    CHECK(json.find("\"endpoint\":\"/sap/bc/adt/repos\"") != std::string::npos);
    CHECK(json.find("\"http_status\":500") != std::string::npos);
    CHECK(json.find("\"message\":\"failed\"") != std::string::npos);
    CHECK(json.find("\"sap_error\":\"ABAP dump\"") != std::string::npos);
    CHECK(json.find("\"exit_code\":3") != std::string::npos);
}

TEST_CASE("Error: ToJson without optional fields", "[error]") {
    Error e{"Connect", "", std::nullopt, "timeout", std::nullopt};
    auto json = e.ToJson();
    CHECK(json.find("\"category\":\"internal\"") != std::string::npos);
    CHECK(json.find("\"endpoint\"") == std::string::npos);
    CHECK(json.find("\"http_status\"") == std::string::npos);
    CHECK(json.find("\"sap_error\"") == std::string::npos);
}

TEST_CASE("Error: ToJson escapes special characters", "[error]") {
    Error e{
        "Op\"Quoted\"",
        "/path",
        500,
        "line1\nline2\t\"quoted\"",
        std::string("backslash\\value"),
        ErrorCategory::Internal};
    auto json = e.ToJson();
    CHECK(json.find("\\n") != std::string::npos);
    CHECK(json.find("\\t") != std::string::npos);
    CHECK(json.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(json.find("backslash\\\\value") != std::string::npos);
}

TEST_CASE("Error: equality includes category", "[error]") {
    Error e1{"Op", "", std::nullopt, "msg", std::nullopt, ErrorCategory::Connection};
    Error e2{"Op", "", std::nullopt, "msg", std::nullopt, ErrorCategory::Connection};
    Error e3{"Op", "", std::nullopt, "msg", std::nullopt, ErrorCategory::Timeout};
    CHECK(e1 == e2);
    CHECK(e1 != e3);
}

// ===========================================================================
// Error::FromHttpStatus
// ===========================================================================

TEST_CASE("FromHttpStatus: 401 maps to Authentication", "[error]") {
    auto e = Error::FromHttpStatus("Search", "/endpoint", 401);
    CHECK(e.category == ErrorCategory::Authentication);
    CHECK(e.http_status.value() == 401);
    CHECK(e.operation == "Search");
    CHECK(e.endpoint == "/endpoint");
    CHECK(e.message.find("login") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 403 maps to CsrfToken", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 403);
    CHECK(e.category == ErrorCategory::CsrfToken);
    CHECK(e.message.find("Forbidden") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 403 includes SAP message when available", "[error]") {
    std::string body = R"(<exc:exception><exc:message>Package $DEMO_SOI_DRAFT does not exist</exc:message></exc:exception>)";
    auto e = Error::FromHttpStatus("Lock", "/ep", 403, body);
    CHECK(e.category == ErrorCategory::CsrfToken);
    CHECK(e.message.find("Package $DEMO_SOI_DRAFT does not exist") != std::string::npos);
    REQUIRE(e.sap_error.has_value());
    CHECK(e.sap_error.value() == "Package $DEMO_SOI_DRAFT does not exist");
}

TEST_CASE("FromHttpStatus: 400 includes SAP message when available", "[error]") {
    std::string body = R"(<exc:exception><exc:message>Malformed XML payload</exc:message></exc:exception>)";
    auto e = Error::FromHttpStatus("Op", "/ep", 400, body);
    CHECK(e.category == ErrorCategory::Internal);
    CHECK(e.message.find("Malformed XML payload") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 404 maps to NotFound", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 404);
    CHECK(e.category == ErrorCategory::NotFound);
    CHECK(e.message.find("Not found") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 409 maps to LockConflict", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 409);
    CHECK(e.category == ErrorCategory::LockConflict);
    CHECK(e.message.find("locked") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 423 maps to LockConflict", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 423);
    CHECK(e.category == ErrorCategory::LockConflict);
    CHECK(e.message.find("locked") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 500 maps to Internal", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 500);
    CHECK(e.category == ErrorCategory::Internal);
    CHECK(e.message.find("internal error") != std::string::npos);
}

TEST_CASE("FromHttpStatus: 408 maps to Timeout", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 408);
    CHECK(e.category == ErrorCategory::Timeout);
}

TEST_CASE("FromHttpStatus: 429 maps to Timeout", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 429);
    CHECK(e.category == ErrorCategory::Timeout);
}

TEST_CASE("FromHttpStatus: 500 includes SAP error in message", "[error]") {
    std::string body = R"(<exc:exception><exc:message>BW Search is not activated</exc:message></exc:exception>)";
    auto e = Error::FromHttpStatus("Op", "/ep", 500, body);
    CHECK(e.category == ErrorCategory::Internal);
    CHECK(e.message.find("BW Search is not activated") != std::string::npos);
    REQUIRE(e.sap_error.has_value());
    CHECK(e.sap_error.value() == "BW Search is not activated");
}

TEST_CASE("FromHttpStatus: 502/503/504 map to Connection", "[error]") {
    for (int code : {502, 503, 504}) {
        auto e = Error::FromHttpStatus("Op", "/ep", code);
        CHECK(e.category == ErrorCategory::Connection);
        CHECK(e.message.find("unavailable") != std::string::npos);
    }
}

TEST_CASE("FromHttpStatus: unknown code maps to Internal", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 418);
    CHECK(e.category == ErrorCategory::Internal);
    CHECK(e.message.find("418") != std::string::npos);
}

TEST_CASE("FromHttpStatus: extracts SAP message from XML body", "[error]") {
    std::string body = R"(<?xml version="1.0"?><error><message>User DEVELOPER is locked</message></error>)";
    auto e = Error::FromHttpStatus("Op", "/ep", 401, body);
    REQUIRE(e.sap_error.has_value());
    CHECK(e.sap_error.value() == "User DEVELOPER is locked");
}

TEST_CASE("FromHttpStatus: extracts exc:message from XML body", "[error]") {
    std::string body = R"(<exc:exception><exc:message>Object ZCL_FOO not found</exc:message></exc:exception>)";
    auto e = Error::FromHttpStatus("Op", "/ep", 404, body);
    REQUIRE(e.sap_error.has_value());
    CHECK(e.sap_error.value() == "Object ZCL_FOO not found");
}

TEST_CASE("FromHttpStatus: empty body yields no sap_error", "[error]") {
    auto e = Error::FromHttpStatus("Op", "/ep", 500, "");
    CHECK_FALSE(e.sap_error.has_value());
}

TEST_CASE("FromHttpStatus: HTML body without XML tags yields no sap_error", "[error]") {
    std::string body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
    auto e = Error::FromHttpStatus("Op", "/ep", 500, body);
    CHECK_FALSE(e.sap_error.has_value());
}

// ===========================================================================
// Hint field
// ===========================================================================

TEST_CASE("Error: ToString includes hint when present", "[error]") {
    Error e;
    e.operation = "BwSearch";
    e.message = "Server error";
    e.hint = "Activate BW Search in transaction RSOSM";
    auto s = e.ToString();
    CHECK(s.find("Hint: Activate BW Search") != std::string::npos);
}

TEST_CASE("Error: ToString omits hint when absent", "[error]") {
    Error e;
    e.operation = "Search";
    e.message = "Not found";
    auto s = e.ToString();
    CHECK(s.find("Hint") == std::string::npos);
}

TEST_CASE("Error: ToJson includes hint field when present", "[error]") {
    Error e;
    e.operation = "BwSearch";
    e.endpoint = "/bw";
    e.message = "err";
    e.hint = "Use RSOSM";
    auto json = e.ToJson();
    CHECK(json.find("\"hint\":\"Use RSOSM\"") != std::string::npos);
}

TEST_CASE("Error: ToJson omits hint field when absent", "[error]") {
    Error e;
    e.operation = "Search";
    e.message = "err";
    auto json = e.ToJson();
    CHECK(json.find("hint") == std::string::npos);
}

TEST_CASE("Error: equality includes hint", "[error]") {
    Error e1;
    e1.operation = "Op";
    e1.message = "msg";
    e1.hint = "some hint";

    Error e2;
    e2.operation = "Op";
    e2.message = "msg";
    e2.hint = "some hint";

    Error e3;
    e3.operation = "Op";
    e3.message = "msg";
    e3.hint = "different hint";

    Error e4;
    e4.operation = "Op";
    e4.message = "msg";

    CHECK(e1 == e2);
    CHECK(e1 != e3);
    CHECK(e1 != e4);
}
