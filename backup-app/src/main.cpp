#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>

#include "ui/main_window.h"
#include "core/config_manager.h"
#include "common/logger.h"

int main(int argc, char* argv[]) {
    // Initialize Qt application
    QApplication app(argc, argv);
    app.setApplicationName("Data Backup Tool");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("BackupApp");

    // Initialize logger
    std::string log_dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation).toStdString();

    // Ensure log directory exists
    QDir().mkpath(QString::fromStdString(log_dir));

    std::string log_path = log_dir + "/backup-app.log";
    backup::Logger::Instance().Init(log_path, backup::LogLevel::kInfo);

    LOG_INFO << "=============================================";
    LOG_INFO << "Data Backup Tool v1.0 starting";
    LOG_INFO << "Log file: " << log_path;
    LOG_INFO << "=============================================";

    // Load configuration
    std::string config_path = log_dir + "/config.json";
    backup::ConfigManager::Instance().Load(config_path);

    LOG_INFO << "Config loaded from: " << config_path;

    // Create and show main window
    MainWindow main_window;
    main_window.show();

    LOG_INFO << "Main window shown, entering event loop";

    // Run event loop
    int ret = app.exec();

    // Cleanup
    LOG_INFO << "Application shutting down (exit code: " << ret << ")";
    backup::ConfigManager::Instance().Save();
    backup::Logger::Instance().Shutdown();

    return ret;
}
