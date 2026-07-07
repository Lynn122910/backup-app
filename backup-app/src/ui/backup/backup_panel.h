#pragma once

#include <QWidget>
#include <QThread>
#include <memory>

#include "common/types.h"
#include "core/backup_engine.h"

class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QGroupBox;
class QTextEdit;

/// Backup configuration and execution panel
///
/// Allows the user to configure a backup operation:
/// - Select source directory
/// - Select destination path
/// - Enter backup name
/// - Start/cancel backup
/// - View progress and log output
class BackupPanel : public QWidget {
    Q_OBJECT
public:
    explicit BackupPanel(QWidget* parent = nullptr);
    ~BackupPanel();

signals:
    /// Emitted when a backup operation starts/finishes (for status bar updates)
    void OperationStarted(const QString& description);
    void OperationFinished(bool success, const QString& message);

private slots:
    void OnBrowseSource();
    void OnBrowseDestination();
    void OnStartBackup();
    void OnCancelBackup();
    void OnProgressUpdated(const backup::BackupProgress& progress);
    void OnBackupCompleted(bool success, const QString& message);
    void OnLogMessage(const QString& level, const QString& message);

private:
    void SetupUI();
    void SetUIMode(bool running);
    backup::BackupOptions GatherOptions() const;

    // UI elements
    QLineEdit*    source_edit_    = nullptr;
    QLineEdit*    dest_edit_      = nullptr;
    QLineEdit*    name_edit_      = nullptr;
    QPushButton*  source_browse_  = nullptr;
    QPushButton*  dest_browse_    = nullptr;
    QPushButton*  start_btn_      = nullptr;
    QPushButton*  cancel_btn_     = nullptr;
    QProgressBar* progress_bar_   = nullptr;
    QLabel*       status_label_   = nullptr;
    QLabel*       stats_label_    = nullptr;
    QTextEdit*    log_output_     = nullptr;

    // Engine and thread
    QThread*      worker_thread_  = nullptr;
    backup::BackupEngine* engine_ = nullptr;
    bool          running_        = false;
};
