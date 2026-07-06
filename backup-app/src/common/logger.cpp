#include "common/logger.h"
#include <iostream>
#include <ctime>

namespace backup {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::string& log_file_path, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = min_level;
    if (!log_file_path.empty()) {
        log_file_.open(log_file_path, std::ios::out | std::ios::app);
        if (log_file_.is_open()) {
            file_enabled_ = true;
        }
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < min_level_) return;

    std::string formatted = FormatMessage(level, message);

    std::lock_guard<std::mutex> lock(mutex_);

    // Write to stdout
    std::cout << formatted << std::endl;

    // Write to log file
    if (file_enabled_ && log_file_.is_open()) {
        log_file_ << formatted << std::endl;
        log_file_.flush();
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
        file_enabled_ = false;
    }
}

Logger::~Logger() {
    Shutdown();
}

std::string Logger::FormatMessage(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);

    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             static_cast<int>(ms.count()));

    std::ostringstream oss;
    oss << "[" << time_buf << "] "
        << "[" << LogLevelToString(level) << "] "
        << message;
    return oss.str();
}

} // namespace backup
