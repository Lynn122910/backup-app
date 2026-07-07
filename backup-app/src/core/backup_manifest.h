#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace backup {

/// Backup manifest: describes what was backed up, when, and how
///
/// The manifest is the index/metadata file for a backup operation.
/// It records all files, their metadata, and their status.
/// For incremental backups, it chains to a parent manifest.
class BackupManifest {
public:
    BackupManifest();
    ~BackupManifest();

    // Make movable (unique_ptr<Impl> prevents copy, destructor suppresses implicit move)
    // Defined in .cpp where Impl is complete — defaulting here would require Impl to be complete
    BackupManifest(BackupManifest&&) noexcept;
    BackupManifest& operator=(BackupManifest&&) noexcept;

    // Non-copyable
    BackupManifest(const BackupManifest&) = delete;
    BackupManifest& operator=(const BackupManifest&) = delete;

    // ========== Basic properties ==========

    std::string manifest_version() const;
    void        set_manifest_version(const std::string& v);

    std::string backup_id() const;
    void        set_backup_id(const std::string& id);

    BackupType  backup_type() const;
    void        set_backup_type(BackupType type);

    std::string parent_manifest_id() const;
    void        set_parent_manifest_id(const std::string& id);

    std::string source_directory() const;
    void        set_source_directory(const std::string& path);

    std::string backup_name() const;
    void        set_backup_name(const std::string& name);

    int64_t     created_at() const;
    void        set_created_at(int64_t timestamp);

    CompressionAlgo compression_algo() const;
    void            set_compression_algo(CompressionAlgo algo);
    int             compression_level() const;
    void            set_compression_level(int level);

    EncryptionAlgo  encryption_algo() const;
    void            set_encryption_algo(EncryptionAlgo algo);

    std::string archive_filename() const;
    void        set_archive_filename(const std::string& name);

    // ========== File entries ==========

    void AddFileEntry(const BackupFileEntry& entry);
    void AddFileEntries(const std::vector<BackupFileEntry>& entries);
    void RemoveFileEntry(const std::string& path);

    std::vector<BackupFileEntry> files() const;
    size_t file_count() const;

    /// Find a file entry by its relative path
    std::optional<BackupFileEntry> FindByPath(const std::string& path) const;

    /// Get all files with a specific status
    std::vector<BackupFileEntry> FilesByStatus(const std::string& status) const;

    uint64_t total_files() const;
    uint64_t total_size() const;

    // ========== Serialization ==========

    /// Serialize to JSON string (using simple custom JSON writer)
    std::string ToJson() const;

    /// Deserialize from JSON string
    static std::optional<BackupManifest> FromJson(const std::string& json);

    /// Write manifest to a file
    bool WriteToFile(const std::string& file_path) const;

    /// Read manifest from a file
    static std::optional<BackupManifest> ReadFromFile(const std::string& file_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backup
