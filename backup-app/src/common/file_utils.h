#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sys/stat.h>

namespace backup {

/// File system utility functions
class FileUtils {
public:
    /// Check if a path exists
    static bool Exists(const std::string& path);

    /// Check if path is a directory
    static bool IsDirectory(const std::string& path);

    /// Check if path is a regular file
    static bool IsRegularFile(const std::string& path);

    /// Check if path is a symbolic link
    static bool IsSymlink(const std::string& path);

    /// Get file size in bytes
    static uint64_t GetFileSize(const std::string& path);

    /// Create directory (recursive, like mkdir -p)
    /// Returns true on success or if directory already exists
    static bool CreateDirectoryRecursive(const std::string& path);

    /// Get parent directory of a path
    static std::string GetParentPath(const std::string& path);

    /// Get filename from a full path
    static std::string GetFileName(const std::string& path);

    /// Join two path components
    static std::string JoinPath(const std::string& base, const std::string& child);

    /// Normalize path separators
    static std::string NormalizePath(const std::string& path);

    /// Get available disk space on the filesystem containing a path (in bytes)
    static uint64_t GetAvailableDiskSpace(const std::string& path);

    /// Generate a unique filename based on timestamp
    static std::string GenerateTimestampName();

    /// Read entire file into a string
    static std::string ReadFileToString(const std::string& path);

    /// Write string to file
    static bool WriteStringToFile(const std::string& path, const std::string& content);

    /// Get current timestamp in nanoseconds since epoch
    static int64_t GetCurrentTimeNsec();

    /// Format nanoseconds timestamp to human-readable string
    static std::string FormatTimestamp(int64_t nsec);

    /// Parse human-readable timestamp string back to nanoseconds
    static int64_t ParseTimestamp(const std::string& time_str);

private:
    FileUtils() = delete;
};

} // namespace backup
