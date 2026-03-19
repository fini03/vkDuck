// shader_file_watcher.cpp
#include "shader_watcher.h"
#include "../util/logger.h"
#include <algorithm>

const std::vector<std::string> ShaderFileWatcher::shaderExtensions = {
    ".slang"
};

ShaderFileWatcher::ShaderFileWatcher(const std::filesystem::path& watchDirectory)
    : watchDirectory(watchDirectory)
    , fileWatcher(nullptr)
    , watchID(-1)
    , watching(false)
    , debounceDelayMs(500)
    , reloadCallback(nullptr) {
    fileWatcher = new efsw::FileWatcher();
}

ShaderFileWatcher::~ShaderFileWatcher() {
    stop();
    delete fileWatcher;
}

void ShaderFileWatcher::start() {
    if (watching) {
        Log::debug("FileWatcher", "Already watching");
        return;
    }

    // Add watch directory (efsw requires string)
    watchID = fileWatcher->addWatch(watchDirectory.string(), this, true);

    if (watchID < 0) {
        Log::error(
            "FileWatcher", "Failed to watch directory: {}",
            watchDirectory
        );
        return;
    }

    // Start watching
    fileWatcher->watch();
    watching = true;

    Log::info("FileWatcher", "Watching directory: {}", watchDirectory);
}

void ShaderFileWatcher::stop() {
    if (!watching) {
        return;
    }

    if (watchID >= 0) {
        fileWatcher->removeWatch(watchID);
        watchID = -1;
    }

    watching = false;
    Log::info("FileWatcher", "Stopped watching");
}

void ShaderFileWatcher::setReloadCallback(ReloadCallback callback) {
    reloadCallback = callback;
}

bool ShaderFileWatcher::shouldProcessFile(
    const std::filesystem::path& filename
) const {
    // Check if file has a shader extension
    auto ext = filename.extension().string();
    for (const auto& shaderExt : shaderExtensions) {
        if (ext == shaderExt) {
            return true;
        }
    }
    return false;
}

bool ShaderFileWatcher::isDebounced(const std::filesystem::path& filepath) {
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
            // Too soon, debounce this event
            return true;
        }
    }

    // Update the last event time
    lastEventTime[filepath] = now;
    return false;
}

void ShaderFileWatcher::handleFileAction(
    efsw::WatchID watchid,
    const std::string& dir,
    const std::string& filename,
    efsw::Action action,
    std::string oldFilename
) {
    // Only process shader files
    std::filesystem::path filenamePath = filename;
    if (!shouldProcessFile(filenamePath)) {
        return;
    }

    std::filesystem::path fullPath = std::filesystem::path(dir) / filename;

    // Filter actions we care about
    switch (action) {
    case efsw::Actions::Add:
        Log::debug("FileWatcher", "File added: {}", fullPath);
        break;
    case efsw::Actions::Delete:
        Log::debug("FileWatcher", "File deleted: {}", fullPath);
        break;
    case efsw::Actions::Modified:
        Log::debug("FileWatcher", "File modified: {}", fullPath);
        break;
    case efsw::Actions::Moved:
        Log::debug(
            "FileWatcher", "File moved from {} to {}", oldFilename,
            fullPath
        );
        fullPath = std::filesystem::path(dir) / oldFilename;
        break;
    default:
        return;
    }

    // Check debouncing
    if (isDebounced(fullPath)) {
        Log::debug("FileWatcher", "Debounced event for {}", fullPath);
        return;
    }

    // Process the event
    FileEvent event;
    event.timestamp = std::chrono::steady_clock::now();
    event.filepath = fullPath;
    event.action = action;

    processEvent(event);
}

void ShaderFileWatcher::processEvent(const FileEvent& event) {
    if (!reloadCallback) {
        Log::warning("FileWatcher", "No reload callback set");
        return;
    }

    // Call the reload callback
    try {
        Log::debug(
            "FileWatcher", "Triggering reload for: {}", event.filepath
        );
        reloadCallback(event.filepath);
    } catch (const std::exception& e) {
        Log::error(
            "FileWatcher", "Error in reload callback: {}", e.what()
        );
    }
}