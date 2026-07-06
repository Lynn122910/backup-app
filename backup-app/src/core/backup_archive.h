#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <fstream>
#include <functional>

namespace backup {

class BackupManifest;

/// ============================================================
/// Archive binary format (v1.0):
///
/// [Header]
///   magic:    4 bytes  "BKP0"
///   version:  2 bytes  uint16 (1)
///   flags:    2 bytes  uint16 (reserved)
///
/// [Manifest Section]
///   manifest_size: 8 bytes uint64
///   manifest_data: N bytes (JSON string)
///
/// [File Sections] (repeated)
///   path_len:    2 bytes uint16
///   path:        N bytes (UTF-8)
///   file_type:   1 byte
///   file_mode:   4 bytes uint32
///   file_size:   8 bytes uint64 (original size)
///   data_size:   8 bytes uint64 (stored size in archive)
///   file_data:   N bytes
///
/// [Footer]
///   file_count:  8 bytes uint64
///   checksum:   32 bytes (SHA-256 of everything before this)
/// ============================================================

/// Progress callback for archive operations
using ArchiveProgressCallback = std::function<void(uint64_t processed, uint64_t total)>;

/// Archive writer - creates backup archives
class BackupArchiveWriter {
public:
    BackupArchiveWriter();
    ~BackupArchiveWriter();

    /// Open archive file for writing
    bool Open(const std::string& file_path);

    /// Write the manifest section
    bool WriteManifest(const std::string& manifest_json);

    /// Write a single file's data to the archive
    /// @param entry  File entry with metadata
    /// @param data   File content bytes
    /// @return true on success
    bool WriteFileData(const BackupFileEntry& entry,
                       const std::vector<uint8_t>& data);

    /// Write a file from disk directly (streaming)
    bool WriteFileFromDisk(const BackupFileEntry& entry,
                           const std::string& source_base_dir);

    /// Finalize the archive (write footer with checksum)
    bool Finalize();

    /// Close without finalizing (cleanup on error)
    void Close();

    /// Check if archive is open
    bool IsOpen() const;

    /// Get the file path
    std::string FilePath() const;

private:
    /// Write binary data to the file
    bool WriteBytes(const void* data, size_t size);

    /// Write uint16 little-endian
    bool WriteUint16(uint16_t value);

    /// Write uint32 little-endian
    bool WriteUint32(uint32_t value);

    /// Write uint64 little-endian
    bool WriteUint64(uint64_t value);

    /// Update running SHA-256
    void UpdateChecksum(const void* data, size_t size);

    std::ofstream file_;
    std::string   file_path_;
    uint64_t      file_count_ = 0;
    bool          open_ = false;
    bool          finalized_ = false;

    // SHA-256 context (opaque pointer to avoid header pollution)
    void* sha256_ctx_ = nullptr;
    std::vector<uint8_t> final_checksum_;
};

/// Archive reader - reads backup archives
class BackupArchiveReader {
public:
    BackupArchiveReader();
    ~BackupArchiveReader();

    /// Open archive file for reading
    bool Open(const std::string& file_path);

    /// Read the manifest section
    std::optional<std::string> ReadManifest();

    /// Read file data for a specific entry
    std::optional<std::vector<uint8_t>> ReadFileData(const BackupFileEntry& entry);

    /// Extract file directly to disk
    bool ExtractFile(const BackupFileEntry& entry,
                     const std::string& dest_base_dir);

    /// Verify archive integrity
    bool VerifyIntegrity();

    /// Get file entries count (quick read of footer)
    uint64_t GetFileCount();

    /// Close the archive
    void Close();

    /// Check if archive is open
    bool IsOpen() const;

private:
    /// Read bytes from file
    bool ReadBytes(void* data, size_t size);

    /// Read uint16 little-endian
    bool ReadUint16(uint16_t& value);

    /// Read uint32 little-endian
    bool ReadUint32(uint32_t& value);

    /// Read uint64 little-endian
    bool ReadUint64(uint64_t& value);

    /// Seek to position
    bool Seek(uint64_t offset);

    /// Get current position
    uint64_t Tell();

    std::ifstream file_;
    std::string   file_path_;
    uint64_t      manifest_offset_ = 0;
    uint64_t      manifest_size_   = 0;
    uint64_t      data_start_offset_ = 0;
    bool          open_ = false;
};

} // namespace backup
