#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/log.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace erpl_adt;

// ===========================================================================
// Helper: a sink that captures messages into a vector.
// ===========================================================================

struct CapturedMessage {
    LogLevel level;
    std::string component;
    std::string message;
};

class CaptureSink : public ILogSink {
public:
    void Write(LogLevel level, std::string_view component,
               std::string_view message) override {
        messages.push_back(
            {level, std::string(component), std::string(message)});
    }

    std::vector<CapturedMessage> messages;
};

// ===========================================================================
// ConsoleSink
// ===========================================================================

TEST_CASE("ConsoleSink: writes without crashing", "[log]") {
    ConsoleSink sink;
    // Just verify it doesn't throw or crash.
    sink.Write(LogLevel::Info, "test", "hello from console sink");
    sink.Write(LogLevel::Error, "test", "error from console sink");
}

// ===========================================================================
// JsonSink
// ===========================================================================

TEST_CASE("JsonSink: writes valid JSON lines", "[log]") {
    std::ostringstream oss;
    JsonSink sink(oss);

    sink.Write(LogLevel::Info, "deploy", "started");

    auto line = oss.str();
    CHECK(line.find("\"level\":\"INFO\"") != std::string::npos);
    CHECK(line.find("\"component\":\"deploy\"") != std::string::npos);
    CHECK(line.find("\"message\":\"started\"") != std::string::npos);
    CHECK(line.find("\"ts\":\"") != std::string::npos);
    // Must end with newline
    REQUIRE(!line.empty());
    CHECK(line.back() == '\n');
}

TEST_CASE("JsonSink: each write produces one line", "[log]") {
    std::ostringstream oss;
    JsonSink sink(oss);

    sink.Write(LogLevel::Debug, "a", "first");
    sink.Write(LogLevel::Warn, "b", "second");

    auto output = oss.str();
    // Count newlines — should be exactly 2
    auto count = std::count(output.begin(), output.end(), '\n');
    CHECK(count == 2);
}

TEST_CASE("JsonSink: all log levels produce correct names", "[log]") {
    std::ostringstream oss;
    JsonSink sink(oss);

    sink.Write(LogLevel::Debug, "x", "d");
    sink.Write(LogLevel::Info, "x", "i");
    sink.Write(LogLevel::Warn, "x", "w");
    sink.Write(LogLevel::Error, "x", "e");

    auto output = oss.str();
    CHECK(output.find("\"level\":\"DEBUG\"") != std::string::npos);
    CHECK(output.find("\"level\":\"INFO\"") != std::string::npos);
    CHECK(output.find("\"level\":\"WARN\"") != std::string::npos);
    CHECK(output.find("\"level\":\"ERROR\"") != std::string::npos);
}

TEST_CASE("JsonSink: escapes special characters in message", "[log]") {
    std::ostringstream oss;
    JsonSink sink(oss);

    sink.Write(LogLevel::Info, "esc", "line1\nline2\ttab \"quoted\" back\\slash");

    auto output = oss.str();
    CHECK(output.find("\\n") != std::string::npos);
    CHECK(output.find("\\t") != std::string::npos);
    CHECK(output.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(output.find("back\\\\slash") != std::string::npos);
}

// ===========================================================================
// Logger: level filtering
// ===========================================================================

TEST_CASE("Logger: respects min_level", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Warn);

    logger.Debug("c", "should be filtered");
    logger.Info("c", "should be filtered");
    logger.Warn("c", "should pass");
    logger.Error("c", "should pass");

    REQUIRE(sink_ptr->messages.size() == 2);
    CHECK(sink_ptr->messages[0].level == LogLevel::Warn);
    CHECK(sink_ptr->messages[1].level == LogLevel::Error);
}

TEST_CASE("Logger: Debug level passes all messages", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Debug);

    logger.Debug("c", "d");
    logger.Info("c", "i");
    logger.Warn("c", "w");
    logger.Error("c", "e");

    CHECK(sink_ptr->messages.size() == 4);
}

TEST_CASE("Logger: Error level only passes errors", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Error);

    logger.Debug("c", "d");
    logger.Info("c", "i");
    logger.Warn("c", "w");
    logger.Error("c", "e");

    REQUIRE(sink_ptr->messages.size() == 1);
    CHECK(sink_ptr->messages[0].level == LogLevel::Error);
}

TEST_CASE("Logger: SetLevel changes filtering dynamically", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Error);

    logger.Info("c", "filtered");
    CHECK(sink_ptr->messages.empty());

    logger.SetLevel(LogLevel::Info);
    logger.Info("c", "now passes");
    REQUIRE(sink_ptr->messages.size() == 1);
    CHECK(sink_ptr->messages[0].message == "now passes");
}

// ===========================================================================
// Logger: message content
// ===========================================================================

TEST_CASE("Logger: preserves component and message", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Debug);

    logger.Info("workflow", "step completed");

    REQUIRE(sink_ptr->messages.size() == 1);
    CHECK(sink_ptr->messages[0].component == "workflow");
    CHECK(sink_ptr->messages[0].message == "step completed");
    CHECK(sink_ptr->messages[0].level == LogLevel::Info);
}

// ===========================================================================
// Logger: thread safety
// ===========================================================================

TEST_CASE("Logger: concurrent logging does not crash", "[log]") {
    auto sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();
    Logger logger(std::move(sink), LogLevel::Debug);

    constexpr int kThreads = 8;
    constexpr int kMessagesPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&logger, t]() {
            for (int i = 0; i < kMessagesPerThread; ++i) {
                logger.Info("thread-" + std::to_string(t),
                            "msg-" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    CHECK(sink_ptr->messages.size() == kThreads * kMessagesPerThread);
}

// ===========================================================================
// ColorConsoleSink
// ===========================================================================

TEST_CASE("ColorConsoleSink: plain mode matches ConsoleSink format", "[log]") {
    std::ostringstream oss;
    ColorConsoleSink sink(false, oss);

    sink.Write(LogLevel::Info, "http", "GET /sap/bc/adt/discovery");

    auto output = oss.str();
    // Should contain ISO timestamp, level name, component, message.
    CHECK(output.find("[INFO]") != std::string::npos);
    CHECK(output.find("[http]") != std::string::npos);
    CHECK(output.find("GET /sap/bc/adt/discovery") != std::string::npos);
    // No ANSI escape codes in plain mode.
    CHECK(output.find("\033[") == std::string::npos);
}

TEST_CASE("ColorConsoleSink: color mode contains ANSI escape codes", "[log]") {
    std::ostringstream oss;
    ColorConsoleSink sink(true, oss);

    sink.Write(LogLevel::Info, "http", "GET /sap/bc/adt/discovery");

    auto output = oss.str();
    // Should contain ANSI escape sequences.
    CHECK(output.find("\033[") != std::string::npos);
    // Should contain the message text.
    CHECK(output.find("GET /sap/bc/adt/discovery") != std::string::npos);
    // Should end with reset + newline.
    CHECK(output.back() == '\n');
}

TEST_CASE("ColorConsoleSink: each level produces distinct ANSI codes", "[log]") {
    std::ostringstream debug_oss, info_oss, warn_oss, error_oss;
    ColorConsoleSink debug_sink(true, debug_oss);
    ColorConsoleSink info_sink(true, info_oss);
    ColorConsoleSink warn_sink(true, warn_oss);
    ColorConsoleSink error_sink(true, error_oss);

    debug_sink.Write(LogLevel::Debug, "x", "msg");
    info_sink.Write(LogLevel::Info, "x", "msg");
    warn_sink.Write(LogLevel::Warn, "x", "msg");
    error_sink.Write(LogLevel::Error, "x", "msg");

    // DEBUG: dim gray \033[90m
    CHECK(debug_oss.str().find("\033[90m") != std::string::npos);
    // INFO: cyan \033[36m
    CHECK(info_oss.str().find("\033[36m") != std::string::npos);
    // WARN: yellow \033[33m
    CHECK(warn_oss.str().find("\033[33m") != std::string::npos);
    // ERROR: bold red \033[1;31m
    CHECK(error_oss.str().find("\033[1;31m") != std::string::npos);
}

TEST_CASE("ColorConsoleSink: ERROR messages get red text", "[log]") {
    std::ostringstream oss;
    ColorConsoleSink sink(true, oss);

    sink.Write(LogLevel::Error, "search", "HTTP 404: Object not found");

    auto output = oss.str();
    // Error message text should be wrapped in red.
    // The bold red code should appear at least twice (level tag + message).
    auto first = output.find("\033[1;31m");
    REQUIRE(first != std::string::npos);
    auto second = output.find("\033[1;31m", first + 1);
    CHECK(second != std::string::npos);
}

TEST_CASE("ColorConsoleSink: color mode has short timestamp", "[log]") {
    std::ostringstream oss;
    ColorConsoleSink sink(true, oss);

    sink.Write(LogLevel::Info, "x", "test");

    auto output = oss.str();
    // Short timestamp format HH:MM:SS — should NOT contain 'T' or 'Z' (ISO).
    CHECK(output.find('T') == std::string::npos);
    CHECK(output.find('Z') == std::string::npos);
}

TEST_CASE("ColorConsoleSink: thread safety with color", "[log]") {
    std::ostringstream oss;
    auto sink = std::make_unique<ColorConsoleSink>(true, oss);
    Logger logger(std::move(sink), LogLevel::Debug);

    constexpr int kThreads = 8;
    constexpr int kMessages = 50;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&logger, t]() {
            for (int i = 0; i < kMessages; ++i) {
                logger.Info("thread-" + std::to_string(t),
                            "msg-" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    // Count newlines — should equal total messages.
    auto output = oss.str();
    auto count = std::count(output.begin(), output.end(), '\n');
    CHECK(count == kThreads * kMessages);
}
