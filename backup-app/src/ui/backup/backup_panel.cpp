#include "ui/backup/backup_panel.h"
#include "core/config_manager.h"
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

BackupPanel::BackupPanel(QWidget* parent)
    : QWidget(parent)
{
    SetupUI();
}

BackupPanel::~BackupPanel() {
    if (worker_thread_ && worker_thread_->isRunning() && engine_) {
        engine_->Cancel();
        worker_thread_->quit();
        worker_thread_->wait(5000);
    }
}

void BackupPanel::SetupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(20, 20, 20, 20);

    // ============================================================
    // Source & Destination group
    // ============================================================
    QGroupBox* config_group = new QGroupBox(tr("Backup Configuration"));
    QFormLayout* form = new QFormLayout(config_group);
    form->setSpacing(10);

    // Source directory
    QHBoxLayout* source_layout = new QHBoxLayout();
    source_edit_ = new QLineEdit();
    source_edit_->setPlaceholderText(tr("Select source directory to backup..."));
    source_edit_->setReadOnly(true);
    source_browse_ = new QPushButton(tr("Browse..."));
    source_browse_->setFixedWidth(100);
    source_layout->addWidget(source_edit_, 1);
    source_layout->addWidget(source_browse_);
    form->addRow(tr("Source:"), source_layout);

    // Destination path
    QHBoxLayout* dest_layout = new QHBoxLayout();
    dest_edit_ = new QLineEdit();
    dest_edit_->setPlaceholderText(tr("Select destination folder for backup archive..."));
    dest_edit_->setReadOnly(true);
    dest_browse_ = new QPushButton(tr("Browse..."));
    dest_browse_->setFixedWidth(100);
    dest_layout->addWidget(dest_edit_, 1);
    dest_layout->addWidget(dest_browse_);
    form->addRow(tr("Destination:"), dest_layout);

    // Backup name
    name_edit_ = new QLineEdit();
    name_edit_->setPlaceholderText(tr("Optional: enter a name for this backup"));
    form->addRow(tr("Name:"), name_edit_);

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

    status_label_ = new QLabel(tr("Configure backup settings and click Start."));
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

    start_btn_ = new QPushButton(tr("▶ Start Backup"));
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
    log_output_->document()->setMaximumBlockCount(500);
    log_output_->setFont(QFont("Monospace", 10));
    log_output_->setStyleSheet("background: #1e1e1e; color: #d4d4d4;");
    log_layout->addWidget(log_output_);

    main_layout->addWidget(log_group, 1);  // stretch factor 1

    // ============================================================
    // Connections
    // ============================================================
    connect(source_browse_, &QPushButton::clicked, this, &BackupPanel::OnBrowseSource);
    connect(dest_browse_,   &QPushButton::clicked, this, &BackupPanel::OnBrowseDestination);
    connect(start_btn_,     &QPushButton::clicked, this, &BackupPanel::OnStartBackup);
    connect(cancel_btn_,    &QPushButton::clicked, this, &BackupPanel::OnCancelBackup);

    // Restore last used paths
    auto& cfg = backup::ConfigManager::Instance();
    std::string last_src = cfg.GetLastSourceDir();
    std::string last_dst = cfg.GetLastDestDir();
    if (!last_src.empty()) source_edit_->setText(QString::fromStdString(last_src));
    if (!last_dst.empty()) dest_edit_->setText(QString::fromStdString(last_dst));
}

void BackupPanel::OnBrowseSource() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Source Directory"),
        source_edit_->text().isEmpty() ? QDir::homePath() : source_edit_->text());
    if (!dir.isEmpty()) {
        source_edit_->setText(dir);
        backup::ConfigManager::Instance().SetLastSourceDir(dir.toStdString());
    }
}

void BackupPanel::OnBrowseDestination() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Destination Directory"),
        dest_edit_->text().isEmpty() ? QDir::homePath() : dest_edit_->text());
    if (!dir.isEmpty()) {
        dest_edit_->setText(dir);
        backup::ConfigManager::Instance().SetLastDestDir(dir.toStdString());
    }
}

void BackupPanel::OnStartBackup() {
    // Validate inputs
    if (source_edit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Please select a source directory."));
        return;
    }
    if (dest_edit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Please select a destination directory."));
        return;
    }

    // Guard against double-click
    if (running_) return;
    running_ = true;

    // Reset UI
    log_output_->clear();
    progress_bar_->setValue(0);
    progress_bar_->setFormat(tr("Starting..."));
    stats_label_->clear();

    // ⚠️ Gather options in MAIN thread before engine moves to worker thread.
    // Accessing QLineEdit::text() from a worker thread is undefined behavior.
    backup::BackupOptions options = GatherOptions();

    // Create engine and thread
    engine_ = new backup::BackupEngine();
    worker_thread_ = new QThread(this);

    engine_->moveToThread(worker_thread_);

    // Connect signals (capture engine + options by value — no 'this' access in worker)
    connect(worker_thread_, &QThread::started, engine_, [engine = engine_, options]() {
        engine->Execute(options);
    });

    connect(engine_, &backup::BackupEngine::ProgressUpdated,
            this, &BackupPanel::OnProgressUpdated);

    connect(engine_, &backup::BackupEngine::BackupCompleted,
            this, &BackupPanel::OnBackupCompleted);

    connect(engine_, &backup::BackupEngine::LogMessage,
            this, &BackupPanel::OnLogMessage);

    // Cleanup when done
    connect(engine_, &backup::BackupEngine::BackupCompleted,
            worker_thread_, &QThread::quit);

    connect(worker_thread_, &QThread::finished, engine_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    // Start
    SetUIMode(true);
    worker_thread_->start();

    emit OperationStarted(tr("Backing up %1...").arg(source_edit_->text()));
}

void BackupPanel::OnCancelBackup() {
    if (running_ && engine_) {
        engine_->Cancel();
        cancel_btn_->setEnabled(false);
        status_label_->setText(tr("Cancelling..."));
    }
}

void BackupPanel::OnProgressUpdated(const backup::BackupProgress& progress) {
    // Update progress bar
    if (progress.total_bytes > 0) {
        int pct = static_cast<int>(progress.processed_bytes * 100 / progress.total_bytes);
        progress_bar_->setValue(pct);
        progress_bar_->setFormat(QString("%1%").arg(pct));
    }

    // Update status
    if (!progress.current_file.empty()) {
        status_label_->setText(
            QString::fromStdString(progress.current_file));
    }

    // Update stats
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

void BackupPanel::OnBackupCompleted(bool success, const QString& message) {
    SetUIMode(false);

    if (success) {
        progress_bar_->setValue(100);
        progress_bar_->setFormat(tr("Complete!"));
        status_label_->setText(message);
        QMessageBox::information(this, tr("Backup Complete"), message);
    } else {
        progress_bar_->setFormat(tr("Failed"));
        status_label_->setText(tr("ERROR: %1").arg(message));
        QMessageBox::critical(this, tr("Backup Failed"), message);
    }

    emit OperationFinished(success, message);

    running_ = false;
}

void BackupPanel::OnLogMessage(const QString& level, const QString& message) {
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

    // Auto-scroll to bottom
    QScrollBar* sb = log_output_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void BackupPanel::SetUIMode(bool running) {
    source_edit_->setEnabled(!running);
    dest_edit_->setEnabled(!running);
    name_edit_->setEnabled(!running);
    source_browse_->setEnabled(!running);
    dest_browse_->setEnabled(!running);
    start_btn_->setEnabled(!running);
    cancel_btn_->setEnabled(running);
}

backup::BackupOptions BackupPanel::GatherOptions() const {
    backup::BackupOptions opts;
    opts.source_dir  = source_edit_->text().toStdString();
    opts.dest_path   = dest_edit_->text().toStdString();
    opts.backup_name = name_edit_->text().toStdString();
    opts.type        = backup::BackupType::kFull;
    return opts;
}
