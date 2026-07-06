#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace backup {

/// Configuration manager (singleton)
///
/// Manages persistent configuration: backup task definitions,
/// global settings, and application state.
/// Uses a simple JSON file for storage.
class ConfigManager {
public:
    static ConfigManager& Instance();

    /// Load configuration from file
    bool Load(const std::string& config_path);

    /// Save configuration to file (uses path from Load)
    bool Save();

    /// Save to specific path
    bool SaveAs(const std::string& config_path);

    // ========== Backup task management ==========

    /// Get all configured backup tasks
    std::vector<BackupTaskConfig> GetBackupTasks() const;

    /// Get a specific task by ID
    std::optional<BackupTaskConfig> GetTask(const std::string& task_id) const;

    /// Add or update a task
    void AddOrUpdateTask(const BackupTaskConfig& task);

    /// Remove a task
    void RemoveTask(const std::string& task_id);

    // ========== Global settings ==========

    GlobalSettings GetGlobalSettings() const;
    void SetGlobalSettings(const GlobalSettings& settings);

    // ========== Recent paths ==========

    std::string GetLastSourceDir() const;
    void SetLastSourceDir(const std::string& path);

    std::string GetLastDestDir() const;
    void SetLastDestDir(const std::string& path);

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    std::string ToJson() const;
    bool FromJson(const std::string& json);

    std::string config_path_;
    GlobalSettings global_settings_;
    std::vector<BackupTaskConfig> tasks_;
    std::string last_source_dir_;
    std::string last_dest_dir_;
};

} // namespace backup
