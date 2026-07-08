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
#ifdef HAVE_ACL
#include <sys/acl.h>
#endif
#ifdef HAVE_CAP
#include <sys/capability.h>
#endif
#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
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

    // 5. POSIX ACL
    if (!meta.acl_text.empty()) {
        ret = ApplyAcl(meta.acl_text, full_path);
        if (ret != 0) ++errors;
    }

    // 6. Linux capabilities
    if (!meta.capabilities_text.empty()) {
        ret = ApplyCapabilities(meta.capabilities_text, full_path);
        if (ret != 0) ++errors;
    }

    // 7. SELinux context
    if (!meta.selinux_context.empty()) {
        ret = ApplySelinuxContext(meta.selinux_context, full_path);
        if (ret != 0) ++errors;
    }

    if (errors > 0) {
        LOG_WARNING << "Applied metadata to " << full_path
                    << " with " << errors << " non-critical failures";
    }
    return errors;
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

int MetadataHandler::ApplyAcl(const std::string& acl_text, const std::string& path) {
#ifdef HAVE_ACL
    if (acl_text.empty() || path.empty()) return 0;

    std::string access_text = acl_text;
    std::string default_text;

    size_t def_pos = acl_text.find("\n#default:\n");
    if (def_pos != std::string::npos) {
        access_text  = acl_text.substr(0, def_pos);
        default_text = acl_text.substr(def_pos + 12);
    }

    acl_t acl = acl_from_text(access_text.c_str());
    if (acl) {
        if (acl_set_file(path.c_str(), ACL_TYPE_ACCESS, acl) != 0) {
            LOG_WARNING << "Failed to set access ACL on " << path
                        << ": " << strerror(errno);
            acl_free(acl);
            return 1;
        }
        acl_free(acl);
    } else {
        LOG_WARNING << "Failed to parse ACL text for " << path;
        return 1;
    }

    if (!default_text.empty()) {
        acl_t def_acl = acl_from_text(default_text.c_str());
        if (def_acl) {
            if (acl_set_file(path.c_str(), ACL_TYPE_DEFAULT, def_acl) != 0) {
                LOG_WARNING << "Failed to set default ACL on " << path
                            << ": " << strerror(errno);
                acl_free(def_acl);
                return 1;
            }
            acl_free(def_acl);
        }
    }
    return 0;
#else
    return 0;
#endif
}

int MetadataHandler::ApplyCapabilities(const std::string& cap_text, const std::string& path) {
#ifdef HAVE_CAP
    if (cap_text.empty() || path.empty()) return 0;

    cap_t caps = cap_from_text(cap_text.c_str());
    if (!caps) {
        LOG_WARNING << "Failed to parse capability text for " << path;
        return 1;
    }

    if (cap_set_file(path.c_str(), caps) != 0) {
        LOG_WARNING << "Failed to set capabilities on " << path
                    << ": " << strerror(errno);
        cap_free(caps);
        return 1;
    }
    cap_free(caps);
    return 0;
#else
    return 0;
#endif
}

int MetadataHandler::ApplySelinuxContext(const std::string& context, const std::string& path) {
#ifdef HAVE_SELINUX
    if (context.empty() || path.empty()) return 0;

    if (setfilecon(path.c_str(), context.c_str()) < 0) {
        LOG_WARNING << "Failed to set SELinux context on " << path
                    << ": " << strerror(errno);
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace backup
