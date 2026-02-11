#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace erpl_adt {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

// Abstract log sink — implementations decide where/how to write.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, std::string_view component,
                       std::string_view message) = 0;
};

// Console sink — human-readable output to stderr.
class ConsoleSink : public ILogSink {
public:
    void Write(LogLevel level, std::string_view component,
               std::string_view message) override;
};

// Color console sink — colored, compact output to a stream.
// When use_color is false, falls back to the same format as ConsoleSink.
class ColorConsoleSink : public ILogSink {
public:
    explicit ColorConsoleSink(bool use_color, std::ostream& out = std::cerr);
    void Write(LogLevel level, std::string_view component,
               std::string_view message) override;
private:
    bool use_color_;
    std::ostream& out_;
};

// JSON sink — machine-readable JSON lines to a stream.
class JsonSink : public ILogSink {
public:
    explicit JsonSink(std::ostream& out);
    void Write(LogLevel level, std::string_view component,
               std::string_view message) override;
private:
    std::ostream& out_;
};

// Thread-safe logger that dispatches to a sink.
class Logger {
public:
    explicit Logger(std::unique_ptr<ILogSink> sink,
                    LogLevel min_level = LogLevel::Info);

    void SetLevel(LogLevel level);

    void Debug(std::string_view component, std::string_view message);
    void Info(std::string_view component, std::string_view message);
    void Warn(std::string_view component, std::string_view message);
    void Error(std::string_view component, std::string_view message);

private:
    void Log(LogLevel level, std::string_view component,
             std::string_view message);

    std::unique_ptr<ILogSink> sink_;
    LogLevel min_level_;
    std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Global logger — set once at startup, used by all components.
// ---------------------------------------------------------------------------

/// Initialize the global logger. Must be called before any logging.
void InitGlobalLogger(std::unique_ptr<ILogSink> sink, LogLevel min_level);

/// Get the global logger. Returns a no-op logger if not initialized.
Logger& GlobalLogger();

/// Convenience free functions that forward to GlobalLogger().
void LogDebug(std::string_view component, std::string_view message);
void LogInfo(std::string_view component, std::string_view message);
void LogWarn(std::string_view component, std::string_view message);
void LogError(std::string_view component, std::string_view message);

} // namespace erpl_adt
