#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <memory>

class BackupPanel;
class RestorePanel;

/// Main application window
///
/// Provides tab-based navigation between Backup and Restore panels,
/// menu bar, toolbar, status bar, and system tray integration.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void OnBackupTabSelected();
    void OnRestoreTabSelected();
    void OnAbout();
    void OnTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void OnShowWindow();

private:
    void CreateMenuBar();
    void CreateStatusBar();
    void CreateSystemTray();
    void SetupUI();
    void LoadSettings();
    void SaveSettings();
    void ApplyStyle();

    QTabWidget*    tab_widget_     = nullptr;
    BackupPanel*   backup_panel_   = nullptr;
    RestorePanel*  restore_panel_  = nullptr;
    QLabel*        status_label_   = nullptr;
    QSystemTrayIcon* tray_icon_    = nullptr;
};
