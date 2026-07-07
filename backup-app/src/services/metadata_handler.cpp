#include "services/metadata_handler.h"
#include "common/logger.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

#ifdef __linux__
#include <sys/xattr.h>
#endif

namespace backup {

int MetadataHandler::Apply(const FileMetadata& meta, const std::string& full_path) {
    int errors = 0;
    int ret;

    // 1. Permissions first (some operations may require write permission)
    ret = ApplyPermissions(meta, full_path);
    if (ret != 0) ++errors;

    // 2. Ownership (requires CAP_CHOWN or root, may fail gracefully)
    ret = ApplyOwnership(meta, full_path);
    if (ret != 0) ++errors;

    // 3. Timestamps
    ret = ApplyTimestamps(meta, full_path);
    if (ret != 0) ++errors;

    // 4. Extended attributes
    ret = ApplyXattrs(meta.xattrs, full_path);
    if (ret != 0) ++errors;

    if (errors > 0) {
        LOG_WARNING << "Applied metadata to " << full_path
                    << " with " << errors << " non-critical failures";
    }
    return errors > 0 ? -1 : 0;
}

int MetadataHandler::ApplyPermissions(const FileMetadata& meta, const std::string& path) {
    if (path.empty()) return -1;

    // Don't change permissions of symlinks (chmod follows the target)
    struct ::stat st;
    if (lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode)) {
        return 0;  // Skip symlinks
    }

    if (chmod(path.c_str(), meta.mode) != 0) {
        LOG_WARNING << "Failed to chmod " << path
                    << " to " << std::oct << meta.mode
                    << ": " << strerror(errno);
        return -1;
    }
    return 0;
}

int MetadataHandler::ApplyTimestamps(const FileMetadata& meta, const std::string& path) {
    if (path.empty()) return -1;

    struct timespec times[2];
    times[0].tv_sec  = meta.atime_nsec / 1000000000LL;
    times[0].tv_nsec = meta.atime_nsec % 1000000000LL;
    times[1].tv_sec  = meta.mtime_nsec / 1000000000LL;
    times[1].tv_nsec = meta.mtime_nsec % 1000000000LL;

    // Use utimensat with AT_SYMLINK_NOFOLLOW — no open() needed, so this
    // never blocks on FIFOs (POSIX FIFO open-for-read blocks until a writer
    // connects, which would hang the restore).
    if (utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) != 0) {
        LOG_WARNING << "Failed to set timestamps on " << path
                    << ": " << strerror(errno);
        return -1;
    }
    return 0;
}

int MetadataHandler::ApplyOwnership(const FileMetadata& meta, const std::string& path) {
    if (path.empty()) return -1;

    // Skip symlinks
    struct ::stat st;
    if (lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode)) {
        return 0;
    }

    if (lchown(path.c_str(), static_cast<uid_t>(meta.uid),
               static_cast<gid_t>(meta.gid)) != 0) {
        // Ownership change failure is non-fatal (often due to non-root user)
        LOG_WARNING << "Failed to chown " << path
                    << " to " << meta.uid << ":" << meta.gid
                    << ": " << strerror(errno);
        return -1;
    }
    return 0;
}

int MetadataHandler::ApplyXattrs(const std::map<std::string, std::string>& xattrs,
                                  const std::string& path) {
    if (xattrs.empty() || path.empty()) return 0;
#ifdef __linux__
    int errors = 0;
    for (const auto& [name, value] : xattrs) {
        if (setxattr(path.c_str(), name.c_str(), value.data(), value.size(), 0) != 0) {
            LOG_WARNING << "Failed to set xattr " << name << " on " << path
                        << ": " << strerror(errno);
            ++errors;
        }
    }
    return errors > 0 ? -1 : 0;
#else
    return 0;
#endif
}

} // namespace backup
