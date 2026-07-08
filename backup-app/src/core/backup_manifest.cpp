#include "core/backup_manifest.h"
#include "common/logger.h"
#include "common/file_utils.h"

#include <sstream>
#include <algorithm>
#include <fstream>

namespace backup {

// ============================================================
// Internal struct to hold manifest data
// ============================================================
struct BackupManifest::Impl {
    std::string manifest_version = "1.0";
    std::string backup_id;
    BackupType  backup_type = BackupType::kFull;
    std::string parent_manifest_id;
    std::string source_directory;
    std::string backup_name;
    int64_t     created_at = 0;
    CompressionAlgo compression_algo = CompressionAlgo::kNone;
    int             compression_level = 3;
    EncryptionAlgo  encryption_algo = EncryptionAlgo::kNone;
    std::string archive_filename;

    std::vector<BackupFileEntry> files;
};

// ============================================================
// Constructor / Destructor
// ============================================================
BackupManifest::BackupManifest() : impl_(std::make_unique<Impl>()) {}
BackupManifest::~BackupManifest() = default;

BackupManifest::BackupManifest(BackupManifest&&) noexcept = default;
BackupManifest& BackupManifest::operator=(BackupManifest&&) noexcept = default;

// ============================================================
// Property accessors
// ============================================================
std::string BackupManifest::manifest_version() const { return impl_->manifest_version; }
void BackupManifest::set_manifest_version(const std::string& v) { impl_->manifest_version = v; }

std::string BackupManifest::backup_id() const { return impl_->backup_id; }
void BackupManifest::set_backup_id(const std::string& id) { impl_->backup_id = id; }

BackupType BackupManifest::backup_type() const { return impl_->backup_type; }
void BackupManifest::set_backup_type(BackupType type) { impl_->backup_type = type; }

std::string BackupManifest::parent_manifest_id() const { return impl_->parent_manifest_id; }
void BackupManifest::set_parent_manifest_id(const std::string& id) { impl_->parent_manifest_id = id; }

std::string BackupManifest::source_directory() const { return impl_->source_directory; }
void BackupManifest::set_source_directory(const std::string& path) { impl_->source_directory = path; }

std::string BackupManifest::backup_name() const { return impl_->backup_name; }
void BackupManifest::set_backup_name(const std::string& name) { impl_->backup_name = name; }

int64_t BackupManifest::created_at() const { return impl_->created_at; }
void BackupManifest::set_created_at(int64_t timestamp) { impl_->created_at = timestamp; }

CompressionAlgo BackupManifest::compression_algo() const { return impl_->compression_algo; }
void BackupManifest::set_compression_algo(CompressionAlgo algo) { impl_->compression_algo = algo; }

int BackupManifest::compression_level() const { return impl_->compression_level; }
void BackupManifest::set_compression_level(int level) { impl_->compression_level = level; }

EncryptionAlgo BackupManifest::encryption_algo() const { return impl_->encryption_algo; }
void BackupManifest::set_encryption_algo(EncryptionAlgo algo) { impl_->encryption_algo = algo; }

std::string BackupManifest::archive_filename() const { return impl_->archive_filename; }
void BackupManifest::set_archive_filename(const std::string& name) { impl_->archive_filename = name; }

// ============================================================
// File entry management
// ============================================================
void BackupManifest::AddFileEntry(const BackupFileEntry& entry) {
    impl_->files.push_back(entry);
}

void BackupManifest::AddFileEntries(const std::vector<BackupFileEntry>& entries) {
    impl_->files.insert(impl_->files.end(), entries.begin(), entries.end());
}

void BackupManifest::RemoveFileEntry(const std::string& path) {
    impl_->files.erase(
        std::remove_if(impl_->files.begin(), impl_->files.end(),
                       [&path](const BackupFileEntry& e) {
                           return e.metadata.path == path;
                       }),
        impl_->files.end());
}

std::vector<BackupFileEntry> BackupManifest::files() const {
    return impl_->files;
}

size_t BackupManifest::file_count() const {
    return impl_->files.size();
}

std::optional<BackupFileEntry> BackupManifest::FindByPath(const std::string& path) const {
    for (const auto& entry : impl_->files) {
        if (entry.metadata.path == path) {
            return std::make_optional(entry);
        }
    }
    return std::nullopt;
}

std::vector<BackupFileEntry> BackupManifest::FilesByStatus(const std::string& status) const {
    std::vector<BackupFileEntry> result;
    for (const auto& entry : impl_->files) {
        if (entry.status == status) {
            result.push_back(entry);
        }
    }
    return result;
}

uint64_t BackupManifest::total_files() const {
    return impl_->files.size();
}

uint64_t BackupManifest::total_size() const {
    uint64_t total = 0;
    for (const auto& entry : impl_->files) {
        total += entry.metadata.size;
    }
    return total;
}

// ============================================================
// JSON Serialization (custom simple writer, no external deps)
// ============================================================
static void WriteJsonString(std::ostringstream& oss, const std::string& s) {
    oss << "\"" << json::EscapeString(s) << "\"";
}

static void WriteJsonKeyValue(std::ostringstream& oss, const std::string& key,
                               const std::string& value, bool quote = true) {
    WriteJsonString(oss, key);
    oss << ": ";
    if (quote) {
        WriteJsonString(oss, value);
    } else {
        oss << value;
    }
}

std::string BackupManifest::ToJson() const {
    std::ostringstream oss;
    oss << "{\n";

    // Header fields
    WriteJsonKeyValue(oss, "manifest_version", impl_->manifest_version);
    oss << ",\n";
    WriteJsonKeyValue(oss, "backup_id", impl_->backup_id);
    oss << ",\n";
    WriteJsonKeyValue(oss, "backup_type", BackupTypeToString(impl_->backup_type));
    oss << ",\n";
    WriteJsonKeyValue(oss, "parent_manifest_id", impl_->parent_manifest_id);
    oss << ",\n";
    WriteJsonKeyValue(oss, "source_directory", impl_->source_directory);
    oss << ",\n";
    WriteJsonKeyValue(oss, "backup_name", impl_->backup_name);
    oss << ",\n";
    WriteJsonKeyValue(oss, "archive_filename", impl_->archive_filename);
    oss << ",\n";
    oss << "\"created_at\": " << impl_->created_at << ",\n";
    oss << "\"created_at_human\": ";
    WriteJsonString(oss, FileUtils::FormatTimestamp(impl_->created_at));
    oss << ",\n";
    oss << "\"compression\": \"" << static_cast<int>(impl_->compression_algo) << "\",\n";
    oss << "\"compression_level\": " << impl_->compression_level << ",\n";
    oss << "\"encryption\": \"" << static_cast<int>(impl_->encryption_algo) << "\",\n";

    // File entries
    oss << "\"total_files\": " << impl_->files.size() << ",\n";
    uint64_t total_size = 0;
    for (const auto& f : impl_->files) total_size += f.metadata.size;
    oss << "\"total_size\": " << total_size << ",\n";

    oss << "\"files\": [\n";
    for (size_t i = 0; i < impl_->files.size(); ++i) {
        const auto& f = impl_->files[i];
        oss << "  {\n";
        WriteJsonKeyValue(oss, "path", f.metadata.path);
        oss << ",\n";
        WriteJsonKeyValue(oss, "type", f.metadata.TypeString());
        oss << ",\n";
        oss << "    \"size\": " << f.metadata.size << ",\n";
        oss << "    \"mode\": " << f.metadata.mode << ",\n";
        oss << "    \"uid\": " << f.metadata.uid << ",\n";
        WriteJsonKeyValue(oss, "owner", f.metadata.owner);
        oss << ",\n";
        oss << "    \"gid\": " << f.metadata.gid << ",\n";
        WriteJsonKeyValue(oss, "group", f.metadata.group);
        oss << ",\n";
        oss << "    \"mtime\": " << f.metadata.mtime_nsec << ",\n";
        oss << "    \"atime\": " << f.metadata.atime_nsec << ",\n";
        oss << "    \"ctime\": " << f.metadata.ctime_nsec << ",\n";
        oss << "    \"inode\": " << f.metadata.inode << ",\n";
        oss << "    \"dev_major\": " << f.metadata.dev_major << ",\n";
        oss << "    \"dev_minor\": " << f.metadata.dev_minor << ",\n";
        if (!f.metadata.symlink_target.empty()) {
            WriteJsonKeyValue(oss, "symlink_target", f.metadata.symlink_target);
            oss << ",\n";
        }
        if (!f.metadata.acl_text.empty()) {
            WriteJsonKeyValue(oss, "acl_text", f.metadata.acl_text);
            oss << ",\n";
        }
        if (!f.metadata.capabilities_text.empty()) {
            WriteJsonKeyValue(oss, "capabilities_text", f.metadata.capabilities_text);
            oss << ",\n";
        }
        if (!f.metadata.selinux_context.empty()) {
            WriteJsonKeyValue(oss, "selinux_context", f.metadata.selinux_context);
            oss << ",\n";
        }
        WriteJsonKeyValue(oss, "status", f.status);
        oss << ",\n";
        oss << "    \"offset_in_archive\": " << f.offset_in_archive << ",\n";
        oss << "    \"stored_size\": " << f.stored_size << "\n";
        oss << "  }";
        if (i < impl_->files.size() - 1) oss << ",";
        oss << "\n";
    }
    oss << "]\n";
    oss << "}\n";
    return oss.str();
}

std::optional<BackupManifest> BackupManifest::FromJson(const std::string& json) {
    BackupManifest manifest;

    // Extract simple fields
    auto version = json::ExtractStringValue(json, "manifest_version");
    if (!version.has_value()) {
        LOG_ERROR << "Manifest missing 'manifest_version' field";
        return std::nullopt;
    }
    manifest.set_manifest_version(version.value());

    auto backup_id = json::ExtractStringValue(json, "backup_id");
    manifest.set_backup_id(backup_id.value_or(""));

    auto backup_type_str = json::ExtractStringValue(json, "backup_type");
    if (backup_type_str.has_value()) {
        if (backup_type_str.value() == "full") manifest.set_backup_type(BackupType::kFull);
        else if (backup_type_str.value() == "incremental") manifest.set_backup_type(BackupType::kIncremental);
        else if (backup_type_str.value() == "differential") manifest.set_backup_type(BackupType::kDifferential);
    }

    auto parent_id = json::ExtractStringValue(json, "parent_manifest_id");
    manifest.set_parent_manifest_id(parent_id.value_or(""));

    auto source_dir = json::ExtractStringValue(json, "source_directory");
    manifest.set_source_directory(source_dir.value_or(""));

    auto backup_name = json::ExtractStringValue(json, "backup_name");
    manifest.set_backup_name(backup_name.value_or(""));

    auto archive_name = json::ExtractStringValue(json, "archive_filename");
    manifest.set_archive_filename(archive_name.value_or(""));

    auto created = json::ExtractIntValue(json, "created_at");
    manifest.set_created_at(created.value_or(0));

    // Parse file entries
    auto file_objects = json::ExtractObjectArray(json, "files");
    for (const auto& file_obj : file_objects) {
        BackupFileEntry entry;
        auto path = json::ExtractStringValue(file_obj, "path");
        if (!path.has_value()) continue;
        entry.metadata.path = path.value();

        auto type_str = json::ExtractStringValue(file_obj, "type");
        entry.metadata.type = StringToFileType(type_str.value_or("regular"));

        auto size = json::ExtractUintValue(file_obj, "size");
        entry.metadata.size = size.value_or(0);

        auto mode = json::ExtractUintValue(file_obj, "mode");
        entry.metadata.mode = static_cast<uint32_t>(mode.value_or(0644));

        auto uid = json::ExtractUintValue(file_obj, "uid");
        entry.metadata.uid = static_cast<uint32_t>(uid.value_or(0));

        auto owner = json::ExtractStringValue(file_obj, "owner");
        entry.metadata.owner = owner.value_or("");

        auto gid = json::ExtractUintValue(file_obj, "gid");
        entry.metadata.gid = static_cast<uint32_t>(gid.value_or(0));

        auto group = json::ExtractStringValue(file_obj, "group");
        entry.metadata.group = group.value_or("");

        auto mtime = json::ExtractIntValue(file_obj, "mtime");
        entry.metadata.mtime_nsec = mtime.value_or(0);

        auto atime = json::ExtractIntValue(file_obj, "atime");
        entry.metadata.atime_nsec = atime.value_or(0);

        auto ctime = json::ExtractIntValue(file_obj, "ctime");
        entry.metadata.ctime_nsec = ctime.value_or(0);

        auto inode = json::ExtractUintValue(file_obj, "inode");
        entry.metadata.inode = inode.value_or(0);

        auto dev_major = json::ExtractUintValue(file_obj, "dev_major");
        entry.metadata.dev_major = static_cast<uint32_t>(dev_major.value_or(0));

        auto dev_minor = json::ExtractUintValue(file_obj, "dev_minor");
        entry.metadata.dev_minor = static_cast<uint32_t>(dev_minor.value_or(0));

        auto symlink = json::ExtractStringValue(file_obj, "symlink_target");
        entry.metadata.symlink_target = symlink.value_or("");

        auto acl = json::ExtractStringValue(file_obj, "acl_text");
        entry.metadata.acl_text = acl.value_or("");

        auto caps = json::ExtractStringValue(file_obj, "capabilities_text");
        entry.metadata.capabilities_text = caps.value_or("");

        auto selinux = json::ExtractStringValue(file_obj, "selinux_context");
        entry.metadata.selinux_context = selinux.value_or("");

        auto status = json::ExtractStringValue(file_obj, "status");
        entry.status = status.value_or("added");

        auto offset = json::ExtractUintValue(file_obj, "offset_in_archive");
        entry.offset_in_archive = offset.value_or(0);

        auto stored = json::ExtractUintValue(file_obj, "stored_size");
        entry.stored_size = stored.value_or(0);

        manifest.AddFileEntry(entry);
    }

    return std::optional<BackupManifest>(std::move(manifest));
}

bool BackupManifest::WriteToFile(const std::string& file_path) const {
    std::string json = ToJson();
    return FileUtils::WriteStringToFile(file_path, json);
}

std::optional<BackupManifest> BackupManifest::ReadFromFile(const std::string& file_path) {
    std::string content = FileUtils::ReadFileToString(file_path);
    if (content.empty()) {
        LOG_ERROR << "Failed to read manifest file: " << file_path;
        return std::nullopt;
    }
    return FromJson(content);
}

} // namespace backup
