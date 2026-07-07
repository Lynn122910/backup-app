#include "core/backup_archive.h"
#include "core/backup_manifest.h"
#include "common/logger.h"
#include "common/file_utils.h"

#include <openssl/sha.h>
#include <cstring>
#include <fstream>
#include <algorithm>

// Platform-specific includes for file I/O
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>

namespace backup {

// ============================================================
// Archive magic and version constants
// ============================================================
static const uint32_t ARCHIVE_MAGIC   = 0x30504B42;  // "BKP0" in little-endian
static const uint16_t ARCHIVE_VERSION = 1;

// ============================================================
// SHA-256 context wrapper
// ============================================================
class Sha256Context {
public:
    Sha256Context()  { SHA256_Init(&ctx_); }
    void Update(const void* data, size_t len) { SHA256_Update(&ctx_, data, len); }
    void Final(std::vector<uint8_t>& out) {
        out.resize(SHA256_DIGEST_LENGTH);
        SHA256_Final(out.data(), &ctx_);
    }
private:
    SHA256_CTX ctx_;
};

// ============================================================
// BackupArchiveWriter implementation
// ============================================================
BackupArchiveWriter::BackupArchiveWriter() {
    sha256_ctx_ = new Sha256Context();
}

BackupArchiveWriter::~BackupArchiveWriter() {
    Close();
    delete static_cast<Sha256Context*>(sha256_ctx_);
    sha256_ctx_ = nullptr;
}

bool BackupArchiveWriter::Open(const std::string& file_path) {
    if (open_) Close();

    file_path_ = file_path;

    // Ensure parent directory exists
    std::string parent = file_path_.substr(0, file_path_.rfind('/'));
    if (!parent.empty() && parent != file_path_) {
        FileUtils::CreateDirectoryRecursive(parent);
    }

    file_.open(file_path_, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        LOG_ERROR << "Failed to create archive file: " << file_path_;
        return false;
    }

    // Reset SHA-256 BEFORE writing header so the header is included in the checksum
    delete static_cast<Sha256Context*>(sha256_ctx_);
    sha256_ctx_ = new Sha256Context();

    // Write header
    if (!WriteUint32(ARCHIVE_MAGIC))   { Close(); return false; }
    if (!WriteUint16(ARCHIVE_VERSION)) { Close(); return false; }
    if (!WriteUint16(0))              { Close(); return false; } // flags reserved

    open_ = true;
    file_count_ = 0;
    finalized_ = false;

    LOG_INFO << "Created archive: " << file_path_;
    return true;
}

bool BackupArchiveWriter::WriteManifest(const std::string& manifest_json) {
    if (!open_) return false;

    // Write manifest size and data
    uint64_t size = manifest_json.size();
    if (!WriteUint64(size)) return false;
    if (!WriteBytes(manifest_json.data(), size)) return false;

    LOG_INFO << "Manifest written to archive (" << size << " bytes)";
    return true;
}

bool BackupArchiveWriter::WriteFileData(const BackupFileEntry& entry,
                                         const std::vector<uint8_t>& data) {
    if (!open_) return false;

    // Write path
    uint16_t path_len = static_cast<uint16_t>(entry.metadata.path.size());
    if (!WriteUint16(path_len)) return false;
    if (!WriteBytes(entry.metadata.path.data(), path_len)) return false;

    // Write metadata
    uint8_t ftype = static_cast<uint8_t>(entry.metadata.type);
    if (!WriteBytes(&ftype, 1)) return false;
    if (!WriteUint32(entry.metadata.mode)) return false;
    if (!WriteUint64(entry.metadata.size)) return false;
    if (!WriteUint64(data.size())) return false;  // stored size

    // Write file data
    if (!data.empty()) {
        if (!WriteBytes(data.data(), data.size())) return false;
    }

    ++file_count_;
    return true;
}

bool BackupArchiveWriter::WriteFileFromDisk(const BackupFileEntry& entry,
                                              const std::string& source_base_dir) {
    if (!open_) return false;

    std::string full_path = FileUtils::JoinPath(source_base_dir, entry.metadata.path);

    // Handle non-regular files (symlinks, directories, etc.)
    if (entry.metadata.type != FileType::kRegular) {
        // For non-regular files, write empty data with special handling
        std::vector<uint8_t> empty;
        return WriteFileData(entry, empty);
    }

    // Open source file
    int src_fd = open(full_path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        LOG_ERROR << "Failed to open source file: " << full_path;
        return false;
    }

    // Write header: path
    uint16_t path_len = static_cast<uint16_t>(entry.metadata.path.size());
    if (!WriteUint16(path_len)) { close(src_fd); return false; }
    if (!WriteBytes(entry.metadata.path.data(), path_len)) { close(src_fd); return false; }

    // Write metadata
    uint8_t ftype = static_cast<uint8_t>(entry.metadata.type);
    if (!WriteBytes(&ftype, 1)) { close(src_fd); return false; }
    if (!WriteUint32(entry.metadata.mode)) { close(src_fd); return false; }
    if (!WriteUint64(entry.metadata.size)) { close(src_fd); return false; }
    if (!WriteUint64(entry.metadata.size)) { close(src_fd); return false; } // stored_size (same as original for now)

    // Stream file content in chunks
    uint64_t remaining = entry.metadata.size;
    uint64_t total_written = 0;
    std::vector<uint8_t> buffer(1024 * 1024);  // 1MB buffer

    while (remaining > 0) {
        size_t to_read = std::min(static_cast<size_t>(remaining), buffer.size());
        ssize_t bytes_read = read(src_fd, buffer.data(), to_read);
        if (bytes_read <= 0) {
            LOG_ERROR << "Failed to read from source file: " << full_path;
            close(src_fd);
            return false;
        }
        if (!WriteBytes(buffer.data(), bytes_read)) {
            close(src_fd);
            return false;
        }
        remaining -= bytes_read;
        total_written += bytes_read;
    }

    close(src_fd);
    ++file_count_;
    return true;
}

bool BackupArchiveWriter::Finalize() {
    if (!open_ || finalized_) return false;

    // Write footer: file count
    if (!WriteUint64(file_count_)) return false;

    // Write checksum
    auto* sha = static_cast<Sha256Context*>(sha256_ctx_);
    sha->Final(final_checksum_);
    if (!WriteBytes(final_checksum_.data(), final_checksum_.size())) return false;

    file_.flush();
    finalized_ = true;

    LOG_INFO << "Archive finalized: " << file_count_ << " files, checksum written";
    return true;
}

void BackupArchiveWriter::Close() {
    if (file_.is_open()) {
        file_.close();
    }
    open_ = false;
}

bool BackupArchiveWriter::IsOpen() const { return open_; }

std::string BackupArchiveWriter::FilePath() const { return file_path_; }

bool BackupArchiveWriter::WriteBytes(const void* data, size_t size) {
    if (!file_.is_open()) return false;
    file_.write(static_cast<const char*>(data), size);
    if (!file_.good()) return false;

    // Update running checksum
    UpdateChecksum(data, size);
    return true;
}

bool BackupArchiveWriter::WriteUint16(uint16_t value) {
    // Little-endian
    uint8_t buf[2] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF)
    };
    return WriteBytes(buf, 2);
}

bool BackupArchiveWriter::WriteUint32(uint32_t value) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    return WriteBytes(buf, 4);
}

bool BackupArchiveWriter::WriteUint64(uint64_t value) {
    uint8_t buf[8] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 32) & 0xFF),
        static_cast<uint8_t>((value >> 40) & 0xFF),
        static_cast<uint8_t>((value >> 48) & 0xFF),
        static_cast<uint8_t>((value >> 56) & 0xFF)
    };
    return WriteBytes(buf, 8);
}

void BackupArchiveWriter::UpdateChecksum(const void* data, size_t size) {
    auto* sha = static_cast<Sha256Context*>(sha256_ctx_);
    sha->Update(data, size);
}

// ============================================================
// BackupArchiveReader implementation
// ============================================================
BackupArchiveReader::BackupArchiveReader()  = default;
BackupArchiveReader::~BackupArchiveReader() { Close(); }

bool BackupArchiveReader::Open(const std::string& file_path) {
    if (open_) Close();

    file_path_ = file_path;
    file_.open(file_path_, std::ios::binary);
    if (!file_.is_open()) {
        LOG_ERROR << "Failed to open archive: " << file_path_;
        return false;
    }

    // Read and verify header
    uint32_t magic;
    uint16_t version, flags;

    if (!ReadUint32(magic) || magic != ARCHIVE_MAGIC) {
        LOG_ERROR << "Invalid archive magic: " << file_path_;
        Close();
        return false;
    }
    if (!ReadUint16(version) || version != ARCHIVE_VERSION) {
        LOG_ERROR << "Unsupported archive version: " << version;
        Close();
        return false;
    }
    if (!ReadUint16(flags)) { Close(); return false; }

    // Read manifest
    if (!ReadUint64(manifest_size_)) {
        LOG_ERROR << "Failed to read manifest size";
        Close();
        return false;
    }

    manifest_offset_ = Tell();
    data_start_offset_ = manifest_offset_ + manifest_size_;

    open_ = true;
    LOG_INFO << "Opened archive: " << file_path_
             << " (manifest: " << manifest_size_ << " bytes)";
    return true;
}

std::optional<std::string> BackupArchiveReader::ReadManifest() {
    if (!open_) return std::nullopt;

    if (!Seek(manifest_offset_)) return std::nullopt;

    std::string manifest(manifest_size_, '\0');
    if (!ReadBytes(&manifest[0], manifest_size_)) {
        LOG_ERROR << "Failed to read manifest data";
        return std::nullopt;
    }

    return manifest;
}

std::optional<std::vector<uint8_t>> BackupArchiveReader::ReadFileData(
    const BackupFileEntry& entry) {
    if (!open_) return std::nullopt;

    // ⚠️ offset_in_archive is unreliable (never set during write).
    // Scan sequentially from data_start_offset_ to find the matching path.
    if (!Seek(data_start_offset_)) return std::nullopt;

    // Get file size to compute footer offset (footer = file_count(8) + checksum(32))
    file_.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(file_.tellg());
    uint64_t footer_start = (file_size > 40) ? (file_size - 40) : 0;
    file_.seekg(data_start_offset_, std::ios::beg);

    while (Tell() < footer_start) {
        // Read path
        uint16_t path_len;
        if (!ReadUint16(path_len)) return std::nullopt;
        std::string path(path_len, '\0');
        if (!ReadBytes(&path[0], path_len)) return std::nullopt;

        // Read metadata
        uint8_t  ftype;
        uint32_t mode;
        uint64_t original_size, stored_size;
        if (!ReadBytes(&ftype, 1)) return std::nullopt;
        if (!ReadUint32(mode)) return std::nullopt;
        if (!ReadUint64(original_size)) return std::nullopt;
        if (!ReadUint64(stored_size)) return std::nullopt;

        if (path == entry.metadata.path) {
            // Found the matching file — read its data
            std::vector<uint8_t> data(stored_size);
            if (stored_size > 0) {
                if (!ReadBytes(data.data(), stored_size)) {
                    LOG_ERROR << "Failed to read file data for: " << entry.metadata.path;
                    return std::nullopt;
                }
            }
            return data;
        } else {
            // Skip this file's data and continue scanning
            if (stored_size > 0) {
                if (!Seek(Tell() + stored_size)) return std::nullopt;
            }
        }
    }

    LOG_ERROR << "File not found in archive data section: " << entry.metadata.path;
    return std::nullopt;
}

bool BackupArchiveReader::ExtractFile(const BackupFileEntry& entry,
                                       const std::string& dest_base_dir) {
    if (!open_) return false;

    std::string dest_path = FileUtils::JoinPath(dest_base_dir, entry.metadata.path);

    // Handle different file types
    switch (entry.metadata.type) {
        case FileType::kDirectory: {
            if (!FileUtils::CreateDirectoryRecursive(dest_path)) {
                LOG_ERROR << "Failed to create directory: " << dest_path;
                return false;
            }
            return true;
        }

        case FileType::kSymlink: {
            // Recreate symlink
            if (symlink(entry.metadata.symlink_target.c_str(), dest_path.c_str()) != 0) {
                LOG_ERROR << "Failed to create symlink: " << dest_path
                          << " -> " << entry.metadata.symlink_target;
                return false;
            }
            return true;
        }

        case FileType::kFifo: {
            // Remove existing file/dir/symlink if present (overwrite mode)
            unlink(dest_path.c_str());
            if (mkfifo(dest_path.c_str(), entry.metadata.mode) != 0) {
                LOG_ERROR << "Failed to create FIFO: " << dest_path
                          << ": " << strerror(errno);
                return false;
            }
            return true;
        }

        case FileType::kHardLink: {
            // Recreate hardlink pointing to the original file
            // The symlink_target field stores the relative path of the first inode occurrence
            std::string target_path = FileUtils::JoinPath(dest_base_dir,
                                                          entry.metadata.symlink_target);
            // Ensure parent directory exists
            std::string parent = FileUtils::GetParentPath(dest_path);
            if (!parent.empty()) {
                FileUtils::CreateDirectoryRecursive(parent);
            }
            // Remove existing destination if present (link() requires it not exist)
            unlink(dest_path.c_str());
            if (link(target_path.c_str(), dest_path.c_str()) != 0) {
                LOG_ERROR << "Failed to create hardlink: " << dest_path
                          << " -> " << target_path << ": " << strerror(errno);
                return false;
            }
            LOG_INFO << "Restored hardlink: " << entry.metadata.path;
            return true;
        }

        case FileType::kRegular: {
            // Ensure parent directory exists
            std::string parent = FileUtils::GetParentPath(dest_path);
            if (!parent.empty()) {
                FileUtils::CreateDirectoryRecursive(parent);
            }

            // Read data from archive and write to disk
            auto data = ReadFileData(entry);
            if (!data.has_value()) {
                LOG_ERROR << "Failed to read archive data for: " << entry.metadata.path;
                return false;
            }

            // Open destination file
            int dest_fd = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                               entry.metadata.mode);
            if (dest_fd < 0) {
                LOG_ERROR << "Failed to create file: " << dest_path;
                return false;
            }

            ssize_t written = write(dest_fd, data->data(), data->size());
            close(dest_fd);

            if (written != static_cast<ssize_t>(data->size())) {
                LOG_ERROR << "Failed to write file: " << dest_path;
                return false;
            }
            return true;
        }

        case FileType::kBlockDevice:
        case FileType::kCharDevice: {
            // Recreate device node with mknod
            std::string parent = FileUtils::GetParentPath(dest_path);
            if (!parent.empty()) {
                FileUtils::CreateDirectoryRecursive(parent);
            }
            dev_t dev = makedev(entry.metadata.dev_major, entry.metadata.dev_minor);
            mode_t node_mode = entry.metadata.mode;
            if (entry.metadata.type == FileType::kBlockDevice)
                node_mode |= S_IFBLK;
            else
                node_mode |= S_IFCHR;

            if (mknod(dest_path.c_str(), node_mode, dev) != 0) {
                LOG_ERROR << "Failed to create device node: " << dest_path
                          << " (" << entry.metadata.dev_major << ","
                          << entry.metadata.dev_minor << "): " << strerror(errno);
                return false;
            }
            LOG_INFO << "Restored device node: " << entry.metadata.path;
            return true;
        }

        case FileType::kSocket:
            // Sockets are transient runtime objects; cannot be meaningfully restored.
            // Log and skip (not an error).
            LOG_INFO << "Skipping socket (transient): " << entry.metadata.path;
            return true;

        default:
            LOG_WARNING << "Unknown file type, skipping: " << entry.metadata.path;
            return true;
    }
}

bool BackupArchiveReader::VerifyIntegrity() {
    if (!open_) return false;

    // Read the entire file up to the checksum
    uint64_t current = Tell();

    if (!Seek(0)) return false;

    SHA256_CTX sha_ctx;
    SHA256_Init(&sha_ctx);

    // Get file size
    file_.seekg(0, std::ios::end);
    uint64_t file_size = file_.tellg();
    file_.seekg(0, std::ios::beg);

    // Read everything except the last 32 bytes (the checksum)
    uint64_t to_hash = file_size - 32;
    std::vector<uint8_t> buffer(1024 * 1024);
    while (to_hash > 0) {
        size_t chunk = std::min(static_cast<size_t>(to_hash), buffer.size());
        if (!ReadBytes(buffer.data(), chunk)) return false;
        SHA256_Update(&sha_ctx, buffer.data(), chunk);
        to_hash -= chunk;
    }

    // Read stored checksum
    std::vector<uint8_t> stored_checksum(32);
    if (!ReadBytes(stored_checksum.data(), 32)) return false;

    // Compute final checksum
    std::vector<uint8_t> computed_checksum(SHA256_DIGEST_LENGTH);
    SHA256_Final(computed_checksum.data(), &sha_ctx);

    bool match = (stored_checksum == computed_checksum);

    // Restore position
    Seek(current);

    if (!match) {
        LOG_ERROR << "Archive integrity check FAILED: " << file_path_;
    } else {
        LOG_INFO << "Archive integrity check passed: " << file_path_;
    }
    return match;
}

uint64_t BackupArchiveReader::GetFileCount() {
    if (!open_) return 0;

    // Seek to end minus checksum(32) minus file_count(8)
    file_.seekg(-40, std::ios::end);
    uint64_t count;
    if (!ReadUint64(count)) return 0;
    return count;
}

void BackupArchiveReader::Close() {
    if (file_.is_open()) {
        file_.close();
    }
    open_ = false;
}

bool BackupArchiveReader::IsOpen() const { return open_; }

bool BackupArchiveReader::ReadBytes(void* data, size_t size) {
    if (!file_.is_open()) return false;
    file_.read(static_cast<char*>(data), size);
    return file_.gcount() == static_cast<std::streamsize>(size);
}

bool BackupArchiveReader::ReadUint16(uint16_t& value) {
    uint8_t buf[2];
    if (!ReadBytes(buf, 2)) return false;
    value = static_cast<uint16_t>(buf[0]) |
            (static_cast<uint16_t>(buf[1]) << 8);
    return true;
}

bool BackupArchiveReader::ReadUint32(uint32_t& value) {
    uint8_t buf[4];
    if (!ReadBytes(buf, 4)) return false;
    value = static_cast<uint32_t>(buf[0]) |
            (static_cast<uint32_t>(buf[1]) << 8) |
            (static_cast<uint32_t>(buf[2]) << 16) |
            (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

bool BackupArchiveReader::ReadUint64(uint64_t& value) {
    uint8_t buf[8];
    if (!ReadBytes(buf, 8)) return false;
    value = static_cast<uint64_t>(buf[0]) |
            (static_cast<uint64_t>(buf[1]) << 8) |
            (static_cast<uint64_t>(buf[2]) << 16) |
            (static_cast<uint64_t>(buf[3]) << 24) |
            (static_cast<uint64_t>(buf[4]) << 32) |
            (static_cast<uint64_t>(buf[5]) << 40) |
            (static_cast<uint64_t>(buf[6]) << 48) |
            (static_cast<uint64_t>(buf[7]) << 56);
    return true;
}

bool BackupArchiveReader::Seek(uint64_t offset) {
    if (!file_.is_open()) return false;
    file_.seekg(offset, std::ios::beg);
    return file_.good();
}

uint64_t BackupArchiveReader::Tell() {
    if (!file_.is_open()) return 0;
    return static_cast<uint64_t>(file_.tellg());
}

} // namespace backup
