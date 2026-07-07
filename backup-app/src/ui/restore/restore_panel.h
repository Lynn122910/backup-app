#pragma once

#include <QWidget>
#include <QThread>
#include <memory>

#include "common/types.h"
#include "core/restore_engine.h"

class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QGroupBox;
class QTextEdit;

/// Restore configuration and execution panel
///
/// Allows the user to restore files from a backup archive:
/// - Select archive file (.bkp)
/// - Select destination directory
/// - Start/cancel restore
/// - View progress and log output
class RestorePanel : public QWidget {
    Q_OBJECT
public:
    explicit RestorePanel(QWidget* parent = nullptr);
    ~RestorePanel();

signals:
    void OperationStarted(const QString& description);
    void OperationFinished(bool success, const QString& message);

private slots:
    void OnBrowseArchive();
    void OnBrowseDestination();
    void OnStartRestore();
    void OnCancelRestore();
    void OnProgressUpdated(const backup::BackupProgress& progress);
    void OnRestoreCompleted(bool success, const QString& message);
    void OnLogMessage(const QString& level, const QString& message);

private:
    void SetupUI();
    void SetUIMode(bool running);
    backup::RestoreOptions GatherOptions() const;

    // UI elements
    QLineEdit*    archive_edit_   = nullptr;
    QLineEdit*    dest_edit_      = nullptr;
    QPushButton*  archive_browse_ = nullptr;
    QPushButton*  dest_browse_    = nullptr;
    QPushButton*  start_btn_      = nullptr;
    QPushButton*  cancel_btn_     = nullptr;
    QProgressBar* progress_bar_   = nullptr;
    QLabel*       status_label_   = nullptr;
    QLabel*       stats_label_    = nullptr;
    QTextEdit*    log_output_     = nullptr;

    // Engine and thread
    QThread*        worker_thread_ = nullptr;
    backup::RestoreEngine* engine_ = nullptr;
    bool            running_       = false;
};
