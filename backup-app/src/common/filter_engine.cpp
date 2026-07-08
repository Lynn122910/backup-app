#include "common/filter_engine.h"
#include "common/file_utils.h"
#include "common/logger.h"

#include <fnmatch.h>

namespace backup {

std::vector<FileMetadata> FilterEngine::Apply(
    const std::vector<FileMetadata>& files,
    const std::vector<FilterRule>& rules) {

    if (rules.empty()) return files;

    // Split rules by op
    std::vector<const FilterRule*> include_rules, exclude_rules;
    for (const auto& r : rules) {
        if (r.op == FilterOp::kInclude) include_rules.push_back(&r);
        else                            exclude_rules.push_back(&r);
    }

    // Pre-compile all name_regex patterns
    std::map<std::string, std::regex> compiled_regex;
    for (const auto& r : rules) {
        if (r.name_regex.has_value()) {
            const auto& pattern = r.name_regex.value();
            if (compiled_regex.find(pattern) == compiled_regex.end()) {
                try {
                    compiled_regex.emplace(pattern, std::regex(pattern));
                } catch (const std::regex_error& e) {
                    LOG_WARNING << "Invalid regex pattern '" << pattern
                                << "': " << e.what();
                    // Insert a never-match regex as placeholder
                    compiled_regex.emplace(pattern, std::regex("$."));
                }
            }
        }
    }

    std::vector<FileMetadata> result;
    result.reserve(files.size());

    for (const auto& file : files) {
        // Step A: Include gate (default-deny if any include rules exist)
        bool included = include_rules.empty();
        if (!included) {
            for (const auto* r : include_rules) {
                if (MatchesRule(file, *r, compiled_regex)) {
                    included = true;
                    break;
                }
            }
        }
        if (!included) continue;

        // Step B: Exclude check (overrides include)
        bool excluded = false;
        for (const auto* r : exclude_rules) {
            if (MatchesRule(file, *r, compiled_regex)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        result.push_back(file);
    }

    return result;
}

bool FilterEngine::MatchesRule(
    const FileMetadata& file,
    const FilterRule& rule,
    const std::map<std::string, std::regex>& compiled_regex) {

    // path_glob
    if (rule.path_glob.has_value()) {
        if (!MatchPathGlob(file.path, rule.path_glob.value()))
            return false;
    }

    // name_regex
    if (rule.name_regex.has_value()) {
        std::string filename = FileUtils::GetFileName(file.path);
        if (!MatchNameRegex(filename, rule.name_regex.value(), compiled_regex))
            return false;
    }

    // file_types — skip for directories (type filtering on dirs rarely useful)
    if (rule.file_types.has_value() && file.type != FileType::kDirectory) {
        if (!MatchFileTypes(file.type, rule.file_types.value()))
            return false;
    }

    // mtime range — skip for directories
    if (file.type != FileType::kDirectory) {
        if (!MatchMtime(file.mtime_nsec, rule.mtime_after, rule.mtime_before))
            return false;
    }

    // min_size / max_size — skip for directories and non-regular files
    if (file.type == FileType::kRegular) {
        if (rule.min_size.has_value() && file.size < rule.min_size.value())
            return false;
        if (rule.max_size.has_value() && file.size > rule.max_size.value())
            return false;
    }

    // owner
    if (rule.owner.has_value()) {
        if (file.owner != rule.owner.value())
            return false;
    }

    // group
    if (rule.group.has_value()) {
        if (file.group != rule.group.value())
            return false;
    }

    return true;
}

bool FilterEngine::MatchPathGlob(const std::string& path, const std::string& pattern) {
    return fnmatch(pattern.c_str(), path.c_str(), FNM_PATHNAME) == 0;
}

bool FilterEngine::MatchNameRegex(
    const std::string& filename,
    const std::string& pattern,
    const std::map<std::string, std::regex>& cache) {

    auto it = cache.find(pattern);
    if (it == cache.end()) return false;
    return std::regex_search(filename, it->second);
}

bool FilterEngine::MatchFileTypes(FileType type, const std::vector<FileType>& types) {
    for (auto t : types) {
        if (t == type) return true;
    }
    return false;
}

bool FilterEngine::MatchMtime(int64_t mtime,
                               std::optional<int64_t> after,
                               std::optional<int64_t> before) {
    if (after.has_value() && mtime < after.value()) return false;
    if (before.has_value() && mtime > before.value()) return false;
    return true;
}

} // namespace backup
