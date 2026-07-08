#pragma once

#include "common/types.h"
#include <string>
#include <map>

namespace backup {

/// Handles reading and applying file metadata (permissions, timestamps, ownership,
/// xattrs, ACLs, capabilities, SELinux context).
class MetadataHandler {
public:
    /// Apply all metadata from a FileMetadata struct to a file on disk
    /// @param meta  The metadata to apply
    /// @param full_path  Absolute path to the target file
    /// @return 0 on success, positive count of non-critical failures
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

    /// Apply POSIX ACL from text representation
    static int ApplyAcl(const std::string& acl_text, const std::string& path);

    /// Apply Linux capabilities from text representation
    static int ApplyCapabilities(const std::string& cap_text, const std::string& path);

    /// Apply SELinux security context
    static int ApplySelinuxContext(const std::string& context, const std::string& path);

private:
    MetadataHandler() = delete;
};

} // namespace backup
