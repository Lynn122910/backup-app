#include "core/backup_engine.h"
#include "core/backup_manifest.h"
#include "core/backup_archive.h"
#include "services/file_scanner.h"
#include "common/logger.h"
#include "common/file_utils.h"
#include "common/filter_engine.h"

#include <QThread>
#include <chrono>
#include <algorithm>

namespace backup {

struct BackupEngine::Impl {
    BackupOptions                 options;
    BackupProgress                progress;
    BackupProgressCallback        progress_cb;
    std::atomic<bool>             cancelled{false};
    std::shared_ptr<BackupManifest> last_manifest;
    std::string                   archive_path;
};

BackupEngine::BackupEngine(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    qRegisterMetaType<backup::BackupProgress>("backup::BackupProgress");
}

BackupEngine::~BackupEngine() = default;

void BackupEngine::SetProgressCallback(BackupProgressCallback cb) {
    impl_->progress_cb = std::move(cb);
}

bool BackupEngine::IsRunning() const {
    return impl_->progress.is_running;
}

void BackupEngine::Cancel() {
    impl_->cancelled.store(true, std::memory_order_release);
    LOG_INFO << "Backup cancellation requested";
}

std::shared_ptr<BackupManifest> BackupEngine::GetLastManifest() const {
    return impl_->last_manifest;
}

void BackupEngine::UpdateProgress(const BackupProgress& progress) {
    impl_->progress = progress;
    if (impl_->progress_cb) {
        impl_->progress_cb(progress);
    }
    emit ProgressUpdated(progress);
}

bool BackupEngine::Execute(const BackupOptions& options) {
    impl_->options  = options;
    impl_->cancelled.store(false, std::memory_order_release);
    impl_->last_manifest.reset();

    BackupProgress progress;
    progress.is_running = true;
    UpdateProgress(progress);

    auto start_time = std::chrono::steady_clock::now();

    emit LogMessage("INFO", QString("Starting backup: %1 → %2")
                    .arg(QString::fromStdString(options.source_dir))
                    .arg(QString::fromStdString(options.dest_path)));

    // ============================================================
    // Step 1: Validate parameters
    // ============================================================
    if (options.source_dir.empty() || options.dest_path.empty()) {
        progress.is_running   = false;
        progress.is_completed = true;
        progress.error_message = "Source or destination path is empty";
        UpdateProgress(progress);
        emit BackupCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    if (!FileUtils::IsDirectory(options.source_dir)) {
        progress.is_running   = false;
        progress.is_completed = true;
        progress.error_message = "Source is not a valid directory: " + options.source_dir;
        UpdateProgress(progress);
        emit BackupCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    // Generate archive path if not explicitly set
    std::string archive_path = options.dest_path;
    if (FileUtils::IsDirectory(archive_path) || archive_path.empty()) {
        std::string archive_name = FileUtils::GenerateTimestampName() + ".bkp";
        archive_path = FileUtils::JoinPath(options.dest_path, archive_name);
    }
    impl_->archive_path = archive_path;

    // ============================================================
    // Step 2: Scan source directory
    // ============================================================
    emit LogMessage("INFO", "Scanning source directory...");

    auto manifest = std::make_shared<BackupManifest>();
    auto entries = PrepareFileList(options);

    if (impl_->cancelled.load(std::memory_order_acquire)) {
        progress.is_running   = false;
        progress.is_completed = true;
        progress.error_message = "Backup cancelled by user";
        UpdateProgress(progress);
        emit BackupCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    if (entries.empty()) {
        LOG_WARNING << "No files found to backup in: " << options.source_dir;
    }

    // ============================================================
    // Step 3: Create manifest
    // ============================================================
    manifest->set_manifest_version("1.0");
    manifest->set_backup_id(FileUtils::GenerateTimestampName());
    manifest->set_backup_type(options.type);
    manifest->set_source_directory(options.source_dir);
    manifest->set_backup_name(options.backup_name.empty()
                              ? "Backup " + FileUtils::FormatTimestamp(FileUtils::GetCurrentTimeNsec())
                              : options.backup_name);
    manifest->set_created_at(FileUtils::GetCurrentTimeNsec());
    manifest->set_archive_filename(FileUtils::GetFileName(archive_path));
    manifest->AddFileEntries(entries);

    // ============================================================
    // Step 4: Write archive
    // ============================================================
    emit LogMessage("INFO", "Writing archive...");

    bool write_ok = WriteArchive(entries, options, manifest);

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time).count();

    progress.is_running   = false;
    progress.is_completed = true;
    progress.elapsed_seconds = static_cast<int>(elapsed);

    if (!write_ok && !impl_->cancelled.load(std::memory_order_acquire)) {
        progress.error_message = "Failed to write backup archive";
        UpdateProgress(progress);
        emit BackupCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    if (impl_->cancelled.load(std::memory_order_acquire)) {
        progress.error_message = "Backup cancelled by user";
        UpdateProgress(progress);
        emit BackupCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    // ============================================================
    // Step 5: Save manifest alongside archive
    // ============================================================
    std::string manifest_path = archive_path + ".manifest.json";
    if (!manifest->WriteToFile(manifest_path)) {
        LOG_ERROR << "Failed to write manifest file: " << manifest_path;
    }

    impl_->last_manifest = manifest;

    LOG_INFO << "Backup completed successfully: " << entries.size()
             << " files, " << manifest->total_size() << " bytes, "
             << elapsed << "s";

    UpdateProgress(progress);

    QString msg = QString("Backup completed: %1 files, %2 bytes, %3 seconds")
        .arg(entries.size())
        .arg(manifest->total_size())
        .arg(elapsed);

    emit BackupCompleted(true, msg);
    return true;
}

std::vector<BackupFileEntry> BackupEngine::PrepareFileList(const BackupOptions& options) {
    FileScanner scanner;
    auto files = scanner.Scan(options.source_dir);

    // Apply filter rules (post-scan)
    if (!options.filters.empty()) {
        size_t before = files.size();
        files = FilterEngine::Apply(files, options.filters);
        size_t after = files.size();
        emit LogMessage("INFO",
                        QString("Filters applied: %1 of %2 files kept (%3 rules)")
                        .arg(after).arg(before).arg(options.filters.size()));
    }

    std::vector<BackupFileEntry> entries;
    entries.reserve(files.size());

    for (auto& meta : files) {
        BackupFileEntry entry;
        entry.metadata = std::move(meta);
        entry.status   = "added";
        entries.push_back(std::move(entry));
    }

    return entries;
}

bool BackupEngine::WriteArchive(
    const std::vector<BackupFileEntry>& entries,
    const BackupOptions& options,
    std::shared_ptr<BackupManifest> manifest) {

    BackupArchiveWriter writer;
    if (!writer.Open(impl_->archive_path)) {
        return false;
    }

    // Write manifest first (place-holder manifest, metadata only)
    std::string manifest_json = manifest->ToJson();
    if (!writer.WriteManifest(manifest_json)) {
        writer.Close();
        return false;
    }

    // Write each file
    BackupProgress progress;
    progress.total_files = entries.size();
    progress.is_running  = true;

    auto archive_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < entries.size(); ++i) {
        if (impl_->cancelled.load(std::memory_order_acquire)) {
            writer.Close();
            return false;
        }

        const auto& entry = entries[i];

        progress.processed_files = i + 1;
        progress.total_bytes     = manifest->total_size();
        progress.processed_bytes += entry.metadata.size;
        progress.current_file    = entry.metadata.path;

        // Calculate speed
        auto now = std::chrono::steady_clock::now();
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - archive_start).count();
        if (elapsed_sec > 0) {
            progress.bytes_per_second = progress.processed_bytes / elapsed_sec;
            if (progress.bytes_per_second > 0) {
                uint64_t remaining = progress.total_bytes - progress.processed_bytes;
                progress.estimated_remaining_seconds =
                    static_cast<int>(remaining / progress.bytes_per_second);
            }
        }
        progress.elapsed_seconds = static_cast<int>(elapsed_sec);

        UpdateProgress(progress);
        emit LogMessage("INFO",
                        QString("[%1/%2] %3")
                        .arg(i + 1)
                        .arg(entries.size())
                        .arg(QString::fromStdString(entry.metadata.path)));

        // Write file data from disk
        if (!writer.WriteFileFromDisk(entry, options.source_dir)) {
            LOG_ERROR << "Failed to write file to archive: " << entry.metadata.path;
            writer.Close();
            return false;
        }
    }

    // Finalize: write footer with checksum
    if (!writer.Finalize()) {
        writer.Close();
        return false;
    }

    writer.Close();
    return true;
}

} // namespace backup
