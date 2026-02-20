#include "directory_watcher.h"
#include "../util/logger.h"
#include <algorithm>
#include <filesystem>

DirectoryWatcher::DirectoryWatcher(const std::string& name)
    : name_(name)
    , watchRecursive_(true)
    , fileWatcher(nullptr)
    , watchID(-1)
    , watching(false)
    , fileChangeCallback(nullptr)
    , directoryChangeCallback(nullptr)
    , debounceDelayMs(500) {
    fileWatcher = new efsw::FileWatcher();
}

DirectoryWatcher::~DirectoryWatcher() {
    stopWatching();
    delete fileWatcher;
}

void DirectoryWatcher::watchDirectory(
    const std::string& directory,
    const std::vector<std::string>& extensions,
    bool recursive
) {
    // Stop any existing watch first
    stopWatching();

    if (directory.empty()) {
        Log::warning(name_, "Cannot watch empty directory path");
        return;
    }

    if (!std::filesystem::exists(directory)) {
        Log::warning(name_, "Directory does not exist: {}", directory);
        return;
    }

    watchDirectory_ = directory;
    watchExtensions_ = extensions;
    watchRecursive_ = recursive;

    // Add watch on the directory
    watchID = fileWatcher->addWatch(watchDirectory_, this, watchRecursive_);

    if (watchID < 0) {
        Log::error(name_, "Failed to watch directory: {}", watchDirectory_);
        return;
    }

    // Start watching
    fileWatcher->watch();
    watching = true;

    Log::info(name_, "Started watching directory: {}", watchDirectory_);
}

void DirectoryWatcher::stopWatching() {
    if (!watching) {
        return;
    }

    if (watchID >= 0) {
        fileWatcher->removeWatch(watchID);
        watchID = -1;
    }

    watching = false;
    watchDirectory_.clear();
    watchExtensions_.clear();

    Log::info(name_, "Stopped watching directory");
}

void DirectoryWatcher::setFileChangeCallback(FileChangeCallback callback) {
    fileChangeCallback = callback;
}

void DirectoryWatcher::setDirectoryChangeCallback(DirectoryChangeCallback callback) {
    directoryChangeCallback = callback;
}

bool DirectoryWatcher::shouldProcessFile(const std::string& filename) const {
    if (watchExtensions_.empty()) {
        return true; // No filter, process all files
    }

    // Get file extension (lowercase)
    std::string ext;
    size_t dotPos = filename.rfind('.');
    if (dotPos != std::string::npos) {
        ext = filename.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Check against allowed extensions
    for (const auto& allowedExt : watchExtensions_) {
        std::string lowerAllowed = allowedExt;
        std::transform(lowerAllowed.begin(), lowerAllowed.end(), lowerAllowed.begin(), ::tolower);
        if (ext == lowerAllowed) {
            return true;
        }
    }

    return false;
}

bool DirectoryWatcher::isDebounced(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(eventMutex);

    auto now = std::chrono::steady_clock::now();
    auto it = lastEventTime.find(filepath);

    if (it != lastEventTime.end()) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second
            )
                .count();

        if (elapsed < debounceDelayMs) {
            return true;
        }
    }

    lastEventTime[filepath] = now;
    return false;
}

void DirectoryWatcher::handleFileAction(
    efsw::WatchID watchid,
    const std::string& dir,
    const std::string& filename,
    efsw::Action action,
    std::string oldFilename
) {
    // Only process files with matching extensions
    if (!shouldProcessFile(filename)) {
        return;
    }

    std::string fullPath = (std::filesystem::path(dir) / filename).string();

    // Convert efsw action to our FileAction enum
    FileAction fileAction;
    switch (action) {
    case efsw::Actions::Add:
        fileAction = FileAction::Added;
        Log::info(name_, "File added: {}", fullPath);
        break;
    case efsw::Actions::Delete:
        fileAction = FileAction::Deleted;
        Log::info(name_, "File deleted: {}", fullPath);
        break;
    case efsw::Actions::Modified:
        fileAction = FileAction::Modified;
        Log::info(name_, "File modified: {}", fullPath);
        break;
    case efsw::Actions::Moved:
        fileAction = FileAction::Moved;
        Log::info(name_, "File moved from {} to {}", oldFilename, fullPath);
        break;
    default:
        return;
    }

    // Check debouncing
    if (isDebounced(fullPath)) {
        Log::debug(name_, "Debounced event for {}", fullPath);
        return;
    }

    // Call the file change callback if set
    if (fileChangeCallback) {
        try {
            fileChangeCallback(fullPath, filename, fileAction);
        } catch (const std::exception& e) {
            Log::error(name_, "Error in file change callback: {}", e.what());
        }
    }

    // Call the directory change callback if set (for triggering rescan)
    if (directoryChangeCallback) {
        try {
            directoryChangeCallback();
        } catch (const std::exception& e) {
            Log::error(name_, "Error in directory change callback: {}", e.what());
        }
    }
}
