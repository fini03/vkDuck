#pragma once

#include <chrono>
#include <efsw/efsw.hpp>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class ShaderManager;

/**
 * @class ShaderFileWatcher
 * @brief Monitors shader files for changes and triggers hot-reload callbacks.
 *
 * Uses efsw for cross-platform file system monitoring with debouncing
 * to prevent multiple rapid reloads from a single save operation.
 */
class ShaderFileWatcher : public efsw::FileWatchListener {
public:
    using ReloadCallback = std::function<void(const std::filesystem::path& filepath)>;

    explicit ShaderFileWatcher(const std::filesystem::path& watchDirectory);
    ~ShaderFileWatcher();

    void start();
    void stop();
    bool isWatching() const { return watching; }
    void setReloadCallback(ReloadCallback callback);
    void setDebounceDelay(int milliseconds) { debounceDelayMs = milliseconds; }

    // Note: handleFileAction signature is fixed by efsw library interface
    void handleFileAction(
        efsw::WatchID watchid,
        const std::string& dir,
        const std::string& filename,
        efsw::Action action,
        std::string oldFilename = ""
    ) override;

private:
    struct FileEvent {
        std::chrono::steady_clock::time_point timestamp;
        std::filesystem::path filepath;
        efsw::Action action;
    };

    bool shouldProcessFile(const std::filesystem::path& filename) const;
    void processEvent(const FileEvent& event);
    bool isDebounced(const std::filesystem::path& filepath);

    std::filesystem::path watchDirectory;
    efsw::FileWatcher* fileWatcher;
    efsw::WatchID watchID;
    bool watching;

    ReloadCallback reloadCallback;

    int debounceDelayMs;
    std::unordered_map<std::filesystem::path, std::chrono::steady_clock::time_point> lastEventTime;
    std::mutex eventMutex;

    static const std::vector<std::string> shaderExtensions;
};