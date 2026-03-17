#include "model_manager.h"
#include "vulkan_editor/util/logger.h"
#include <algorithm>
#include <future>

// Use vkDuck's shared implementations
#include <vkDuck/image_loader.h>
#include <vkDuck/model_loader.h>

namespace fs = std::filesystem;

// EditorImage destructor
EditorImage::~EditorImage() {
    if (pixels)
        imageFree(pixels);
}

namespace {
constexpr const char* LOG_CATEGORY = "ModelManager";

// Supported model extensions
const std::vector<std::string> MODEL_EXTENSIONS = {".gltf", ".glb", ".obj"};

bool hasModelExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(MODEL_EXTENSIONS.begin(), MODEL_EXTENSIONS.end(), ext) !=
           MODEL_EXTENSIONS.end();
}

std::string getDisplayName(const fs::path& path) {
    return path.stem().string();
}

// Parallel image loading result
struct DecodedImageResult {
    void* pixels{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    size_t index{0};
    bool success{false};
};

DecodedImageResult loadSingleImage(const fs::path& path, size_t index) {
    DecodedImageResult result;
    result.index = index;
    result.pixels = imageLoad(path, result.width, result.height);
    result.success = (result.pixels != nullptr);
    return result;
}

std::vector<DecodedImageResult>
loadImagesParallel(const std::vector<std::pair<size_t, fs::path>>& imagesToLoad) {
    std::vector<std::future<DecodedImageResult>> futures;
    futures.reserve(imagesToLoad.size());

    for (const auto& [index, path] : imagesToLoad) {
        futures.push_back(std::async(std::launch::async, loadSingleImage, path, index));
    }

    std::vector<DecodedImageResult> results;
    results.reserve(futures.size());
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    return results;
}

}  // namespace

// Global instance pointer - initialized in editor.cpp
ModelManager* g_modelManager = nullptr;

ModelManager::ModelManager() {
    Log::info(LOG_CATEGORY, "ModelManager initialized");
}

ModelManager::~ModelManager() {
    // Clear global pointer first to prevent use-after-free
    g_modelManager = nullptr;
    clearCache(true);
    Log::info(LOG_CATEGORY, "ModelManager destroyed");
}

void ModelManager::setProjectRoot(const fs::path& root) {
    std::lock_guard lock(mutex_);

    if (projectRoot_ == root) {
        return;
    }

    projectRoot_ = root;
    Log::info(LOG_CATEGORY, "Project root set to: {}", root.string());

    // Clear cache when project changes
    cache_.clear();
    pathToHandle_.clear();
    availableModels_.clear();
}

void ModelManager::scanModels() {
    std::lock_guard lock(mutex_);

    availableModels_.clear();

    if (projectRoot_.empty()) {
        Log::warning(LOG_CATEGORY, "Cannot scan models: project root not set");
        return;
    }

    fs::path modelsDir = projectRoot_ / "data" / "models";
    if (!fs::exists(modelsDir)) {
        Log::info(LOG_CATEGORY, "Models directory does not exist: {}", modelsDir.string());
        return;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(modelsDir)) {
            if (entry.is_regular_file() && hasModelExtension(entry.path())) {
                // Store relative path from project root
                fs::path relativePath = fs::relative(entry.path(), projectRoot_);
                availableModels_.push_back(relativePath);
            }
        }

        std::sort(availableModels_.begin(), availableModels_.end());
        Log::info(LOG_CATEGORY, "Found {} models in project", availableModels_.size());

        // Clean up stale cache entries (models that no longer exist on disk)
        cleanupStaleCacheEntries();

        // Setup directory watcher if not already active
        setupDirectoryWatcher();

    } catch (const std::exception& e) {
        Log::error(LOG_CATEGORY, "Error scanning models directory: {}", e.what());
    }
}

std::vector<fs::path> ModelManager::getAvailableModels() const {
    std::lock_guard lock(mutex_);
    return availableModels_;
}

void ModelManager::setupDirectoryWatcher() {
    if (projectRoot_.empty()) {
        return;
    }

    fs::path modelsDir = projectRoot_ / "data" / "models";
    if (!fs::exists(modelsDir)) {
        return;
    }

    // Create watcher if it doesn't exist
    if (!directoryWatcher_) {
        directoryWatcher_ = std::make_unique<DirectoryWatcher>("ModelsDirWatcher");
    }

    // Don't restart if already watching the same directory
    if (directoryWatcher_->isWatching() &&
        directoryWatcher_->getWatchedDirectory() == modelsDir.string()) {
        return;
    }

    // Set up callbacks
    directoryWatcher_->setFileChangeCallback(
        [this](const std::string& filepath, const std::string& filename, DirectoryWatcher::FileAction action) {
            handleDirectoryChange(filepath, filename, action);
        }
    );

    directoryWatcher_->setDirectoryChangeCallback([this]() {
        pendingRescan_ = true;
    });

    // Start watching with model file extensions
    directoryWatcher_->watchDirectory(
        modelsDir.string(),
        {".gltf", ".glb", ".obj"},
        true  // recursive
    );

    Log::info(LOG_CATEGORY, "Started watching models directory: {}", modelsDir.string());
}

void ModelManager::handleDirectoryChange(
    const std::string& filepath,
    const std::string& filename,
    DirectoryWatcher::FileAction action
) {
    // Convert absolute path to relative path
    fs::path absolutePath(filepath);
    fs::path relativePath;
    try {
        relativePath = fs::relative(absolutePath, projectRoot_);
    } catch (const std::exception& e) {
        Log::warning(LOG_CATEGORY, "Failed to get relative path for {}: {}", filepath, e.what());
        return;
    }

    std::string pathKey = relativePath.string();

    switch (action) {
        case DirectoryWatcher::FileAction::Added:
            Log::info(LOG_CATEGORY, "Model file added: {}", filename);
            pendingRescan_ = true;
            break;

        case DirectoryWatcher::FileAction::Deleted: {
            Log::info(LOG_CATEGORY, "Model file deleted: {}", filename);
            // Mark for unload if it was cached
            std::lock_guard lock(mutex_);
            auto it = pathToHandle_.find(pathKey);
            if (it != pathToHandle_.end()) {
                ModelHandle handle = it->second;
                if (auto cacheIt = cache_.find(handle); cacheIt != cache_.end()) {
                    CachedModel* model = cacheIt->second.get();
                    model->status = ModelStatus::Error;
                    model->errorMessage = "File was deleted";
                    Log::warning(LOG_CATEGORY, "Cached model '{}' was deleted from disk", model->displayName);
                }
            }
            pendingRescan_ = true;
            break;
        }

        case DirectoryWatcher::FileAction::Modified: {
            Log::info(LOG_CATEGORY, "Model file modified: {}", filename);
            // Mark for reload if it was cached
            std::lock_guard lock(mutex_);
            auto it = pathToHandle_.find(pathKey);
            if (it != pathToHandle_.end()) {
                ModelHandle handle = it->second;
                if (auto cacheIt = cache_.find(handle); cacheIt != cache_.end()) {
                    CachedModel* model = cacheIt->second.get();
                    if (model->autoReload) {
                        model->pendingReload = true;
                        Log::info(LOG_CATEGORY, "Marking model '{}' for reload", model->displayName);
                    }
                }
            }
            break;
        }

        case DirectoryWatcher::FileAction::Moved:
            Log::info(LOG_CATEGORY, "Model file moved: {}", filename);
            pendingRescan_ = true;
            break;
    }
}

void ModelManager::cleanupStaleCacheEntries() {
    // Must be called with mutex_ already held
    std::vector<ModelHandle> toRemove;

    for (const auto& [handle, model] : cache_) {
        fs::path absolutePath = projectRoot_ / model->path;
        if (!fs::exists(absolutePath)) {
            // File no longer exists
            if (model->referenceCount == 0) {
                toRemove.push_back(handle);
                Log::info(LOG_CATEGORY, "Removing stale cache entry: {}", model->displayName);
            } else {
                // Mark as error but don't remove (still referenced)
                model->status = ModelStatus::Error;
                model->errorMessage = "File no longer exists";
                Log::warning(
                    LOG_CATEGORY,
                    "Model '{}' no longer exists but has {} references",
                    model->displayName,
                    model->referenceCount
                );
            }
        }
    }

    for (ModelHandle handle : toRemove) {
        auto it = cache_.find(handle);
        if (it != cache_.end()) {
            pathToHandle_.erase(it->second->path.string());
            cache_.erase(it);
        }
    }

    if (!toRemove.empty()) {
        Log::info(LOG_CATEGORY, "Cleaned up {} stale cache entries", toRemove.size());
    }
}

ModelHandle ModelManager::findOrCreateHandle(const fs::path& relativePath) {
    std::string pathKey = relativePath.string();

    auto it = pathToHandle_.find(pathKey);
    if (it != pathToHandle_.end()) {
        return it->second;
    }

    // Create new handle
    ModelHandle handle{nextHandleId_++};
    pathToHandle_[pathKey] = handle;

    // Create cache entry
    auto model = std::make_unique<CachedModel>();
    model->handle = handle;
    model->path = relativePath;
    model->displayName = getDisplayName(relativePath);
    model->status = ModelStatus::NotLoaded;

    cache_[handle] = std::move(model);

    return handle;
}

ModelHandle ModelManager::loadModel(const fs::path& relativePath) {
    std::lock_guard lock(mutex_);

    ModelHandle handle = findOrCreateHandle(relativePath);
    CachedModel* model = cache_[handle].get();

    if (model->status == ModelStatus::Loaded) {
        model->lastAccessed = std::chrono::system_clock::now();
        Log::debug(LOG_CATEGORY, "Model already loaded: {}", relativePath.string());
        return handle;
    }

    if (model->status == ModelStatus::Loading) {
        Log::debug(LOG_CATEGORY, "Model is currently loading: {}", relativePath.string());
        return handle;
    }

    model->status = ModelStatus::Loading;
    Log::info(LOG_CATEGORY, "Loading model: {}", relativePath.string());

    if (loadModelInternal(*model)) {
        model->status = ModelStatus::Loaded;
        model->loadedAt = std::chrono::system_clock::now();
        model->lastAccessed = model->loadedAt;
        model->errorMessage.clear();

        calculateMemoryUsage(*model);
        setupFileWatcher(*model);

        Log::info(
            LOG_CATEGORY,
            "Loaded model '{}': {} vertices, {} indices, {} geometries, {} cameras, {} lights",
            model->displayName,
            model->modelData.getTotalVertexCount(),
            model->modelData.getTotalIndexCount(),
            model->modelData.getGeometryCount(),
            model->cameras.size(),
            model->lights.size()
        );
    } else {
        model->status = ModelStatus::Error;
        Log::error(LOG_CATEGORY, "Failed to load model: {}", relativePath.string());
    }

    return handle;
}

ModelHandle ModelManager::loadModelAsync(
    const fs::path& relativePath,
    ModelLoadCallback callback
) {
    // For simplicity, we do synchronous loading here.
    // A full async implementation would use a worker thread pool.
    ModelHandle handle = loadModel(relativePath);

    if (callback) {
        bool success = isLoaded(handle);
        callback(handle, success);
    }

    return handle;
}

bool ModelManager::loadModelInternal(CachedModel& model) {
    auto totalStart = std::chrono::high_resolution_clock::now();

    fs::path absolutePath = projectRoot_ / model.path;

    if (!fs::exists(absolutePath)) {
        model.errorMessage = "File not found: " + absolutePath.string();
        Log::error(LOG_CATEGORY, "{}", model.errorMessage);
        return false;
    }

    // Clear existing data
    model.modelData.clear();
    model.materials.clear();
    model.images.clear();
    model.cameras.clear();
    model.lights.clear();

    // Load default texture
    fs::path defaultTexPath = projectRoot_ / "data" / "images" / "default.png";
    model.defaultTexture.path = defaultTexPath;
    model.defaultTexture.pixels = imageLoad(defaultTexPath, model.defaultTexture.width, model.defaultTexture.height);

    if (!model.defaultTexture.pixels) {
        Log::warning(LOG_CATEGORY, "Failed to load default texture: {}", defaultTexPath.string());
    }

    // Use vkDuck library's loadModel
    ModelData libModelData = ::loadModel(absolutePath.string(), projectRoot_.string());

    if (libModelData.vertices.empty()) {
        model.errorMessage = "Model is empty or failed to parse";
        Log::error(LOG_CATEGORY, "{}", model.errorMessage);
        return false;
    }

    // Copy consolidated geometry data
    model.modelData.vertices = std::move(libModelData.vertices);
    model.modelData.indices = std::move(libModelData.indices);

    // Convert GeometryRange to EditorGeometryRange
    model.modelData.ranges.reserve(libModelData.ranges.size());
    for (const auto& range : libModelData.ranges) {
        EditorGeometryRange editorRange;
        editorRange.firstVertex = range.firstVertex;
        editorRange.vertexCount = range.vertexCount;
        editorRange.firstIndex = range.firstIndex;
        editorRange.indexCount = range.indexCount;
        editorRange.materialIndex = range.materialIndex;
        editorRange.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // Default
        model.modelData.ranges.push_back(editorRange);
    }

    // Copy cameras and lights
    model.cameras = std::move(libModelData.cameras);
    model.lights = std::move(libModelData.lights);

    if (!model.cameras.empty()) {
        Log::info(LOG_CATEGORY, "Found {} camera(s) in model", model.cameras.size());
    }

    if (!model.lights.empty()) {
        Log::info(LOG_CATEGORY, "Found {} light(s) in model", model.lights.size());
    }

    // Set up images from all unique texture paths
    model.images.resize(libModelData.allTexturePaths.size());
    for (size_t i = 0; i < libModelData.allTexturePaths.size(); ++i) {
        const auto& texPath = libModelData.allTexturePaths[i];
        if (!texPath.empty()) {
            model.images[i].path = texPath;
            model.images[i].toLoad = true;
        }
    }

    // Set up materials with all PBR texture indices and factors
    model.materials.resize(libModelData.materials.size());
    for (size_t i = 0; i < libModelData.materials.size(); ++i) {
        const auto& srcMat = libModelData.materials[i];
        EditorMaterial& dstMat = model.materials[i];
        dstMat.baseColorTextureIndex = srcMat.baseColorTextureIndex;
        dstMat.emissiveTextureIndex = srcMat.emissiveTextureIndex;
        dstMat.metallicRoughnessTextureIndex = srcMat.metallicRoughnessTextureIndex;
        dstMat.normalTextureIndex = srcMat.normalTextureIndex;
        dstMat.baseColorFactor = srcMat.baseColorFactor;
        dstMat.emissiveFactor = srcMat.emissiveFactor;
        dstMat.metallicFactor = srcMat.metallicFactor;
        dstMat.roughnessFactor = srcMat.roughnessFactor;
    }

    // Load textures in parallel
    {
        auto t1 = std::chrono::high_resolution_clock::now();

        std::vector<std::pair<size_t, fs::path>> imagesToLoad;
        for (size_t i = 0; i < model.images.size(); ++i) {
            if (model.images[i].toLoad) {
                imagesToLoad.push_back({i, model.images[i].path});
            }
        }

        if (!imagesToLoad.empty()) {
            Log::debug(LOG_CATEGORY, "Loading {} images in parallel...", imagesToLoad.size());
            auto results = loadImagesParallel(imagesToLoad);

            for (const auto& result : results) {
                model.images[result.index].pixels = result.pixels;
                model.images[result.index].width = result.width;
                model.images[result.index].height = result.height;

                if (!result.success) {
                    Log::warning(
                        LOG_CATEGORY,
                        "Failed to load texture: {}, will use default",
                        model.images[result.index].path.string()
                    );
                }
            }
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        Log::debug(LOG_CATEGORY, "Image loading took {:.1f}ms", ms);
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    auto totalMs = std::chrono::duration<double, std::milli>(totalEnd - totalStart).count();
    Log::info(LOG_CATEGORY, "Total model loading time: {:.1f}ms", totalMs);

    return true;
}

void ModelManager::setupFileWatcher(CachedModel& model) {
    if (!model.autoReload) {
        return;
    }

    model.fileWatcher = std::make_unique<ModelFileWatcher>();

    fs::path absolutePath = projectRoot_ / model.path;
    ModelHandle handle = model.handle;

    model.fileWatcher->setReloadCallback([this, handle](const std::string& filepath) {
        std::lock_guard lock(mutex_);
        if (auto* m = cache_[handle].get()) {
            Log::info(LOG_CATEGORY, "File change detected: {}", filepath);
            m->pendingReload = true;
        }
    });

    model.fileWatcher->watchFile(absolutePath.string());
    model.fileWatcher->setLoadingState(ModelFileWatcher::LoadingState::Loaded);
}

void ModelManager::calculateMemoryUsage(CachedModel& model) {
    size_t usage = 0;

    // Vertex data
    usage += model.modelData.vertices.size() * sizeof(Vertex);
    usage += model.modelData.indices.size() * sizeof(uint32_t);

    // Images (CPU side)
    for (const auto& img : model.images) {
        if (img.pixels) {
            usage += img.width * img.height * 4;  // RGBA
        }
    }

    // Default texture
    if (model.defaultTexture.pixels) {
        usage += model.defaultTexture.width * model.defaultTexture.height * 4;
    }

    model.memoryUsageBytes = usage;
}

const CachedModel* ModelManager::getModel(ModelHandle handle) const {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it != cache_.end() && it->second->status == ModelStatus::Loaded) {
        return it->second.get();
    }
    return nullptr;
}

CachedModel* ModelManager::getModel(ModelHandle handle) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it != cache_.end() && it->second->status == ModelStatus::Loaded) {
        return it->second.get();
    }
    return nullptr;
}

const CachedModel* ModelManager::getModelByPath(const fs::path& relativePath) const {
    std::lock_guard lock(mutex_);

    auto it = pathToHandle_.find(relativePath.string());
    if (it != pathToHandle_.end()) {
        auto cacheIt = cache_.find(it->second);
        if (cacheIt != cache_.end() && cacheIt->second->status == ModelStatus::Loaded) {
            return cacheIt->second.get();
        }
    }
    return nullptr;
}

bool ModelManager::isLoaded(ModelHandle handle) const {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    return it != cache_.end() && it->second->status == ModelStatus::Loaded;
}

ModelStatus ModelManager::getStatus(ModelHandle handle) const {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it != cache_.end()) {
        return it->second->status;
    }
    return ModelStatus::NotLoaded;
}

void ModelManager::addReference(ModelHandle handle) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it != cache_.end()) {
        it->second->referenceCount++;
        Log::debug(
            LOG_CATEGORY,
            "Added reference to '{}': {} refs",
            it->second->displayName,
            it->second->referenceCount
        );
    }
}

void ModelManager::removeReference(ModelHandle handle) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it != cache_.end() && it->second->referenceCount > 0) {
        it->second->referenceCount--;
        Log::debug(
            LOG_CATEGORY,
            "Removed reference from '{}': {} refs",
            it->second->displayName,
            it->second->referenceCount
        );
    }
}

bool ModelManager::unloadModel(ModelHandle handle, bool force) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(handle);
    if (it == cache_.end()) {
        return false;
    }

    CachedModel* model = it->second.get();

    if (model->referenceCount > 0 && !force) {
        Log::warning(
            LOG_CATEGORY,
            "Cannot unload '{}': {} active references",
            model->displayName,
            model->referenceCount
        );
        return false;
    }

    Log::info(LOG_CATEGORY, "Unloading model: {}", model->displayName);

    // Stop file watching
    if (model->fileWatcher) {
        model->fileWatcher->stopWatching();
    }

    // Remove from path mapping
    pathToHandle_.erase(model->path.string());

    // Remove from cache
    cache_.erase(it);

    return true;
}

void ModelManager::unloadUnusedModels() {
    std::lock_guard lock(mutex_);

    std::vector<ModelHandle> toRemove;

    for (const auto& [handle, model] : cache_) {
        if (model->referenceCount == 0 && model->status == ModelStatus::Loaded) {
            toRemove.push_back(handle);
        }
    }

    for (ModelHandle handle : toRemove) {
        // Call without lock (we already hold it)
        auto it = cache_.find(handle);
        if (it != cache_.end()) {
            Log::info(LOG_CATEGORY, "Unloading unused model: {}", it->second->displayName);
            pathToHandle_.erase(it->second->path.string());
            cache_.erase(it);
        }
    }

    if (!toRemove.empty()) {
        Log::info(LOG_CATEGORY, "Unloaded {} unused models", toRemove.size());
    }
}

void ModelManager::reloadModel(ModelHandle handle) {
    // Use unique_lock to allow unlocking during I/O operations
    std::unique_lock lock(mutex_);

    auto it = cache_.find(handle);
    if (it == cache_.end()) {
        Log::warning(LOG_CATEGORY, "Cannot reload: model handle not found");
        return;
    }

    CachedModel* model = it->second.get();
    Log::info(LOG_CATEGORY, "Reloading model: {}", model->displayName);

    // Store state to preserve
    bool wasAutoReload = model->autoReload;

    // Stop file watcher during reload
    if (model->fileWatcher) {
        model->fileWatcher->stopWatching();
    }

    model->status = ModelStatus::Loading;

    // Release lock during I/O-heavy loadModelInternal to avoid blocking other threads
    // Note: projectRoot_ is read-only after setProjectRoot(), so this is safe
    lock.unlock();

    bool loadSuccess = loadModelInternal(*model);

    // Reacquire lock to update shared state
    lock.lock();

    // Verify model wasn't invalidated while we were loading
    it = cache_.find(handle);
    if (it == cache_.end()) {
        Log::warning(LOG_CATEGORY, "Model was removed during reload");
        return;
    }

    if (loadSuccess) {
        model->status = ModelStatus::Loaded;
        model->loadedAt = std::chrono::system_clock::now();
        model->lastAccessed = model->loadedAt;
        model->errorMessage.clear();
        model->pendingReload = false;

        calculateMemoryUsage(*model);

        model->autoReload = wasAutoReload;
        setupFileWatcher(*model);

        Log::info(LOG_CATEGORY, "Model reloaded successfully: {}", model->displayName);

        // Notify listeners - release lock first to avoid deadlock in callbacks
        ModelReloadCallback callback = reloadCallback_;
        lock.unlock();
        if (callback) {
            callback(handle);
        }
    } else {
        model->status = ModelStatus::Error;
        Log::error(LOG_CATEGORY, "Failed to reload model: {}", model->displayName);
    }
}

void ModelManager::processPendingReloads() {
    // Handle pending directory rescan first
    if (pendingRescan_.exchange(false)) {
        Log::info(LOG_CATEGORY, "Processing pending directory rescan");
        scanModels();
    }

    // Then handle individual model reloads
    std::vector<ModelHandle> toReload;

    {
        std::lock_guard lock(mutex_);
        for (const auto& [handle, model] : cache_) {
            if (model->pendingReload) {
                toReload.push_back(handle);
            }
        }
    }

    for (ModelHandle handle : toReload) {
        reloadModel(handle);
    }
}

size_t ModelManager::getTotalMemoryUsage() const {
    std::lock_guard lock(mutex_);

    size_t total = 0;
    for (const auto& [handle, model] : cache_) {
        if (model->status == ModelStatus::Loaded) {
            total += model->memoryUsageBytes;
        }
    }
    return total;
}

size_t ModelManager::getLoadedModelCount() const {
    std::lock_guard lock(mutex_);

    size_t count = 0;
    for (const auto& [handle, model] : cache_) {
        if (model->status == ModelStatus::Loaded) {
            count++;
        }
    }
    return count;
}

std::vector<ModelHandle> ModelManager::getAllModels() const {
    std::lock_guard lock(mutex_);

    std::vector<ModelHandle> handles;
    handles.reserve(cache_.size());
    for (const auto& [handle, model] : cache_) {
        handles.push_back(handle);
    }
    return handles;
}

void ModelManager::clearCache(bool force) {
    std::lock_guard lock(mutex_);

    if (force) {
        Log::info(LOG_CATEGORY, "Force clearing all {} cached models", cache_.size());
        cache_.clear();
        pathToHandle_.clear();
    } else {
        // Only clear unused models
        std::vector<ModelHandle> toRemove;
        for (const auto& [handle, model] : cache_) {
            if (model->referenceCount == 0) {
                toRemove.push_back(handle);
            }
        }

        for (ModelHandle handle : toRemove) {
            auto it = cache_.find(handle);
            if (it != cache_.end()) {
                pathToHandle_.erase(it->second->path.string());
                cache_.erase(it);
            }
        }

        Log::info(LOG_CATEGORY, "Cleared {} unused models from cache", toRemove.size());
    }
}
