#include "recent_projects.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

RecentProjects::RecentProjects() {
    load();
}

std::filesystem::path RecentProjects::getConfigPath() const {
    std::filesystem::path configDir;

#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        configDir = std::filesystem::path(appData) / "vkDuck";
    } else {
        configDir = std::filesystem::path(".") / ".vkduck";
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        configDir = std::filesystem::path(home);
    } else {
        configDir = std::filesystem::path(".");
    }
#endif

    return configDir / ".vkduck_recent_projects.json";
}

void RecentProjects::load() {
    recentPaths_.clear();

    std::filesystem::path configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) {
        return;
    }

    try {
        std::ifstream file(configPath);
        if (!file.is_open()) return;

        nlohmann::json j;
        file >> j;

        if (j.contains("recent") && j["recent"].is_array()) {
            for (const auto& item : j["recent"]) {
                if (item.is_string()) {
                    std::filesystem::path p = item.get<std::string>();
                    // Only add if directory still exists
                    if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                        recentPaths_.push_back(p);
                    }
                }
            }
        }
    } catch (...) {
        // Silently ignore parse errors
        recentPaths_.clear();
    }
}

void RecentProjects::save() const {
    std::filesystem::path configPath = getConfigPath();

    // Ensure parent directory exists
    std::filesystem::path parentDir = configPath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::filesystem::create_directories(parentDir);
    }

    try {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : recentPaths_) {
            arr.push_back(p.string());
        }
        j["recent"] = arr;

        std::ofstream file(configPath);
        if (file.is_open()) {
            file << j.dump(2);
        }
    } catch (...) {
        // Silently ignore write errors
    }
}

void RecentProjects::addProject(const std::filesystem::path& projectPath) {
    // Normalize the path
    std::filesystem::path normalized = std::filesystem::weakly_canonical(projectPath);

    // Remove if already in list (will be re-added at front)
    auto it = std::find(recentPaths_.begin(), recentPaths_.end(), normalized);
    if (it != recentPaths_.end()) {
        recentPaths_.erase(it);
    }

    // Add to front
    recentPaths_.insert(recentPaths_.begin(), normalized);

    // Trim to max size
    if (recentPaths_.size() > MAX_RECENT) {
        recentPaths_.resize(MAX_RECENT);
    }

    save();
}
