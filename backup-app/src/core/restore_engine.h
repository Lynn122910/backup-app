#pragma once

#include "common/types.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <QObject>
#include <QString>

namespace backup {

class BackupManifest;
class BackupArchiveReader;

/// Progress callback type
using RestoreProgressCallback = std::function<void(const BackupProgress&)>;

/// Restore engine: orchestrates the restore process
///
/// Reads a backup archive and restores files to a destination directory.
/// Runs in a worker thread. Communicates via Qt signals.
class RestoreEngine : public QObject {
    Q_OBJECT
public:
    explicit RestoreEngine(QObject* parent = nullptr);
    ~RestoreEngine();

    // QObject is non-movable; these are implicitly deleted
    RestoreEngine(RestoreEngine&&) = delete;
    RestoreEngine& operator=(RestoreEngine&&) = delete;
    RestoreEngine(const RestoreEngine&) = delete;
    RestoreEngine& operator=(const RestoreEngine&) = delete;

    /// Set progress callback
    void SetProgressCallback(RestoreProgressCallback cb);

    /// Execute restore (blocking — run in worker thread)
    /// @param options  Restore configuration
    /// @return true on success
    bool Execute(const RestoreOptions& options);

    /// Request cancellation (thread-safe)
    void Cancel();

    /// Check if restore is running
    bool IsRunning() const;

signals:
    /// Emitted periodically during restore
    void ProgressUpdated(const backup::BackupProgress& progress);

    /// Emitted when restore completes
    void RestoreCompleted(bool success, const QString& message);

    /// Emitted on file conflict (overwrite decision already made via options)
    void FileConflict(const QString& path);

    /// Log messages
    void LogMessage(const QString& level, const QString& message);

private:
    /// Read archive and restore all files
    bool DoRestore(const RestoreOptions& options,
                   const std::shared_ptr<BackupManifest>& manifest);

    void UpdateProgress(const BackupProgress& progress);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backup
