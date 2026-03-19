#include "model_watcher.h"
#include "vulkan_editor/util/logger.h"
#include <algorithm>
#include <filesystem>

const std::vector<std::string> ModelFileWatcher::modelExtensions = {
    ".gltf", ".glb", ".obj"
};

ModelFileWatcher::ModelFileWatcher()
    : fileWatcher(nullptr)
    , watchID(-1)
    , watching(false)
    , reloadCallback(nullptr)
    , loadingState(LoadingState::Idle)
    , debounceDelayMs(500) {
    fileWatcher = new efsw::FileWatcher();
}

ModelFileWatcher::~ModelFileWatcher() {
    stopWatching();
    delete fileWatcher;
}

void ModelFileWatcher::watchFile(const std::filesystem::path& filePath) {
    // Stop any existing watch first
    stopWatching();

    if (filePath.empty()) {
        Log::warning("ModelFileWatcher", "Cannot watch empty file path");
        return;
    }

    if (!std::filesystem::exists(filePath)) {
        Log::warning("ModelFileWatcher", "File does not exist: {}", filePath);
        return;
    }

    watchedFilePath = filePath;
    watchedFileName = filePath.filename();
    watchDirectory = filePath.parent_path();

    // Add watch on the directory containing the file (efsw requires string)
    std::string watchDirStr = watchDirectory.string();
    // Ensure the directory path ends with a separator for efsw
    if (!watchDirStr.empty() && watchDirStr.back() != '/' && watchDirStr.back() != '\\') {
        watchDirStr += std::filesystem::path::preferred_separator;
    }

    watchID = fileWatcher->addWatch(watchDirStr, this, false);

    if (watchID < 0) {
        Log::error(
            "ModelFileWatcher", "Failed to watch directory: {}",
            watchDirectory
        );
        return;
    }

    // Start watching
    fileWatcher->watch();
    watching = true;

    Log::info("ModelFileWatcher", "Started watching model file: {}", filePath);
}

void ModelFileWatcher::stopWatching() {
    if (!watching) {
        return;
    }

    if (watchID >= 0) {
        fileWatcher->removeWatch(watchID);
        watchID = -1;
    }

    watching = false;
    watchedFilePath.clear();
    watchedFileName.clear();
    watchDirectory.clear();

    Log::info("ModelFileWatcher", "Stopped watching model file");
}

void ModelFileWatcher::setReloadCallback(ReloadCallback callback) {
    reloadCallback = callback;
}

bool ModelFileWatcher::shouldProcessFile(const std::filesystem::path& filename) const {
    // Only process if this is the exact file we're watching
    if (filename != watchedFileName) {
        return false;
    }

    // Check if file has a model extension
    std::string ext = filename.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (const auto& modelExt : modelExtensions) {
        if (ext == modelExt) {
            return true;
        }
    }
    return false;
}

bool ModelFileWatcher::isDebounced(const std::filesystem::path& filepath) {
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

void ModelFileWatcher::handleFileAction(
    efsw::WatchID watchid,
    const std::string& dir,
    const std::string& filename,
    efsw::Action action,
    std::string oldFilename
) {
    std::filesystem::path filenamePath = filename;

    // Only process the specific model file we're watching
    if (!shouldProcessFile(filenamePath)) {
        return;
    }

    std::filesystem::path fullPath = std::filesystem::path(dir) / filename;

    // Filter actions we care about
    switch (action) {
    case efsw::Actions::Add:
        Log::info("ModelFileWatcher", "Model file added: {}", fullPath);
        break;
    case efsw::Actions::Delete:
        Log::warning("ModelFileWatcher", "Model file deleted: {}", fullPath);
        loadingState = LoadingState::Error;
        lastError = "Model file was deleted";
        return; // Don't try to reload a deleted file
    case efsw::Actions::Modified:
        Log::info("ModelFileWatcher", "Model file modified: {}", fullPath);
        break;
    case efsw::Actions::Moved:
        Log::info(
            "ModelFileWatcher", "Model file moved from {} to {}", oldFilename,
            fullPath
        );
        break;
    default:
        return;
    }

    // Check debouncing
    if (isDebounced(fullPath)) {
        Log::debug("ModelFileWatcher", "Debounced event for {}", fullPath);
        return;
    }

    // Process the event
    FileEvent event;
    event.timestamp = std::chrono::steady_clock::now();
    event.filepath = fullPath;
    event.action = action;

    processEvent(event);
}

void ModelFileWatcher::processEvent(const FileEvent& event) {
    if (!reloadCallback) {
        Log::warning("ModelFileWatcher", "No reload callback set");
        return;
    }

    // Update loading state
    loadingState = LoadingState::Loading;
    lastError.clear();

    // Call the reload callback
    try {
        Log::info(
            "ModelFileWatcher", "Triggering model reload for: {}", event.filepath
        );
        reloadCallback(event.filepath);
        loadingState = LoadingState::Loaded;
    } catch (const std::exception& e) {
        Log::error(
            "ModelFileWatcher", "Error reloading model: {}", e.what()
        );
        loadingState = LoadingState::Error;
        lastError = e.what();
    }
}
