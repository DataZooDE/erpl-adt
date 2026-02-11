#include <erpl_adt/core/log.hpp>
#include <erpl_adt/core/ansi.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace erpl_adt {

namespace {

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

std::string Iso8601Now() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time_t_now);
#else
    gmtime_r(&time_t_now, &utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count()
        << 'Z';
    return oss.str();
}

// Escape a string for JSON output (handles \, ", and control characters).
void JsonEscape(std::ostream& out, std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u"
                        << std::hex << std::setfill('0') << std::setw(4)
                        << static_cast<int>(static_cast<unsigned char>(c))
                        << std::dec;
                } else {
                    out << c;
                }
                break;
        }
    }
}

std::string HhMmSsNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time_t_now);
#else
    localtime_r(&time_t_now, &local);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local, "%H:%M:%S");
    return oss.str();
}

// ANSI escape code helpers.
const char* LevelAnsi(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return ansi::kDim;     // dim gray
        case LogLevel::Info:  return ansi::kCyan;    // cyan
        case LogLevel::Warn:  return ansi::kYellow;  // yellow
        case LogLevel::Error: return ansi::kRed;     // bold red
    }
    return "";
}

// Fixed-width 5-char level tag (right-padded).
const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "     ";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ConsoleSink
// ---------------------------------------------------------------------------
void ConsoleSink::Write(LogLevel level, std::string_view component,
                        std::string_view message) {
    std::cerr << Iso8601Now()
              << " [" << LevelName(level) << "] "
              << "[" << component << "] "
              << message << '\n';
}

// ---------------------------------------------------------------------------
// ColorConsoleSink
// ---------------------------------------------------------------------------
ColorConsoleSink::ColorConsoleSink(bool use_color, std::ostream& out)
    : use_color_(use_color), out_(out) {}

void ColorConsoleSink::Write(LogLevel level, std::string_view component,
                             std::string_view message) {
    if (!use_color_) {
        // Plain format: same as ConsoleSink but to configurable stream.
        out_ << Iso8601Now()
             << " [" << LevelName(level) << "] "
             << "[" << component << "] "
             << message << '\n';
        return;
    }

    // Colored compact format:
    // HH:MM:SS LEVEL [component] message
    const auto* level_color = LevelAnsi(level);

    // Dim timestamp.
    out_ << ansi::kDim << HhMmSsNow() << ansi::kReset << ' ';

    // Colored level tag.
    out_ << level_color << LevelTag(level) << ansi::kReset << ' ';

    // Dim component.
    out_ << ansi::kDim << '[' << component << ']' << ansi::kReset << ' ';

    // Message: ERROR gets red, others get default.
    if (level == LogLevel::Error) {
        out_ << level_color << message << ansi::kReset;
    } else {
        out_ << message;
    }
    out_ << '\n';
}

// ---------------------------------------------------------------------------
// JsonSink
// ---------------------------------------------------------------------------
JsonSink::JsonSink(std::ostream& out) : out_(out) {}

void JsonSink::Write(LogLevel level, std::string_view component,
                     std::string_view message) {
    out_ << "{\"ts\":\"" << Iso8601Now()
         << "\",\"level\":\"" << LevelName(level)
         << "\",\"component\":\"";
    JsonEscape(out_, component);
    out_ << "\",\"message\":\"";
    JsonEscape(out_, message);
    out_ << "\"}\n";
}

// ---------------------------------------------------------------------------
// Logger
// ---------------------------------------------------------------------------
Logger::Logger(std::unique_ptr<ILogSink> sink, LogLevel min_level)
    : sink_(std::move(sink)), min_level_(min_level) {}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

void Logger::Debug(std::string_view component, std::string_view message) {
    Log(LogLevel::Debug, component, message);
}

void Logger::Info(std::string_view component, std::string_view message) {
    Log(LogLevel::Info, component, message);
}

void Logger::Warn(std::string_view component, std::string_view message) {
    Log(LogLevel::Warn, component, message);
}

void Logger::Error(std::string_view component, std::string_view message) {
    Log(LogLevel::Error, component, message);
}

void Logger::Log(LogLevel level, std::string_view component,
                 std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) >= static_cast<int>(min_level_)) {
        sink_->Write(level, component, message);
    }
}

// ---------------------------------------------------------------------------
// NullSink — discards all messages (used before InitGlobalLogger is called).
// ---------------------------------------------------------------------------
namespace {

class NullSink : public ILogSink {
public:
    void Write(LogLevel, std::string_view, std::string_view) override {}
};

std::unique_ptr<Logger>& GlobalLoggerInstance() {
    static auto instance = std::make_unique<Logger>(
        std::make_unique<NullSink>(), LogLevel::Error);
    return instance;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Global logger
// ---------------------------------------------------------------------------
void InitGlobalLogger(std::unique_ptr<ILogSink> sink, LogLevel min_level) {
    GlobalLoggerInstance() = std::make_unique<Logger>(std::move(sink), min_level);
}

Logger& GlobalLogger() {
    return *GlobalLoggerInstance();
}

void LogDebug(std::string_view component, std::string_view message) {
    GlobalLogger().Debug(component, message);
}

void LogInfo(std::string_view component, std::string_view message) {
    GlobalLogger().Info(component, message);
}

void LogWarn(std::string_view component, std::string_view message) {
    GlobalLogger().Warn(component, message);
}

void LogError(std::string_view component, std::string_view message) {
    GlobalLogger().Error(component, message);
}

} // namespace erpl_adt
