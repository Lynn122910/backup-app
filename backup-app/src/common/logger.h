#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace backup {

/// Log levels
enum class LogLevel {
    kDebug   = 0,
    kInfo    = 1,
    kWarning = 2,
    kError   = 3,
};

inline std::string LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:   return "DEBUG";
        case LogLevel::kInfo:    return "INFO";
        case LogLevel::kWarning: return "WARN";
        case LogLevel::kError:   return "ERROR";
        default:                 return "UNKNOWN";
    }
}

/// Simple thread-safe logger
/// Writes to both stdout and an optional log file
class Logger {
public:
    /// Get singleton instance
    static Logger& Instance();

    /// Initialize with optional log file path
    void Init(const std::string& log_file_path = "", LogLevel min_level = LogLevel::kInfo);

    /// Log a message
    void Log(LogLevel level, const std::string& message);

    /// Convenience methods
    void Debug(const std::string& msg)   { Log(LogLevel::kDebug, msg); }
    void Info(const std::string& msg)    { Log(LogLevel::kInfo, msg); }
    void Warning(const std::string& msg) { Log(LogLevel::kWarning, msg); }
    void Error(const std::string& msg)   { Log(LogLevel::kError, msg); }

    /// Set minimum log level
    void SetLevel(LogLevel level) { min_level_ = level; }

    /// Shutdown (close log file)
    void Shutdown();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string FormatMessage(LogLevel level, const std::string& message);

    std::mutex   mutex_;
    LogLevel     min_level_ = LogLevel::kInfo;
    std::ofstream log_file_;
    bool         file_enabled_ = false;
};

/// Stream-based log helper
class LogStream {
public:
    LogStream(LogLevel level, const char* file, int line)
        : level_(level), file_(file), line_(line) {}

    ~LogStream() {
        Logger::Instance().Log(level_, ss_.str());
    }

    std::ostringstream& Stream() { return ss_; }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream ss_;
};

// Convenience macros
#define LOG_DEBUG   backup::LogStream(backup::LogLevel::kDebug,   __FILE__, __LINE__).Stream()
#define LOG_INFO    backup::LogStream(backup::LogLevel::kInfo,    __FILE__, __LINE__).Stream()
#define LOG_WARNING backup::LogStream(backup::LogLevel::kWarning, __FILE__, __LINE__).Stream()
#define LOG_ERROR   backup::LogStream(backup::LogLevel::kError,   __FILE__, __LINE__).Stream()

} // namespace backup
