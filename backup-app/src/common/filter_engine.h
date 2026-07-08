#pragma once

#include "common/types.h"
#include <vector>
#include <regex>
#include <map>

namespace backup {

/// Stateless filter engine: applies FilterRule list to FileMetadata collection.
///
/// Usage:
///   auto filtered = FilterEngine::Apply(all_files, rules);
///
/// Filter semantics:
///   - Empty rules → all files pass
///   - Include rules present → default-deny: only matching files pass
///   - Exclude rules → remove matching files (takes priority over include)
class FilterEngine {
public:
    /// Apply filter rules to a list of file metadata.
    /// Returns the subset of files that pass through the filter.
    static std::vector<FileMetadata> Apply(
        const std::vector<FileMetadata>& files,
        const std::vector<FilterRule>& rules);

private:
    FilterEngine() = delete;

    /// Check whether a single file matches a single rule.
    /// All non-empty rule conditions are ANDed together.
    static bool MatchesRule(
        const FileMetadata& file,
        const FilterRule& rule,
        const std::map<std::string, std::regex>& compiled_regex);

    // Individual condition matchers
    static bool MatchPathGlob(const std::string& path, const std::string& pattern);
    static bool MatchNameRegex(const std::string& filename, const std::string& pattern,
                               const std::map<std::string, std::regex>& cache);
    static bool MatchFileTypes(FileType type, const std::vector<FileType>& types);
    static bool MatchMtime(int64_t mtime, std::optional<int64_t> after,
                           std::optional<int64_t> before);
};

} // namespace backup
