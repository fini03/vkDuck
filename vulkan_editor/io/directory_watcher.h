#pragma once

#include <chrono>
#include <efsw/efsw.hpp>
#include <filesystem>
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
        const std::filesystem::path& filepath,
        const std::filesystem::path& filename,
        FileAction action
    )>;

    using DirectoryChangeCallback = std::function<void()>;

    explicit DirectoryWatcher(const std::string& name = "DirectoryWatcher");
    ~DirectoryWatcher();

    // Watch a directory with specified file extensions
    void watchDirectory(
        const std::filesystem::path& directory,
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
    const std::filesystem::path& getWatchedDirectory() const {
        return watchDirectory_;
    }

    // efsw::FileWatchListener interface (signature fixed by library)
    void handleFileAction(
        efsw::WatchID watchid,
        const std::string& dir,
        const std::string& filename,
        efsw::Action action,
        std::string oldFilename = ""
    ) override;

private:
    bool shouldProcessFile(const std::filesystem::path& filename) const;
    bool isDebounced(const std::filesystem::path& filepath);

    std::string name_;
    std::filesystem::path watchDirectory_;
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
        std::filesystem::path,
        std::chrono::steady_clock::time_point>
        lastEventTime;
    std::mutex eventMutex;
};
