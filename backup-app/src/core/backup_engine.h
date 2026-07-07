#pragma once

#include "common/types.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <QObject>
#include <QString>

namespace backup {

class FileScanner;
class BackupManifest;
class BackupArchiveWriter;

/// Progress callback for backup operations
using BackupProgressCallback = std::function<void(const BackupProgress&)>;

/// Backup engine: orchestrates the backup process
///
/// Runs in a worker thread. Communicates progress via Qt signals.
/// Usage:
///   1. Create BackupEngine instance
///   2. Connect to signals (ProgressUpdated, BackupCompleted, LogMessage)
///   3. Call Execute() in a QThread
///   4. Call Cancel() to abort
class BackupEngine : public QObject {
    Q_OBJECT
public:
    explicit BackupEngine(QObject* parent = nullptr);
    ~BackupEngine();

    // QObject is non-movable; these are implicitly deleted
    BackupEngine(BackupEngine&&) = delete;
    BackupEngine& operator=(BackupEngine&&) = delete;
    BackupEngine(const BackupEngine&) = delete;
    BackupEngine& operator=(const BackupEngine&) = delete;

    /// Set progress callback (alternative to signal)
    void SetProgressCallback(BackupProgressCallback cb);

    /// Execute backup (blocking — run in worker thread)
    /// @param options  Backup configuration
    /// @return true on success
    bool Execute(const BackupOptions& options);

    /// Request cancellation (thread-safe)
    void Cancel();

    /// Check if backup is running
    bool IsRunning() const;

    /// Get the manifest from the last successful backup
    std::shared_ptr<BackupManifest> GetLastManifest() const;

signals:
    /// Emitted periodically during backup
    void ProgressUpdated(const backup::BackupProgress& progress);

    /// Emitted when backup completes (success or failure)
    void BackupCompleted(bool success, const QString& message);

    /// Emitted for log messages from the engine
    void LogMessage(const QString& level, const QString& message);

private:
    /// Scan source directory and prepare file list
    std::vector<BackupFileEntry> PrepareFileList(const BackupOptions& options);

    /// Write all files to the archive
    bool WriteArchive(const std::vector<BackupFileEntry>& entries,
                      const BackupOptions& options,
                      std::shared_ptr<BackupManifest> manifest);

    /// Update and emit progress
    void UpdateProgress(const BackupProgress& progress);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backup

// Register BackupProgress with Qt's meta-type system
Q_DECLARE_METATYPE(backup::BackupProgress)
