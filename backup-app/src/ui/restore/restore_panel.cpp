#include "ui/restore/restore_panel.h"
#include "common/file_utils.h"
#include "common/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QDateTime>

RestorePanel::RestorePanel(QWidget* parent)
    : QWidget(parent)
{
    SetupUI();
}

RestorePanel::~RestorePanel() {
    if (worker_thread_ && worker_thread_->isRunning()) {
        engine_->Cancel();
        worker_thread_->quit();
        worker_thread_->wait(5000);
    }
}

void RestorePanel::SetupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(20, 20, 20, 20);

    // ============================================================
    // Restore Configuration group
    // ============================================================
    QGroupBox* config_group = new QGroupBox(tr("Restore Configuration"));
    QFormLayout* form = new QFormLayout(config_group);
    form->setSpacing(10);

    // Archive file
    QHBoxLayout* archive_layout = new QHBoxLayout();
    archive_edit_ = new QLineEdit();
    archive_edit_->setPlaceholderText(tr("Select backup archive file (*.bkp)..."));
    archive_edit_->setReadOnly(true);
    archive_browse_ = new QPushButton(tr("Browse..."));
    archive_browse_->setFixedWidth(100);
    archive_layout->addWidget(archive_edit_, 1);
    archive_layout->addWidget(archive_browse_);
    form->addRow(tr("Archive:"), archive_layout);

    // Destination directory
    QHBoxLayout* dest_layout = new QHBoxLayout();
    dest_edit_ = new QLineEdit();
    dest_edit_->setPlaceholderText(tr("Select restore destination directory..."));
    dest_edit_->setReadOnly(true);
    dest_browse_ = new QPushButton(tr("Browse..."));
    dest_browse_->setFixedWidth(100);
    dest_layout->addWidget(dest_edit_, 1);
    dest_layout->addWidget(dest_browse_);
    form->addRow(tr("Restore to:"), dest_layout);

    main_layout->addWidget(config_group);

    // ============================================================
    // Progress group
    // ============================================================
    QGroupBox* progress_group = new QGroupBox(tr("Progress"));
    QVBoxLayout* progress_layout = new QVBoxLayout(progress_group);

    progress_bar_ = new QProgressBar();
    progress_bar_->setMinimum(0);
    progress_bar_->setMaximum(100);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(true);
    progress_bar_->setFormat(tr("Ready"));
    progress_layout->addWidget(progress_bar_);

    status_label_ = new QLabel(tr("Select a backup archive and destination to restore."));
    status_label_->setWordWrap(true);
    progress_layout->addWidget(status_label_);

    stats_label_ = new QLabel("");
    stats_label_->setStyleSheet("color: #666; font-size: 12px;");
    progress_layout->addWidget(stats_label_);

    main_layout->addWidget(progress_group);

    // ============================================================
    // Buttons
    // ============================================================
    QHBoxLayout* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    cancel_btn_ = new QPushButton(tr("Cancel"));
    cancel_btn_->setObjectName("cancelBtn");
    cancel_btn_->setEnabled(false);
    cancel_btn_->setFixedWidth(120);
    btn_layout->addWidget(cancel_btn_);

    start_btn_ = new QPushButton(tr("▶ Start Restore"));
    start_btn_->setObjectName("startBtn");
    start_btn_->setFixedWidth(160);
    btn_layout->addWidget(start_btn_);

    main_layout->addLayout(btn_layout);

    // ============================================================
    // Log output
    // ============================================================
    QGroupBox* log_group = new QGroupBox(tr("Operation Log"));
    QVBoxLayout* log_layout = new QVBoxLayout(log_group);

    log_output_ = new QTextEdit();
    log_output_->setReadOnly(true);
    log_output_->setMaximumBlockCount(500);
    log_output_->setFont(QFont("Monospace", 10));
    log_output_->setStyleSheet("background: #1e1e1e; color: #d4d4d4;");
    log_layout->addWidget(log_output_);

    main_layout->addWidget(log_group, 1);

    // ============================================================
    // Connections
    // ============================================================
    connect(archive_browse_, &QPushButton::clicked, this, &RestorePanel::OnBrowseArchive);
    connect(dest_browse_,    &QPushButton::clicked, this, &RestorePanel::OnBrowseDestination);
    connect(start_btn_,      &QPushButton::clicked, this, &RestorePanel::OnStartRestore);
    connect(cancel_btn_,     &QPushButton::clicked, this, &RestorePanel::OnCancelRestore);
}

void RestorePanel::OnBrowseArchive() {
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Backup Archive"),
        QDir::homePath(),
        tr("Backup Archives (*.bkp);;All Files (*)"));
    if (!file.isEmpty()) {
        archive_edit_->setText(file);
    }
}

void RestorePanel::OnBrowseDestination() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Restore Destination"),
        QDir::homePath());
    if (!dir.isEmpty()) {
        dest_edit_->setText(dir);
    }
}

void RestorePanel::OnStartRestore() {
    // Validate inputs
    if (archive_edit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Please select a backup archive file."));
        return;
    }
    if (dest_edit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Please select a destination directory."));
        return;
    }

    // Check archive exists
    std::string archive_path = archive_edit_->text().toStdString();
    if (!backup::FileUtils::Exists(archive_path)) {
        QMessageBox::critical(this, tr("File Not Found"),
                              tr("Backup archive not found:\n%1").arg(archive_edit_->text()));
        return;
    }

    // Reset UI
    log_output_->clear();
    progress_bar_->setValue(0);
    progress_bar_->setFormat(tr("Starting..."));
    stats_label_->clear();

    // Create engine and thread
    engine_ = new backup::RestoreEngine();
    worker_thread_ = new QThread(this);

    engine_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, engine_, [this]() {
        auto options = GatherOptions();
        engine_->Execute(options);
    });

    connect(engine_, &backup::RestoreEngine::ProgressUpdated,
            this, &RestorePanel::OnProgressUpdated);

    connect(engine_, &backup::RestoreEngine::RestoreCompleted,
            this, &RestorePanel::OnRestoreCompleted);

    connect(engine_, &backup::RestoreEngine::LogMessage,
            this, &RestorePanel::OnLogMessage);

    connect(engine_, &backup::RestoreEngine::RestoreCompleted,
            worker_thread_, &QThread::quit);

    connect(worker_thread_, &QThread::finished, engine_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    SetUIMode(true);
    worker_thread_->start();

    emit OperationStarted(tr("Restoring to %1...").arg(dest_edit_->text()));
}

void RestorePanel::OnCancelRestore() {
    if (engine_) {
        engine_->Cancel();
        cancel_btn_->setEnabled(false);
        status_label_->setText(tr("Cancelling..."));
    }
}

void RestorePanel::OnProgressUpdated(const backup::BackupProgress& progress) {
    if (progress.total_bytes > 0) {
        int pct = static_cast<int>(progress.processed_bytes * 100 / progress.total_bytes);
        progress_bar_->setValue(pct);
        progress_bar_->setFormat(QString("%1%").arg(pct));
    }

    if (!progress.current_file.empty()) {
        status_label_->setText(
            QString::fromStdString(progress.current_file));
    }

    QString stats;
    stats += tr("Files: %1 / %2")
        .arg(progress.processed_files)
        .arg(progress.total_files);

    if (progress.bytes_per_second > 0) {
        double mbps = progress.bytes_per_second / (1024.0 * 1024.0);
        stats += tr("  |  Speed: %1 MB/s").arg(mbps, 0, 'f', 1);
    }

    if (progress.estimated_remaining_seconds > 0) {
        int mins = progress.estimated_remaining_seconds / 60;
        int secs = progress.estimated_remaining_seconds % 60;
        stats += tr("  |  Remaining: %1:%2")
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }

    stats_label_->setText(stats);
}

void RestorePanel::OnRestoreCompleted(bool success, const QString& message) {
    SetUIMode(false);

    if (success) {
        progress_bar_->setValue(100);
        progress_bar_->setFormat(tr("Complete!"));
        status_label_->setText(message);
        QMessageBox::information(this, tr("Restore Complete"), message);
    } else {
        progress_bar_->setFormat(tr("Failed"));
        status_label_->setText(tr("ERROR: %1").arg(message));
        QMessageBox::critical(this, tr("Restore Failed"), message);
    }

    emit OperationFinished(success, message);

    worker_thread_ = nullptr;
    engine_ = nullptr;
}

void RestorePanel::OnLogMessage(const QString& level, const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color;

    if (level == "ERROR") color = "#f44747";
    else if (level == "WARN")  color = "#cca700";
    else if (level == "INFO")  color = "#6a9955";
    else color = "#d4d4d4";

    log_output_->append(
        QString("<span style='color:%1'>[%2] [%3]</span> %4")
        .arg(color, timestamp, level,
             message.toHtmlEscaped()));

    QScrollBar* sb = log_output_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void RestorePanel::SetUIMode(bool running) {
    archive_edit_->setEnabled(!running);
    dest_edit_->setEnabled(!running);
    archive_browse_->setEnabled(!running);
    dest_browse_->setEnabled(!running);
    start_btn_->setEnabled(!running);
    cancel_btn_->setEnabled(running);
}

backup::RestoreOptions RestorePanel::GatherOptions() const {
    backup::RestoreOptions opts;
    opts.archive_path    = archive_edit_->text().toStdString();
    opts.dest_dir        = dest_edit_->text().toStdString();
    opts.overwrite_existing = true;
    opts.restore_metadata   = true;
    return opts;
}
