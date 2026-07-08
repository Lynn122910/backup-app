#include "services/file_scanner.h"
#include "common/logger.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

// Extended attributes (Linux)
#ifdef __linux__
#include <sys/xattr.h>
#endif

// Advanced metadata (optional, build system may not have these libraries)
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

FileScanner::FileScanner()  = default;
FileScanner::~FileScanner() = default;

void FileScanner::Cancel() {
    cancelled_.store(true, std::memory_order_release);
}

std::vector<FileMetadata> FileScanner::Scan(const std::string& root_path,
                                             ScanProgressCallback progress_cb) {
    std::vector<FileMetadata> results;
    cancelled_.store(false, std::memory_order_release);
    inode_map_.clear();  // Reset hardlink tracking for each scan

    // Verify root path exists
    struct ::stat root_st;
    if (stat(root_path.c_str(), &root_st) != 0) {
        LOG_ERROR << "Source directory not found: " << root_path;
        return results;
    }
    if (!S_ISDIR(root_st.st_mode)) {
        LOG_ERROR << "Source path is not a directory: " << root_path;
        return results;
    }

    LOG_INFO << "Scanning directory: " << root_path;

    ScanRecursive(root_path, "", results, progress_cb);

    // Sort by path for consistent ordering
    std::sort(results.begin(), results.end(),
              [](const FileMetadata& a, const FileMetadata& b) {
                  return a.path < b.path;
              });

    LOG_INFO << "Scan complete: " << results.size() << " files found";
    return results;
}

uint64_t FileScanner::CountFiles(const std::string& root_path) {
    auto files = Scan(root_path);
    uint64_t count = 0;
    for (const auto& f : files) {
        if (f.type != FileType::kDirectory) ++count;
    }
    return count;
}

uint64_t FileScanner::TotalSize(const std::string& root_path) {
    auto files = Scan(root_path);
    uint64_t total = 0;
    for (const auto& f : files) {
        if (f.type == FileType::kRegular) total += f.size;
    }
    return total;
}

void FileScanner::ScanRecursive(const std::string& root_path,
                                 const std::string& relative_dir,
                                 std::vector<FileMetadata>& results,
                                 ScanProgressCallback progress_cb) {
    if (cancelled_.load(std::memory_order_acquire)) return;

    std::string full_dir = root_path;
    if (!relative_dir.empty()) {
        full_dir += "/" + relative_dir;
    }

    DIR* dir = opendir(full_dir.c_str());
    if (!dir) {
        LOG_WARNING << "Cannot open directory: " << full_dir
                    << " - " << strerror(errno);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (cancelled_.load(std::memory_order_acquire)) {
            closedir(dir);
            return;
        }

        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string child_rel = relative_dir.empty()
            ? entry->d_name
            : relative_dir + "/" + entry->d_name;
        std::string child_full = root_path + "/" + child_rel;

        struct ::stat st;
        if (lstat(child_full.c_str(), &st) != 0) {
            LOG_WARNING << "Cannot stat: " << child_full << " - " << strerror(errno);
            continue;
        }

        // Gather metadata
        FileMetadata meta = GatherMetadata(child_full, child_rel, st);

        // Hardlink detection: if we've seen this inode before, mark as kHardLink
        if (meta.type == FileType::kRegular && st.st_nlink > 1) {
            auto it = inode_map_.find(meta.inode);
            if (it != inode_map_.end()) {
                // Duplicate inode → this is a hardlink
                meta.type = FileType::kHardLink;
                meta.symlink_target = it->second;  // Path to the first occurrence
                meta.size = 0;  // Hardlinks carry no data of their own
            } else {
                // First time seeing this inode → track it
                inode_map_[meta.inode] = child_rel;
            }
        }

        results.push_back(meta);
        ++total_found_;

        // Progress callback
        if (progress_cb) {
            progress_cb(total_found_, 0);  // total unknown during scan
        }

        // Recursively scan subdirectories
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            // Note: lstat on a symlink to directory won't have S_ISDIR set
            // But we handle this by checking if it's a symlink first
            // Actually lstat doesn't follow symlinks, so S_ISDIR on lstat result
            // means it's actually a directory, not a symlink to one
            ScanRecursive(root_path, child_rel, results, progress_cb);
        }
    }
    closedir(dir);
}

FileMetadata FileScanner::GatherMetadata(const std::string& full_path,
                                          const std::string& relative_path,
                                          const struct ::stat& st) {
    FileMetadata meta;
    meta.path    = relative_path;
    meta.type    = ModeToFileType(st.st_mode);
    meta.size    = static_cast<uint64_t>(st.st_size);
    meta.mode    = st.st_mode & 07777;
    meta.uid     = st.st_uid;
    meta.gid     = st.st_gid;
    meta.inode   = st.st_ino;
    meta.dev_major = major(st.st_dev);
    meta.dev_minor = minor(st.st_dev);

    meta.mtime_nsec = StatMtimeNsec(st);
    meta.atime_nsec = StatAtimeNsec(st);
    meta.ctime_nsec = StatCtimeNsec(st);

    meta.owner = ResolveUserName(st.st_uid);
    meta.group = ResolveGroupName(st.st_gid);

    // Handle special file types
    switch (meta.type) {
        case FileType::kSymlink: {
            char buf[PATH_MAX];
            ssize_t len = readlink(full_path.c_str(), buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                meta.symlink_target = buf;
            }
            break;
        }
        case FileType::kBlockDevice:
        case FileType::kCharDevice:
            meta.dev_major = major(st.st_rdev);
            meta.dev_minor = minor(st.st_rdev);
            break;
        default:
            break;
    }

    // Read extended attributes (Linux)
    meta.xattrs = ReadXattrs(full_path);

    // Read advanced metadata (best-effort, silently skip on failure)
    meta.acl_text        = ReadAcl(full_path);
    meta.capabilities_text = ReadCapabilities(full_path);
    meta.selinux_context = ReadSelinuxContext(full_path);

    return meta;
}

FileType FileScanner::ModeToFileType(mode_t mode) {
    if (S_ISREG(mode))  return FileType::kRegular;
    if (S_ISDIR(mode))  return FileType::kDirectory;
    if (S_ISLNK(mode))  return FileType::kSymlink;
    if (S_ISFIFO(mode)) return FileType::kFifo;
    if (S_ISBLK(mode))  return FileType::kBlockDevice;
    if (S_ISCHR(mode))  return FileType::kCharDevice;
    if (S_ISSOCK(mode)) return FileType::kSocket;
    return FileType::kUnknown;
}

int64_t FileScanner::StatMtimeNsec(const struct ::stat& st) {
    int64_t nsec = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL;
#ifdef st_mtime  // POSIX.1-2008
    nsec += st.st_mtim.tv_nsec;
#else
    nsec += 0;
#endif
    return nsec;
}

int64_t FileScanner::StatAtimeNsec(const struct ::stat& st) {
    int64_t nsec = static_cast<int64_t>(st.st_atim.tv_sec) * 1000000000LL;
#ifdef st_atime
    nsec += st.st_atim.tv_nsec;
#endif
    return nsec;
}

int64_t FileScanner::StatCtimeNsec(const struct ::stat& st) {
    int64_t nsec = static_cast<int64_t>(st.st_ctim.tv_sec) * 1000000000LL;
#ifdef st_ctime
    nsec += st.st_ctim.tv_nsec;
#endif
    return nsec;
}

std::string FileScanner::ResolveUserName(uint32_t uid) {
    struct passwd pwd_buf;
    struct passwd* result = nullptr;
    char buf[4096];

    int ret = getpwuid_r(static_cast<uid_t>(uid), &pwd_buf, buf, sizeof(buf), &result);
    if (ret == 0 && result != nullptr) {
        return std::string(result->pw_name);
    }
    return std::to_string(uid);
}

std::string FileScanner::ResolveGroupName(uint32_t gid) {
    struct group grp_buf;
    struct group* result = nullptr;
    char buf[4096];

    int ret = getgrgid_r(static_cast<gid_t>(gid), &grp_buf, buf, sizeof(buf), &result);
    if (ret == 0 && result != nullptr) {
        return std::string(result->gr_name);
    }
    return std::to_string(gid);
}

std::map<std::string, std::string> FileScanner::ReadXattrs(const std::string& path) {
    std::map<std::string, std::string> attrs;
#ifdef __linux__
    // Get list of xattr names
    ssize_t list_size = listxattr(path.c_str(), nullptr, 0);
    if (list_size <= 0) return attrs;

    std::vector<char> list_buf(list_size);
    list_size = listxattr(path.c_str(), list_buf.data(), list_buf.size());
    if (list_size <= 0) return attrs;

    // Parse the null-separated list
    size_t pos = 0;
    while (pos < static_cast<size_t>(list_size)) {
        std::string name(list_buf.data() + pos);
        pos += name.size() + 1;

        // Get value
        ssize_t val_size = getxattr(path.c_str(), name.c_str(), nullptr, 0);
        if (val_size > 0) {
            std::vector<char> val_buf(val_size);
            val_size = getxattr(path.c_str(), name.c_str(), val_buf.data(), val_buf.size());
            if (val_size > 0) {
                attrs[name] = std::string(val_buf.data(), val_size);
            }
        }
    }
#endif
    return attrs;
}

std::string FileScanner::ReadAcl(const std::string& path) {
#ifdef HAVE_ACL
    acl_t acl = acl_get_file(path.c_str(), ACL_TYPE_ACCESS);
    if (!acl) return "";

    char* text = acl_to_text(acl, nullptr);
    acl_free(acl);
    if (!text) return "";

    std::string result(text);
    acl_free(text);

    // Also try default ACL for directories
    acl_t def_acl = acl_get_file(path.c_str(), ACL_TYPE_DEFAULT);
    if (def_acl) {
        char* def_text = acl_to_text(def_acl, nullptr);
        acl_free(def_acl);
        if (def_text) {
            result += "\n#default:\n";
            result += def_text;
            acl_free(def_text);
        }
    }
    return result;
#else
    return "";
#endif
}

std::string FileScanner::ReadCapabilities(const std::string& path) {
#ifdef HAVE_CAP
    cap_t caps = cap_get_file(path.c_str());
    if (!caps) return "";

    char* text = cap_to_text(caps, nullptr);
    cap_free(caps);
    if (!text) return "";

    std::string result(text);
    cap_free(text);
    return result;
#else
    return "";
#endif
}

std::string FileScanner::ReadSelinuxContext(const std::string& path) {
#ifdef HAVE_SELINUX
    security_context_t ctx = nullptr;
    if (getfilecon(path.c_str(), &ctx) < 0) return "";
    std::string result(ctx);
    freecon(ctx);
    return result;
#else
    return "";
#endif
}

} // namespace backup
