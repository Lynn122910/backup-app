#include "ui/main_window.h"
#include "ui/backup/backup_panel.h"
#include "ui/restore/restore_panel.h"
#include "core/config_manager.h"
#include "common/logger.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QStyle>
#include <QIcon>
#include <QFont>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    SetupUI();
    CreateMenuBar();
    CreateStatusBar();
    CreateSystemTray();
    LoadSettings();
    ApplyStyle();

    setWindowTitle(tr("Data Backup Tool"));
    resize(900, 650);
}

MainWindow::~MainWindow() {
    SaveSettings();
}

void MainWindow::SetupUI() {
    // Central widget: tab widget with Backup and Restore tabs
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setTabPosition(QTabWidget::North);
    tab_widget_->setDocumentMode(true);

    backup_panel_  = new BackupPanel(this);
    restore_panel_ = new RestorePanel(this);

    tab_widget_->addTab(backup_panel_,  tr("💾 Backup"));
    tab_widget_->addTab(restore_panel_, tr("📂 Restore"));

    tab_widget_->setTabToolTip(0, tr("Create a new backup from a source directory"));
    tab_widget_->setTabToolTip(1, tr("Restore files from an existing backup archive"));

    setCentralWidget(tab_widget_);

    // Connect tab changes
    connect(tab_widget_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0) OnBackupTabSelected();
        else if (index == 1) OnRestoreTabSelected();
    });
}

void MainWindow::CreateMenuBar() {
    // File menu
    QMenu* file_menu = menuBar()->addMenu(tr("&File"));

    QAction* exit_action = file_menu->addAction(tr("E&xit"),
        this, &QWidget::close);
    exit_action->setShortcut(QKeySequence::Quit);
    exit_action->setStatusTip(tr("Exit the application"));

    // Help menu
    QMenu* help_menu = menuBar()->addMenu(tr("&Help"));

    QAction* about_action = help_menu->addAction(tr("&About"),
        this, &MainWindow::OnAbout);
    about_action->setStatusTip(tr("About this application"));
}

void MainWindow::CreateStatusBar() {
    status_label_ = new QLabel(tr("Ready"));
    statusBar()->addWidget(status_label_, 1);
    statusBar()->showMessage(tr("Welcome to Data Backup Tool"), 3000);
}

void MainWindow::CreateSystemTray() {
    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
    tray_icon_->setToolTip(tr("Data Backup Tool"));

    QMenu* tray_menu = new QMenu(this);
    tray_menu->addAction(tr("Show Window"), this, &MainWindow::OnShowWindow);
    tray_menu->addSeparator();
    tray_menu->addAction(tr("Exit"), this, &QWidget::close);

    tray_icon_->setContextMenu(tray_menu);

    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::OnTrayActivated);

    tray_icon_->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Minimize to tray instead of closing
    if (tray_icon_ && tray_icon_->isVisible()) {
        hide();
        tray_icon_->showMessage(
            tr("Data Backup Tool"),
            tr("Application minimized to tray. Double-click to restore."),
            QSystemTrayIcon::Information, 2000);
        event->ignore();
    } else {
        SaveSettings();
        event->accept();
    }
}

void MainWindow::OnBackupTabSelected() {
    statusBar()->showMessage(tr("Backup mode: select source and destination to create a backup"));
}

void MainWindow::OnRestoreTabSelected() {
    statusBar()->showMessage(tr("Restore mode: select a backup archive to restore files"));
}

void MainWindow::OnAbout() {
    QMessageBox::about(this, tr("About Data Backup Tool"),
        tr("<h3>Data Backup Tool v1.0</h3>"
           "<p>A file backup and restore application for Linux.</p>"
           "<p><b>Features:</b></p>"
           "<ul>"
           "<li>Full directory backup with file metadata preservation</li>"
           "<li>Single-file archive format with SHA-256 integrity checks</li>"
           "<li>Support for regular files, symlinks, directories, and special files</li>"
           "<li>Metadata preservation (permissions, timestamps, ownership, xattrs)</li>"
           "<li>Clean Qt graphical interface</li>"
           "</ul>"
           "<p>Built with C++17, Qt, and OpenSSL.</p>"));
}

void MainWindow::OnTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        OnShowWindow();
    }
}

void MainWindow::OnShowWindow() {
    show();
    raise();
    activateWindow();
}

void MainWindow::LoadSettings() {
    auto& cfg = backup::ConfigManager::Instance();
    // Load last window state if needed
    (void)cfg;  // suppress unused warning
}

void MainWindow::SaveSettings() {
    auto& cfg = backup::ConfigManager::Instance();
    cfg.Save();
}

void MainWindow::ApplyStyle() {
    // Apply a clean stylesheet
    setStyleSheet(R"(
        QMainWindow {
            background-color: #f5f5f5;
        }
        QTabWidget::pane {
            border: 1px solid #ccc;
            background: white;
            border-radius: 4px;
        }
        QTabBar::tab {
            padding: 8px 20px;
            margin-right: 2px;
            background: #e0e0e0;
            border: 1px solid #ccc;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            font-size: 13px;
        }
        QTabBar::tab:selected {
            background: white;
            border-bottom: 2px solid #0078d4;
            font-weight: bold;
        }
        QPushButton {
            padding: 6px 16px;
            border: 1px solid #ccc;
            border-radius: 4px;
            background: #f0f0f0;
            font-size: 13px;
        }
        QPushButton:hover {
            background: #e0e0e0;
        }
        QPushButton:pressed {
            background: #d0d0d0;
        }
        QPushButton#startBtn {
            background: #0078d4;
            color: white;
            border: none;
            padding: 8px 24px;
            font-weight: bold;
            font-size: 14px;
        }
        QPushButton#startBtn:hover {
            background: #106ebe;
        }
        QPushButton#startBtn:disabled {
            background: #ccc;
        }
        QPushButton#cancelBtn {
            background: #d32f2f;
            color: white;
            border: none;
            padding: 8px 24px;
            font-weight: bold;
            font-size: 14px;
        }
        QPushButton#cancelBtn:hover {
            background: #b71c1c;
        }
        QLineEdit {
            padding: 6px;
            border: 1px solid #ccc;
            border-radius: 4px;
            font-size: 13px;
        }
        QProgressBar {
            border: 1px solid #ccc;
            border-radius: 4px;
            text-align: center;
            height: 22px;
        }
        QProgressBar::chunk {
            background: #0078d4;
            border-radius: 3px;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #ddd;
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 15px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
        }
        QStatusBar {
            background: #e8e8e8;
            border-top: 1px solid #ccc;
        }
    )");
}
