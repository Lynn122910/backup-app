#pragma once

#include "common/types.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace backup {

/// Progress callback type
using ScanProgressCallback = std::function<void(uint64_t scanned, uint64_t total)>;

/// File scanner: recursively traverses directories and collects file metadata
class FileScanner {
public:
    FileScanner();
    ~FileScanner();

    FileScanner(FileScanner&&) = default;
    FileScanner& operator=(FileScanner&&) = default;
    FileScanner(const FileScanner&) = delete;
    FileScanner& operator=(const FileScanner&) = delete;

    /// Scan a directory recursively, returning file metadata for all entries
    /// @param root_path  Absolute path to scan
    /// @param progress_cb  Optional progress callback
    /// @return List of file metadata entries
    std::vector<FileMetadata> Scan(const std::string& root_path,
                                    ScanProgressCallback progress_cb = nullptr);

    /// Count total files (non-recursive count of directories)
    uint64_t CountFiles(const std::string& root_path);

    /// Calculate total size of all regular files
    uint64_t TotalSize(const std::string& root_path);

    /// Cancel an ongoing scan
    void Cancel();

private:
    /// Recursive scan implementation
    void ScanRecursive(const std::string& root_path,
                       const std::string& current_path,
                       std::vector<FileMetadata>& results,
                       ScanProgressCallback progress_cb);

    /// Gather metadata for a single file
    FileMetadata GatherMetadata(const std::string& full_path,
                                const std::string& relative_path,
                                const struct ::stat& st);

    /// Determine file type from st_mode
    static FileType ModeToFileType(mode_t mode);

    /// Read extended attributes
    std::map<std::string, std::string> ReadXattrs(const std::string& path);

    /// Read timestamps with nanosecond precision from stat
    static int64_t StatMtimeNsec(const struct ::stat& st);
    static int64_t StatAtimeNsec(const struct ::stat& st);
    static int64_t StatCtimeNsec(const struct ::stat& st);

    /// Resolve username from UID
    static std::string ResolveUserName(uint32_t uid);

    /// Resolve group name from GID
    static std::string ResolveGroupName(uint32_t gid);

    std::atomic<bool> cancelled_{false};
    uint64_t total_found_ = 0;
    std::map<uint64_t, std::string> inode_map_;  // inode → first-seen relative path (hardlink detection)
};

} // namespace backup
