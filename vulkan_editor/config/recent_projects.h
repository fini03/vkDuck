#pragma once

#include <filesystem>
#include <string>
#include <vector>

/**
 * @class RecentProjects
 * @brief Manages a list of recently opened project folders.
 *
 * Persists the list to ~/.vkduck_recent_projects.json
 * and provides access to the last N opened projects.
 */
class RecentProjects {
public:
    static constexpr size_t MAX_RECENT = 3;

    RecentProjects();

    // Add a project to the recent list (moves it to front if already present)
    void addProject(const std::filesystem::path& projectPath);

    // Get the list of recent projects (most recent first)
    const std::vector<std::filesystem::path>& getRecent() const { return recentPaths_; }

    // Check if there are any recent projects
    bool hasRecent() const { return !recentPaths_.empty(); }

private:
    void load();
    void save() const;
    std::filesystem::path getConfigPath() const;

    std::vector<std::filesystem::path> recentPaths_;
};
