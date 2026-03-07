#pragma once

#include "vulkan_editor/io/model_watcher.h"
#include "vulkan_editor/gpu/primitives.h"
#include <vkDuck/model_loader.h> // For Vertex, GLTFCamera, GLTFLight
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Types needed for CachedModel (duplicated from model_node.h to avoid circular dep)
// ============================================================================

struct EditorImage {
    std::filesystem::path path{};
    void* pixels{nullptr};
    bool toLoad{false};
    uint32_t width{0};
    uint32_t height{0};
    primitives::StoreHandle image{};  // Forward-declared, only used as handle

    ~EditorImage();
};

struct EditorMaterial {
    int baseTextureIndex{-1};
};

struct EditorGeometryRange {
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t firstIndex;
    uint32_t indexCount;
    int materialIndex;
    VkPrimitiveTopology topology;
};

struct ConsolidatedModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<EditorGeometryRange> ranges;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;

    void clear() {
        vertices.clear();
        indices.clear();
        ranges.clear();
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferAllocation = VK_NULL_HANDLE;
        indexBuffer = VK_NULL_HANDLE;
        indexBufferAllocation = VK_NULL_HANDLE;
    }

    size_t getTotalVertexCount() const { return vertices.size(); }
    size_t getTotalIndexCount() const { return indices.size(); }
    size_t getGeometryCount() const { return ranges.size(); }
};

/**
 * @brief Unique identifier for a cached model asset.
 *
 * Using a strong type prevents accidental misuse of raw integers.
 */
struct ModelHandle {
    uint32_t id{UINT32_MAX};

    bool isValid() const { return id != UINT32_MAX; }
    bool operator==(const ModelHandle& other) const { return id == other.id; }
    bool operator!=(const ModelHandle& other) const { return id != other.id; }
};

// Hash for ModelHandle to use in unordered containers
template <>
struct std::hash<ModelHandle> {
    size_t operator()(const ModelHandle& h) const noexcept {
        return std::hash<uint32_t>{}(h.id);
    }
};

/**
 * @brief Status of a model in the cache.
 */
enum class ModelStatus {
    NotLoaded,  ///< Model path known but not loaded
    Loading,    ///< Currently loading (async)
    Loaded,     ///< Successfully loaded and ready
    Error       ///< Failed to load
};

/**
 * @brief Cached model data with metadata.
 *
 * Contains all the data from a loaded model plus metadata for
 * cache management and error handling.
 */
struct CachedModel {
    // Identification
    ModelHandle handle;
    std::filesystem::path path;
    std::string displayName;  ///< User-friendly name (filename without extension)

    // Status
    ModelStatus status{ModelStatus::NotLoaded};
    std::string errorMessage;
    std::chrono::system_clock::time_point loadedAt;
    std::chrono::system_clock::time_point lastAccessed;

    // Model data (populated when status == Loaded)
    ConsolidatedModelData modelData;
    std::vector<EditorMaterial> materials;
    std::vector<EditorImage> images;
    std::vector<GLTFCamera> cameras;
    std::vector<GLTFLight> lights;

    // File watching
    std::unique_ptr<ModelFileWatcher> fileWatcher;
    bool autoReload{true};
    bool pendingReload{false};

    // Statistics
    size_t referenceCount{0};  ///< Number of nodes using this model
    size_t memoryUsageBytes{0};

    // Default texture for fallback
    EditorImage defaultTexture;

    CachedModel() = default;
    ~CachedModel() = default;

    // Non-copyable, movable
    CachedModel(const CachedModel&) = delete;
    CachedModel& operator=(const CachedModel&) = delete;
    CachedModel(CachedModel&&) = default;
    CachedModel& operator=(CachedModel&&) = default;
};

/**
 * @brief Callback for model load completion.
 */
using ModelLoadCallback = std::function<void(ModelHandle, bool success)>;

/**
 * @brief Callback for model reload events.
 */
using ModelReloadCallback = std::function<void(ModelHandle)>;

/**
 * @class ModelManager
 * @brief Centralized service for loading, caching, and managing 3D models.
 *
 * Provides:
 * - Model loading with automatic caching
 * - File watching for hot-reload
 * - Reference counting for cache management
 * - Thread-safe access to model data
 * - Memory usage tracking
 *
 * Usage:
 * @code
 * // Initialize once at startup
 * ModelManager modelManager;
 * g_modelManager = &modelManager;
 *
 * // Use throughout the application
 * g_modelManager->setProjectRoot("/path/to/project");
 * ModelHandle handle = g_modelManager->loadModel("models/cube.gltf");
 * @endcode
 */
class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    // Non-copyable
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    /**
     * @brief Set the project root directory.
     *
     * Models are loaded relative to this path. Also used for locating
     * the default texture and other project resources.
     *
     * @param root Absolute path to project root
     */
    void setProjectRoot(const std::filesystem::path& root);
    const std::filesystem::path& getProjectRoot() const { return projectRoot_; }

    /**
     * @brief Scan the project's models directory for available models.
     *
     * Populates the available models list without loading them.
     * Call this after setProjectRoot().
     */
    void scanModels();

    /**
     * @brief Get list of available model paths (relative to project root).
     */
    std::vector<std::filesystem::path> getAvailableModels() const;

    /**
     * @brief Load a model (blocking).
     *
     * If the model is already cached, returns the existing handle.
     * Otherwise, loads the model synchronously.
     *
     * @param relativePath Path relative to project root
     * @return Handle to the loaded model, or invalid handle on failure
     */
    ModelHandle loadModel(const std::filesystem::path& relativePath);

    /**
     * @brief Load a model asynchronously.
     *
     * Returns immediately. The callback is invoked on the main thread
     * when loading completes.
     *
     * @param relativePath Path relative to project root
     * @param callback Called when loading completes
     * @return Handle that will be valid after loading completes
     */
    ModelHandle loadModelAsync(
        const std::filesystem::path& relativePath,
        ModelLoadCallback callback = nullptr
    );

    /**
     * @brief Get a cached model by handle.
     *
     * @param handle Model handle
     * @return Pointer to cached model, or nullptr if not found/not loaded
     */
    const CachedModel* getModel(ModelHandle handle) const;
    CachedModel* getModel(ModelHandle handle);

    /**
     * @brief Get a cached model by path.
     *
     * @param relativePath Path relative to project root
     * @return Pointer to cached model, or nullptr if not found/not loaded
     */
    const CachedModel* getModelByPath(const std::filesystem::path& relativePath) const;

    /**
     * @brief Check if a model is loaded and ready.
     */
    bool isLoaded(ModelHandle handle) const;

    /**
     * @brief Get the status of a model.
     */
    ModelStatus getStatus(ModelHandle handle) const;

    /**
     * @brief Increment reference count for a model.
     *
     * Call when a node starts using a model. Prevents unloading.
     */
    void addReference(ModelHandle handle);

    /**
     * @brief Decrement reference count for a model.
     *
     * Call when a node stops using a model.
     */
    void removeReference(ModelHandle handle);

    /**
     * @brief Unload a model from cache.
     *
     * Does nothing if the model has active references.
     *
     * @param handle Model to unload
     * @param force If true, unload even with active references
     * @return true if unloaded, false if still in use
     */
    bool unloadModel(ModelHandle handle, bool force = false);

    /**
     * @brief Unload all models without active references.
     */
    void unloadUnusedModels();

    /**
     * @brief Reload a model from disk.
     *
     * Preserves the handle but reloads all data.
     */
    void reloadModel(ModelHandle handle);

    /**
     * @brief Process pending reloads.
     *
     * Call this from the main loop to handle file-watcher triggered reloads.
     */
    void processPendingReloads();

    /**
     * @brief Register a callback for model reload events.
     */
    void setReloadCallback(ModelReloadCallback callback) {
        reloadCallback_ = std::move(callback);
    }

    /**
     * @brief Get total memory usage of all cached models.
     */
    size_t getTotalMemoryUsage() const;

    /**
     * @brief Get number of loaded models.
     */
    size_t getLoadedModelCount() const;

    /**
     * @brief Get all cached model handles.
     */
    std::vector<ModelHandle> getAllModels() const;

    /**
     * @brief Clear all cached models.
     *
     * @param force If true, clear even models with active references
     */
    void clearCache(bool force = false);

private:
    // Internal loading implementation
    bool loadModelInternal(CachedModel& model);
    void setupFileWatcher(CachedModel& model);
    ModelHandle findOrCreateHandle(const std::filesystem::path& relativePath);
    void calculateMemoryUsage(CachedModel& model);

    std::filesystem::path projectRoot_;
    std::vector<std::filesystem::path> availableModels_;

    // Model cache - indexed by handle
    std::unordered_map<ModelHandle, std::unique_ptr<CachedModel>> cache_;

    // Path to handle mapping for deduplication
    std::unordered_map<std::string, ModelHandle> pathToHandle_;

    // Handle generation
    uint32_t nextHandleId_{0};

    // Callbacks
    ModelReloadCallback reloadCallback_;

    // Thread safety
    mutable std::mutex mutex_;
};

// Global instance - initialized once in editor.cpp
extern ModelManager* g_modelManager;
