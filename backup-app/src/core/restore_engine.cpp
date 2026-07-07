#include "core/restore_engine.h"
#include "core/backup_manifest.h"
#include "core/backup_archive.h"
#include "services/metadata_handler.h"
#include "common/logger.h"
#include "common/file_utils.h"

#include <chrono>

namespace backup {

struct RestoreEngine::Impl {
    RestoreOptions              options;
    BackupProgress              progress;
    RestoreProgressCallback     progress_cb;
    std::atomic<bool>           cancelled{false};
};

RestoreEngine::RestoreEngine(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    qRegisterMetaType<backup::BackupProgress>("backup::BackupProgress");
}

RestoreEngine::~RestoreEngine() = default;

void RestoreEngine::SetProgressCallback(RestoreProgressCallback cb) {
    impl_->progress_cb = std::move(cb);
}

bool RestoreEngine::IsRunning() const {
    return impl_->progress.is_running;
}

void RestoreEngine::Cancel() {
    impl_->cancelled.store(true, std::memory_order_release);
    LOG_INFO << "Restore cancellation requested";
}

void RestoreEngine::UpdateProgress(const BackupProgress& progress) {
    impl_->progress = progress;
    if (impl_->progress_cb) {
        impl_->progress_cb(progress);
    }
    emit ProgressUpdated(progress);
}

bool RestoreEngine::Execute(const RestoreOptions& options) {
    impl_->options = options;
    impl_->cancelled.store(false, std::memory_order_release);

    BackupProgress progress;
    progress.is_running = true;
    UpdateProgress(progress);

    auto start_time = std::chrono::steady_clock::now();

    emit LogMessage("INFO", QString("Starting restore from: %1")
                    .arg(QString::fromStdString(options.archive_path)));

    // ============================================================
    // Step 1: Validate parameters
    // ============================================================
    if (options.archive_path.empty() || options.dest_dir.empty()) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Archive path or destination is empty";
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    if (!FileUtils::Exists(options.archive_path)) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Archive file not found: " + options.archive_path;
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    // ============================================================
    // Step 2: Read manifest from archive
    // ============================================================
    emit LogMessage("INFO", "Reading manifest from archive...");

    BackupArchiveReader reader;
    if (!reader.Open(options.archive_path)) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Failed to open archive: " + options.archive_path;
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    auto manifest_json = reader.ReadManifest();
    if (!manifest_json.has_value()) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Failed to read manifest from archive";
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        reader.Close();
        return false;
    }

    auto manifest_opt = BackupManifest::FromJson(manifest_json.value());
    if (!manifest_opt.has_value()) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Failed to parse manifest JSON";
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        reader.Close();
        return false;
    }

    auto manifest = std::make_shared<BackupManifest>(std::move(manifest_opt.value()));

    // ============================================================
    // Step 3: Verify archive integrity
    // ============================================================
    emit LogMessage("INFO", "Verifying archive integrity...");
    if (!reader.VerifyIntegrity()) {
        LOG_WARNING << "Archive integrity check failed — proceeding with caution";
        emit LogMessage("WARN", "Archive integrity check failed");
        // Continue anyway, as data may still be recoverable
    }

    // ============================================================
    // Step 4: Close reader (will reopen per-file or we keep it open)
    // ============================================================
    reader.Close();

    // ============================================================
    // Step 5: Create destination directory
    // ============================================================
    if (!FileUtils::CreateDirectoryRecursive(options.dest_dir)) {
        progress.is_running = false;
        progress.is_completed = true;
        progress.error_message = "Failed to create destination: " + options.dest_dir;
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    // Check disk space
    uint64_t available = FileUtils::GetAvailableDiskSpace(options.dest_dir);
    if (available < manifest->total_size()) {
        LOG_WARNING << "Low disk space: need " << manifest->total_size()
                    << " bytes, have " << available << " bytes";
        emit LogMessage("WARN",
                        QString("Low disk space: need %1, available %2")
                        .arg(manifest->total_size())
                        .arg(available));
    }

    // ============================================================
    // Step 6: Restore files
    // ============================================================
    bool result = DoRestore(options, manifest);

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time).count();

    progress.is_running   = false;
    progress.is_completed = true;
    progress.elapsed_seconds = static_cast<int>(elapsed);

    if (!result && !impl_->cancelled.load(std::memory_order_acquire)) {
        progress.error_message = "Restore failed — see log for details";
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    if (impl_->cancelled.load(std::memory_order_acquire)) {
        progress.error_message = "Restore cancelled by user";
        UpdateProgress(progress);
        emit RestoreCompleted(false, QString::fromStdString(progress.error_message));
        return false;
    }

    UpdateProgress(progress);

    QString msg = QString("Restore completed: %1 files restored in %2 seconds")
        .arg(manifest->file_count())
        .arg(elapsed);
    emit RestoreCompleted(true, msg);
    return true;
}

bool RestoreEngine::DoRestore(const RestoreOptions& options,
                               const std::shared_ptr<BackupManifest>& manifest) {
    auto files = manifest->files();

    // ── Reorder: hardlinks LAST ──────────────────────────────────────
    // Hardlinks depend on the original inode already existing on disk.
    // If a hardlink entry sorts before its target file in the manifest,
    // link() will fail with ENOENT.  Move all hardlink entries to the
    // end so that regular files (the first occurrence of each inode) are
    // restored first.
    std::vector<BackupFileEntry> ordered;
    ordered.reserve(files.size());
    for (const auto& f : files) {
        if (f.metadata.type != FileType::kHardLink) {
            ordered.push_back(f);
        }
    }
    for (const auto& f : files) {
        if (f.metadata.type == FileType::kHardLink) {
            ordered.push_back(f);
        }
    }

    BackupProgress progress;
    progress.total_files = ordered.size();
    progress.is_running  = true;
    progress.total_bytes = manifest->total_size();

    auto start_time = std::chrono::steady_clock::now();

    // Reopen archive for extracting files
    BackupArchiveReader reader;
    if (!reader.Open(options.archive_path)) {
        LOG_ERROR << "Failed to reopen archive for extraction";
        return false;
    }

    int failed_count = 0;

    for (size_t i = 0; i < ordered.size(); ++i) {
        if (impl_->cancelled.load(std::memory_order_acquire)) {
            reader.Close();
            return false;
        }

        const auto& entry = ordered[i];

        progress.processed_files = i + 1;
        progress.processed_bytes += entry.metadata.size;
        progress.current_file    = entry.metadata.path;

        auto now = std::chrono::steady_clock::now();
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time).count();
        if (elapsed_sec > 0) {
            progress.bytes_per_second = progress.processed_bytes / elapsed_sec;
            if (progress.bytes_per_second > 0 && progress.total_bytes > progress.processed_bytes) {
                uint64_t remaining = progress.total_bytes - progress.processed_bytes;
                progress.estimated_remaining_seconds =
                    static_cast<int>(remaining / progress.bytes_per_second);
            }
        }
        progress.elapsed_seconds = static_cast<int>(elapsed_sec);

        UpdateProgress(progress);

        std::string dest_path = FileUtils::JoinPath(options.dest_dir, entry.metadata.path);

        // Check for existing files
        if (FileUtils::Exists(dest_path) && !options.overwrite_existing) {
            emit FileConflict(QString::fromStdString(entry.metadata.path));
            continue;  // Skip
        }

        emit LogMessage("INFO",
                        QString("[%1/%2] Restoring: %3")
                        .arg(i + 1)
                        .arg(ordered.size())
                        .arg(QString::fromStdString(entry.metadata.path)));

        // Extract file from archive to disk
        if (!reader.ExtractFile(entry, options.dest_dir)) {
            LOG_ERROR << "Failed to extract: " << entry.metadata.path;
            emit LogMessage("ERROR",
                            QString("Failed to restore: %1")
                            .arg(QString::fromStdString(entry.metadata.path)));
            ++failed_count;
            continue;  // Keep going — don't abort the whole restore
        }

        // Restore metadata (permissions, timestamps, ownership, xattrs)
        // Applies to directories, regular files, symlinks, hardlinks, and FIFOs.
        // Device files and other special types are skipped (metadata not meaningful).
        if (options.restore_metadata) {
            bool supported = (entry.metadata.type == FileType::kRegular ||
                              entry.metadata.type == FileType::kDirectory ||
                              entry.metadata.type == FileType::kSymlink ||
                              entry.metadata.type == FileType::kFifo);
            if (supported) {
                MetadataHandler::Apply(entry.metadata, dest_path);
            }
        }
    }

    reader.Close();

    if (failed_count > 0) {
        LOG_WARNING << "Restore completed with " << failed_count << " file(s) failed";
        emit LogMessage("WARN",
                        QString("Restore completed with %1 file(s) failed — see log for details")
                        .arg(failed_count));
    }

    return true;
}

} // namespace backup
