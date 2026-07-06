#pragma once

#include "common/types.h"
#include <string>
#include <map>

namespace backup {

/// Handles reading and applying file metadata (permissions, timestamps, ownership, xattrs)
class MetadataHandler {
public:
    /// Apply all metadata from a FileMetadata struct to a file on disk
    /// @param meta  The metadata to apply
    /// @param full_path  Absolute path to the target file
    /// @return 0 on success, -1 on partial failure (best-effort)
    static int Apply(const FileMetadata& meta, const std::string& full_path);

    /// Apply only permissions (mode)
    static int ApplyPermissions(const FileMetadata& meta, const std::string& path);

    /// Apply only timestamps (mtime, atime)
    static int ApplyTimestamps(const FileMetadata& meta, const std::string& path);

    /// Apply ownership (uid, gid) - requires appropriate privileges
    static int ApplyOwnership(const FileMetadata& meta, const std::string& path);

    /// Apply extended attributes
    static int ApplyXattrs(const std::map<std::string, std::string>& xattrs,
                           const std::string& path);

private:
    MetadataHandler() = delete;
};

} // namespace backup
