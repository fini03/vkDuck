#pragma once

#include <chrono>
#include <efsw/efsw.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class DirectoryWatcher : public efsw::FileWatchListener {
public:
    enum class FileAction {
        Added,
        Deleted,
        Modified,
        Moved
    };

    using FileChangeCallback = std::function<void(
        const std::string& filepath,
        const std::string& filename,
        FileAction action
    )>;

    using DirectoryChangeCallback = std::function<void()>;

    explicit DirectoryWatcher(const std::string& name = "DirectoryWatcher");
    ~DirectoryWatcher();

    // Watch a directory with specified file extensions
    void watchDirectory(
        const std::string& directory,
        const std::vector<std::string>& extensions,
        bool recursive = true
    );

    // Stop watching
    void stopWatching();

    bool isWatching() const {
        return watching;
    }

    // Set callback for individual file changes
    void setFileChangeCallback(FileChangeCallback callback);

    // Set callback for any directory change (triggers rescan)
    void setDirectoryChangeCallback(DirectoryChangeCallback callback);

    // Configure debouncing
    void setDebounceDelay(int milliseconds) {
        debounceDelayMs = milliseconds;
    }

    // Get the directory being watched
    const std::string& getWatchedDirectory() const {
        return watchDirectory_;
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
    bool shouldProcessFile(const std::string& filename) const;
    bool isDebounced(const std::string& filepath);

    std::string name_;
    std::string watchDirectory_;
    std::vector<std::string> watchExtensions_;
    bool watchRecursive_;

    efsw::FileWatcher* fileWatcher;
    efsw::WatchID watchID;
    bool watching;

    FileChangeCallback fileChangeCallback;
    DirectoryChangeCallback directoryChangeCallback;

    // Debouncing
    int debounceDelayMs;
    std::unordered_map<
        std::string,
        std::chrono::steady_clock::time_point>
        lastEventTime;
    std::mutex eventMutex;
};
