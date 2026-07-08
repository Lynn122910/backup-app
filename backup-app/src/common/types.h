#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <iomanip>

namespace backup {

// ============================================================
// File type enumeration
// ============================================================
enum class FileType : uint8_t {
    kRegular     = 0,
    kDirectory   = 1,
    kSymlink     = 2,
    kHardLink    = 3,
    kFifo        = 4,
    kBlockDevice = 5,
    kCharDevice  = 6,
    kSocket      = 7,
    kUnknown     = 99,
};

inline std::string FileTypeToString(FileType type) {
    switch (type) {
        case FileType::kRegular:     return "regular";
        case FileType::kDirectory:   return "directory";
        case FileType::kSymlink:     return "symlink";
        case FileType::kHardLink:    return "hardlink";
        case FileType::kFifo:        return "fifo";
        case FileType::kBlockDevice: return "block_device";
        case FileType::kCharDevice:  return "char_device";
        case FileType::kSocket:      return "socket";
        default:                     return "unknown";
    }
}

inline FileType StringToFileType(const std::string& s) {
    if (s == "regular")      return FileType::kRegular;
    if (s == "directory")    return FileType::kDirectory;
    if (s == "symlink")      return FileType::kSymlink;
    if (s == "hardlink")     return FileType::kHardLink;
    if (s == "fifo")         return FileType::kFifo;
    if (s == "block_device") return FileType::kBlockDevice;
    if (s == "char_device")  return FileType::kCharDevice;
    if (s == "socket")       return FileType::kSocket;
    return FileType::kUnknown;
}

// ============================================================
// File metadata structure
// ============================================================
struct FileMetadata {
    std::string path;               // Relative path from source root
    FileType    type = FileType::kRegular;
    uint64_t    size = 0;           // File size in bytes
    uint32_t    mode = 0;           // st_mode permissions
    uint32_t    uid = 0;            // Owner UID
    std::string owner;              // Owner username
    uint32_t    gid = 0;            // Group GID
    std::string group;              // Group name
    int64_t     mtime_nsec = 0;     // Modification time (nanoseconds since epoch)
    int64_t     atime_nsec = 0;     // Access time
    int64_t     ctime_nsec = 0;     // Status change time
    uint64_t    inode = 0;          // Inode number (for hardlink detection)
    uint32_t    dev_major = 0;      // Device major number
    uint32_t    dev_minor = 0;      // Device minor number
    std::string symlink_target;     // Symlink target path
    std::map<std::string, std::string> xattrs;  // Extended attributes

    // ── Advanced metadata (Linux) ─────────────────────────────
    std::string acl_text;           // POSIX ACL in getfacl/setfacl text format
    std::string capabilities_text;  // Linux capabilities in getcap/setcap text format
    std::string selinux_context;    // SELinux security context

    /// Returns a human-readable file type icon/indicator for display
    std::string TypeString() const { return FileTypeToString(type); }
};

// ============================================================
// Backup file entry (used in manifest)
// ============================================================
struct BackupFileEntry {
    FileMetadata metadata;
    std::string  status = "added";   // "added" | "modified" | "deleted" | "unchanged"
    uint64_t     offset_in_archive = 0;
    uint64_t     stored_size = 0;    // Size in archive (may differ from original)
};

// ============================================================
// Backup type
// ============================================================
enum class BackupType : uint8_t {
    kFull         = 0,
    kIncremental  = 1,
    kDifferential = 2,
};

inline std::string BackupTypeToString(BackupType t) {
    switch (t) {
        case BackupType::kFull:         return "full";
        case BackupType::kIncremental:  return "incremental";
        case BackupType::kDifferential: return "differential";
        default:                        return "unknown";
    }
}

// ============================================================
// Compression algorithm
// ============================================================
enum class CompressionAlgo : uint8_t {
    kNone = 0,
    kZlib = 1,
    kZstd = 2,
};

// ============================================================
// Encryption algorithm
// ============================================================
enum class EncryptionAlgo : uint8_t {
    kNone      = 0,
    kAes256Gcm = 1,
};

// ============================================================
// Filter rule operator
// ============================================================
enum class FilterOp : uint8_t {
    kInclude = 0,
    kExclude = 1,
};

// ============================================================
// Filter rule structure
// ============================================================
struct FilterRule {
    FilterOp                   op = FilterOp::kInclude;
    std::string                description;
    std::optional<std::string> path_glob;
    std::optional<std::string> name_regex;
    std::optional<std::vector<FileType>> file_types;
    std::optional<int64_t>     mtime_after;
    std::optional<int64_t>     mtime_before;
    std::optional<uint64_t>    min_size;
    std::optional<uint64_t>    max_size;
    std::optional<std::string> owner;
    std::optional<std::string> group;
};

// ============================================================
// Retention type
// ============================================================
enum class RetentionType : uint8_t {
    kByCount = 0,
    kByAge   = 1,
    kBySize  = 2,
};

// ============================================================
// Schedule configuration
// ============================================================
struct ScheduleConfig {
    bool          enabled         = false;
    std::string   type;             // "interval" | "daily" | "weekly" | "monthly"
    int           interval_minutes = 0;
    int           hour             = 0;
    int           minute           = 0;
    int           day_of_week      = -1;   // 0=Sun, -1=not set
    int           day_of_month     = -1;   // 1-31, -1=not set
    RetentionType retention_type   = RetentionType::kByCount;
    int           retention_value  = 7;
};

// ============================================================
// Backup task configuration
// ============================================================
struct BackupTaskConfig {
    std::string              id;
    std::string              name;
    std::string              source_dir;
    std::string              dest_dir;
    std::vector<FilterRule>  filters;
    CompressionAlgo          compression       = CompressionAlgo::kNone;
    int                      compression_level = 3;
    EncryptionAlgo           encryption        = EncryptionAlgo::kNone;
    bool                     incremental_enabled = false;
    BackupType               incremental_type   = BackupType::kIncremental;
    ScheduleConfig           schedule;
};

// ============================================================
// Global settings
// ============================================================
struct GlobalSettings {
    std::string language  = "zh_CN";
    std::string log_level = "info";
    std::string log_path;
};

// ============================================================
// Backup progress information
// ============================================================
struct BackupProgress {
    uint64_t    total_files                = 0;
    uint64_t    processed_files            = 0;
    uint64_t    total_bytes                = 0;
    uint64_t    processed_bytes            = 0;
    uint64_t    bytes_per_second           = 0;
    int         elapsed_seconds            = 0;
    int         estimated_remaining_seconds = 0;
    std::string current_file;
    bool        is_running                 = false;
    bool        is_completed               = false;
    std::string error_message;
};

// ============================================================
// Backup options (input to BackupEngine)
// ============================================================
struct BackupOptions {
    std::string   source_dir;
    std::string   dest_path;          // Destination archive file path
    std::string   backup_name;
    BackupType    type                = BackupType::kFull;
    std::string   prev_manifest_path; // For incremental backup
    std::vector<FilterRule> filters;
};

// ============================================================
// Restore options (input to RestoreEngine)
// ============================================================
struct RestoreOptions {
    std::string archive_path;         // Path to backup archive file
    std::string dest_dir;             // Restore destination directory
    std::string password;             // For encrypted archives
    bool        overwrite_existing = true;
    bool        restore_metadata   = true;
};

// ============================================================
// Simple JSON serialization helpers
// (lightweight, avoiding external dependency for basic functionality)
// ============================================================
namespace json {

inline std::string EscapeString(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;
        }
    }
    return result;
}

inline std::string UnescapeString(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                default:   result += s[i];
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

// Trim whitespace
inline std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r'))
        --end;
    return s.substr(start, end - start);
}

// Find a JSON value by key in a simple JSON object string
inline std::optional<std::string> ExtractStringValue(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string::npos) return std::nullopt;

    // Find opening quote
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;

    // Find closing quote — skip escaped quotes (\")
    size_t end = pos + 1;
    while (end < json.size()) {
        if (json[end] == '"') break;
        if (json[end] == '\\' && end + 1 < json.size()) ++end;  // skip escaped char
        ++end;
    }
    if (end >= json.size()) return std::nullopt;

    return UnescapeString(json.substr(pos + 1, end - pos - 1));
}

inline std::optional<int64_t> ExtractIntValue(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string::npos) return std::nullopt;

    // Skip whitespace and colon
    std::string num_str;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '-' || (c >= '0' && c <= '9')) {
            num_str += c;
        } else if (!num_str.empty()) {
            break;
        }
    }
    if (num_str.empty()) return std::nullopt;
    return std::stoll(num_str);
}

inline std::optional<uint64_t> ExtractUintValue(const std::string& json, const std::string& key) {
    auto val = ExtractIntValue(json, key);
    if (val.has_value()) return static_cast<uint64_t>(val.value());
    return std::nullopt;
}

inline std::optional<bool> ExtractBoolValue(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string::npos) return std::nullopt;

    if (json.find("true", pos) < json.find(',', pos + 1) &&
        json.find("true", pos) < json.find('}', pos + 1))
        return true;
    if (json.find("false", pos) < json.find(',', pos + 1) &&
        json.find("false", pos) < json.find('}', pos + 1))
        return false;
    return std::nullopt;
}

// Find start of a JSON array
inline std::optional<std::string> ExtractArrayString(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find('[', pos + search_key.size());
    if (pos == std::string::npos) return std::nullopt;

    int depth = 0;
    size_t end = pos;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '[') ++depth;
        else if (json[i] == ']') { --depth; if (depth == 0) { end = i + 1; break; } }
    }
    return json.substr(pos, end - pos);
}

inline std::optional<std::string> ExtractObjectString(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find('{', pos + search_key.size());
    if (pos == std::string::npos) return std::nullopt;

    int depth = 0;
    size_t end = pos;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        else if (json[i] == '}') { --depth; if (depth == 0) { end = i + 1; break; } }
    }
    return json.substr(pos, end - pos);
}

inline std::vector<std::string> ExtractStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    auto arr = ExtractArrayString(json, key);
    if (!arr.has_value()) return result;

    const std::string& a = arr.value();
    size_t pos = 1; // Skip opening '['
    while (pos < a.size()) {
        size_t q = a.find('"', pos);
        if (q == std::string::npos) break;
        size_t eq = a.find('"', q + 1);
        if (eq == std::string::npos) break;
        result.push_back(UnescapeString(a.substr(q + 1, eq - q - 1)));
        pos = eq + 1;
    }
    return result;
}

// Parse array of JSON objects
inline std::vector<std::string> ExtractObjectArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    auto arr_str = json::ExtractArrayString(json, key);
    if (!arr_str.has_value()) return result;

    const std::string& arr = arr_str.value();
    size_t pos = 1; // Skip opening '['
    while (pos < arr.size()) {
        size_t obj_start = arr.find('{', pos);
        if (obj_start == std::string::npos) break;

        int depth = 0;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < arr.size(); ++i) {
            if (arr[i] == '{') ++depth;
            else if (arr[i] == '}') { --depth; if (depth == 0) { obj_end = i + 1; break; } }
        }
        result.push_back(arr.substr(obj_start, obj_end - obj_start));
        pos = obj_end;
    }
    return result;
}

} // namespace json
} // namespace backup
