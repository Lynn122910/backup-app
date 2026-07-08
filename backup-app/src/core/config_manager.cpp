#include "core/config_manager.h"
#include "common/logger.h"
#include "common/file_utils.h"

#include <algorithm>
#include <sstream>

namespace backup {

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::string& config_path) {
    config_path_ = config_path;

    if (!FileUtils::Exists(config_path)) {
        LOG_INFO << "Config file not found, using defaults: " << config_path;
        return true;  // Not an error, use defaults
    }

    std::string content = FileUtils::ReadFileToString(config_path);
    if (content.empty()) {
        LOG_WARNING << "Config file is empty: " << config_path;
        return true;
    }

    if (!FromJson(content)) {
        LOG_ERROR << "Failed to parse config file: " << config_path;
        return false;
    }

    LOG_INFO << "Config loaded from: " << config_path;
    return true;
}

bool ConfigManager::Save() {
    if (config_path_.empty()) {
        LOG_ERROR << "No config path set — call Load() or SaveAs() first";
        return false;
    }
    return SaveAs(config_path_);
}

bool ConfigManager::SaveAs(const std::string& config_path) {
    config_path_ = config_path;
    std::string json = ToJson();
    return FileUtils::WriteStringToFile(config_path, json);
}

std::vector<BackupTaskConfig> ConfigManager::GetBackupTasks() const {
    return tasks_;
}

std::optional<BackupTaskConfig> ConfigManager::GetTask(const std::string& task_id) const {
    for (const auto& task : tasks_) {
        if (task.id == task_id) return std::make_optional(task);
    }
    return std::nullopt;
}

void ConfigManager::AddOrUpdateTask(const BackupTaskConfig& task) {
    for (auto& existing : tasks_) {
        if (existing.id == task.id) {
            existing = task;
            return;
        }
    }
    tasks_.push_back(task);
}

void ConfigManager::RemoveTask(const std::string& task_id) {
    tasks_.erase(
        std::remove_if(tasks_.begin(), tasks_.end(),
                       [&task_id](const BackupTaskConfig& t) {
                           return t.id == task_id;
                       }),
        tasks_.end());
}

GlobalSettings ConfigManager::GetGlobalSettings() const {
    return global_settings_;
}

void ConfigManager::SetGlobalSettings(const GlobalSettings& settings) {
    global_settings_ = settings;
}

std::string ConfigManager::GetLastSourceDir() const {
    return last_source_dir_;
}

void ConfigManager::SetLastSourceDir(const std::string& path) {
    last_source_dir_ = path;
}

std::string ConfigManager::GetLastDestDir() const {
    return last_dest_dir_;
}

void ConfigManager::SetLastDestDir(const std::string& path) {
    last_dest_dir_ = path;
}

std::string ConfigManager::ToJson() const {
    std::ostringstream oss;
    oss << "{\n";

    // Version
    oss << "  \"version\": \"1.0\",\n";

    // Global settings
    oss << "  \"global_settings\": {\n";
    oss << "    \"language\": \"" << json::EscapeString(global_settings_.language) << "\",\n";
    oss << "    \"log_level\": \"" << json::EscapeString(global_settings_.log_level) << "\",\n";
    oss << "    \"log_path\": \"" << json::EscapeString(global_settings_.log_path) << "\"\n";
    oss << "  },\n";

    // Recent paths
    oss << "  \"last_source_dir\": \"" << json::EscapeString(last_source_dir_) << "\",\n";
    oss << "  \"last_dest_dir\": \"" << json::EscapeString(last_dest_dir_) << "\",\n";

    // Tasks
    oss << "  \"backup_tasks\": [\n";
    for (size_t i = 0; i < tasks_.size(); ++i) {
        const auto& t = tasks_[i];
        oss << "    {\n";
        oss << "      \"id\": \"" << json::EscapeString(t.id) << "\",\n";
        oss << "      \"name\": \"" << json::EscapeString(t.name) << "\",\n";
        oss << "      \"source_dir\": \"" << json::EscapeString(t.source_dir) << "\",\n";
        oss << "      \"dest_dir\": \"" << json::EscapeString(t.dest_dir) << "\",\n";
        // Filters
        oss << "      \"filters\": [\n";
        for (size_t j = 0; j < t.filters.size(); ++j) {
            oss << "        " << json::FilterRuleToJson(t.filters[j]);
            if (j < t.filters.size() - 1) oss << ",";
            oss << "\n";
        }
        oss << "      ]\n";
        oss << "    }";
        if (i < tasks_.size() - 1) oss << ",";
        oss << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

bool ConfigManager::FromJson(const std::string& json_str) {
    // Parse global settings
    auto gs_obj = json::ExtractObjectString(json_str, "global_settings");
    if (gs_obj.has_value()) {
        auto lang = json::ExtractStringValue(gs_obj.value(), "language");
        if (lang.has_value()) global_settings_.language = lang.value();

        auto log_level = json::ExtractStringValue(gs_obj.value(), "log_level");
        if (log_level.has_value()) global_settings_.log_level = log_level.value();

        auto log_path = json::ExtractStringValue(gs_obj.value(), "log_path");
        if (log_path.has_value()) global_settings_.log_path = log_path.value();
    }

    // Parse recent paths
    auto src = json::ExtractStringValue(json_str, "last_source_dir");
    if (src.has_value()) last_source_dir_ = src.value();

    auto dst = json::ExtractStringValue(json_str, "last_dest_dir");
    if (dst.has_value()) last_dest_dir_ = dst.value();

    // Parse tasks
    auto task_objs = json::ExtractObjectArray(json_str, "backup_tasks");
    tasks_.clear();
    for (const auto& obj : task_objs) {
        BackupTaskConfig task;
        auto id = json::ExtractStringValue(obj, "id");
        if (id.has_value()) task.id = id.value();

        auto name = json::ExtractStringValue(obj, "name");
        if (name.has_value()) task.name = name.value();

        auto sdir = json::ExtractStringValue(obj, "source_dir");
        if (sdir.has_value()) task.source_dir = sdir.value();

        auto ddir = json::ExtractStringValue(obj, "dest_dir");
        if (ddir.has_value()) task.dest_dir = ddir.value();

        // Parse filters
        auto filter_objs = json::ExtractObjectArray(obj, "filters");
        for (const auto& fobj : filter_objs) {
            task.filters.push_back(json::FilterRuleFromJson(fobj));
        }

        if (!task.id.empty()) {
            tasks_.push_back(task);
        }
    }

    return true;
}

} // namespace backup
