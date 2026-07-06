#include "common/file_utils.h"
#include "common/logger.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cerrno>
#include <fstream>

namespace backup {

bool FileUtils::Exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool FileUtils::IsDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool FileUtils::IsRegularFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool FileUtils::IsSymlink(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    return S_ISLNK(st.st_mode);
}

uint64_t FileUtils::GetFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
}

bool FileUtils::CreateDirectoryRecursive(const std::string& path) {
    if (path.empty()) return false;

    // Normalize
    std::string norm_path = NormalizePath(path);

    // Already exists
    struct stat st;
    if (stat(norm_path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Create parent first
    std::string parent = GetParentPath(norm_path);
    if (!parent.empty() && parent != norm_path && parent != "/") {
        if (!CreateDirectoryRecursive(parent)) {
            return false;
        }
    }

    // Create this directory
    if (mkdir(norm_path.c_str(), 0755) != 0) {
        // If directory was created by another process between our check and mkdir
        if (errno == EEXIST) return true;
        LOG_ERROR << "Failed to create directory: " << norm_path
                  << " - " << strerror(errno);
        return false;
    }
    return true;
}

std::string FileUtils::GetParentPath(const std::string& path) {
    std::string norm = NormalizePath(path);
    // Remove trailing slash
    while (norm.size() > 1 && norm.back() == '/') {
        norm.pop_back();
    }
    size_t pos = norm.rfind('/');
    if (pos == std::string::npos) return "";
    if (pos == 0) return "/";  // Root
    return norm.substr(0, pos);
}

std::string FileUtils::GetFileName(const std::string& path) {
    std::string norm = NormalizePath(path);
    // Remove trailing slash
    while (norm.size() > 1 && norm.back() == '/') {
        norm.pop_back();
    }
    size_t pos = norm.rfind('/');
    if (pos == std::string::npos) return norm;
    return norm.substr(pos + 1);
}

std::string FileUtils::JoinPath(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    if (child.empty()) return base;
    if (child[0] == '/') return child;  // child is absolute

    std::string result = base;
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    result += '/';
    result += child;
    return result;
}

std::string FileUtils::NormalizePath(const std::string& path) {
    std::string result;
    result.reserve(path.size());

    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '\\') {
            result += '/';
        } else {
            result += path[i];
        }
    }
    return result;
}

uint64_t FileUtils::GetAvailableDiskSpace(const std::string& path) {
    struct statvfs vfs;
    if (statvfs(path.c_str(), &vfs) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
}

std::string FileUtils::GenerateTimestampName() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);

    char buf[64];
    snprintf(buf, sizeof(buf), "backup_%04d%02d%02d_%02d%02d%02d_%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             static_cast<int>(ms.count()));
    return std::string(buf);
}

std::string FileUtils::ReadFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

bool FileUtils::WriteStringToFile(const std::string& path, const std::string& content) {
    // Ensure parent directory exists
    std::string parent = GetParentPath(path);
    if (!parent.empty() && !CreateDirectoryRecursive(parent)) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR << "Failed to open file for writing: " << path;
        return false;
    }
    file.write(content.data(), content.size());
    file.close();
    return file.good();
}

int64_t FileUtils::GetCurrentTimeNsec() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

std::string FileUtils::FormatTimestamp(int64_t nsec) {
    auto duration = std::chrono::nanoseconds(nsec);
    auto time_point = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));
    auto time_t_val = std::chrono::system_clock::to_time_t(time_point);

    std::tm tm_buf;
    localtime_r(&time_t_val, &tm_buf);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return std::string(buf);
}

int64_t FileUtils::ParseTimestamp(const std::string& time_str) {
    std::tm tm_buf = {};
    if (time_str.size() >= 19) {
        // Format: "YYYY-MM-DD HH:MM:SS"
        sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d",
               &tm_buf.tm_year, &tm_buf.tm_mon, &tm_buf.tm_mday,
               &tm_buf.tm_hour, &tm_buf.tm_min, &tm_buf.tm_sec);
        tm_buf.tm_year -= 1900;
        tm_buf.tm_mon -= 1;
    }
    auto time_point = std::chrono::system_clock::from_time_t(mktime(&tm_buf));
    auto duration = time_point.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

} // namespace backup
