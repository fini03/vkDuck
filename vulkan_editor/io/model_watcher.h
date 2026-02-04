#pragma once

#include <chrono>
#include <efsw/efsw.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class ModelFileWatcher : public efsw::FileWatchListener {
public:
    enum class LoadingState {
        Idle,
        Loading,
        Loaded,
        Error
    };

    using ReloadCallback =
        std::function<void(const std::string& filepath)>;

    explicit ModelFileWatcher();
    ~ModelFileWatcher();

    // Watch a specific model file
    void watchFile(const std::string& filePath);

    // Stop watching current file
    void stopWatching();

    bool isWatching() const {
        return watching;
    }

    // Set callback for when the model file changes
    void setReloadCallback(ReloadCallback callback);

    // Configure debouncing
    void setDebounceDelay(int milliseconds) {
        debounceDelayMs = milliseconds;
    }

    // Loading state management
    LoadingState getLoadingState() const {
        return loadingState;
    }
    void setLoadingState(LoadingState state) {
        loadingState = state;
    }
    const std::string& getLastError() const {
        return lastError;
    }
    void setLastError(const std::string& error) {
        lastError = error;
    }

    // Get the file being watched
    const std::string& getWatchedFile() const {
        return watchedFilePath;
    }

    // efsw::FileWatchListener interface
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
        std::string filepath;
        efsw::Action action;
    };

    bool shouldProcessFile(const std::string& filename) const;
    void processEvent(const FileEvent& event);
    bool isDebounced(const std::string& filepath);

    std::string watchedFilePath;
    std::string watchedFileName;
    std::string watchDirectory;
    efsw::FileWatcher* fileWatcher;
    efsw::WatchID watchID;
    bool watching;

    ReloadCallback reloadCallback;

    // Loading state
    LoadingState loadingState;
    std::string lastError;

    // Debouncing to avoid multiple rapid reloads
    int debounceDelayMs;
    std::unordered_map<
        std::string,
        std::chrono::steady_clock::time_point>
        lastEventTime;
    std::mutex eventMutex;

    // Supported model extensions
    static const std::vector<std::string> modelExtensions;
};
