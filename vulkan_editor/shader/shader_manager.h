#pragma once
#include "../io/directory_watcher.h"
#include "shader_watcher.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class PipelineNode;
class NodeGraph;

/**
 * @class ShaderManager
 * @brief Central manager for shader discovery, compilation, reflection, and hot-reload.
 *
 * Scans project directories for Slang shaders and 3D models, provides UI pickers
 * for asset selection, compiles shaders via Slang, and performs reflection to
 * extract binding information. Includes file watching for automatic hot-reload
 * when shaders are modified externally.
 */
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    void setProjectRoot(const std::filesystem::path& root);
    std::string getProjectRoot() const {
        return projectRoot.string();
    }
    void scanShaders();
    void scanModels();
    void scanStates();

    const std::vector<std::filesystem::path>& getShaders() const;
    const std::vector<std::filesystem::path>& getModels();  // Non-const to allow auto-rescan
    const std::vector<std::filesystem::path>& getStates();  // Non-const to allow auto-rescan

    void showShaderPicker(
        PipelineNode* selectedNode,
        const char* label,
        std::filesystem::path& outPathProject, // relative to project root
        std::filesystem::path& outCompiledPath, // relative to project root
        NodeGraph& graph
    );

    bool reflectShader(PipelineNode* node, NodeGraph& graph);
    bool showModelPicker(const char* label, std::filesystem::path& outModelPath);
    std::filesystem::path showStatePicker(const char* label);

    void processPendingReloads(NodeGraph& graph);
    bool hasPendingReloads() const;
    void setAutoReloadEnabled(bool enabled);
    bool isAutoReloadEnabled() const;
    int getDebounceDelay() const;
    void setDebounceDelay(int milliseconds);
    void reloadAllShaders(NodeGraph& graph);

    bool needsModelRescan() const { return pendingModelRescan; }
    bool needsStateRescan() const { return pendingStateRescan; }
    void clearModelRescanFlag() { pendingModelRescan = false; }
    void clearStateRescanFlag() { pendingStateRescan = false; }
    void processPendingDirectoryChanges();
    void setModelWatchingEnabled(bool enabled);
    void setStateWatchingEnabled(bool enabled);
    bool isModelWatchingEnabled() const;
    bool isStateWatchingEnabled() const;

private:
    void initializeFileWatcher();
    void shutdownFileWatcher();
    void initializeDirectoryWatchers();
    void shutdownDirectoryWatchers();
    void onShaderFileChanged(const std::string& filepath);
    void onModelDirectoryChanged(const std::string& filepath, const std::string& filename, DirectoryWatcher::FileAction action);
    void onStateDirectoryChanged(const std::string& filepath, const std::string& filename, DirectoryWatcher::FileAction action);
    void queueReload(const std::string& filepath);
    std::vector<PipelineNode*> findPipelinesUsingShader(
        const std::string& shaderPath,
        NodeGraph& graph
    );

    std::filesystem::path projectRoot;
    std::vector<std::filesystem::path> slangShaders;
    std::vector<std::filesystem::path> modelFiles;
    std::vector<std::filesystem::path> stateFiles;

    std::unique_ptr<ShaderFileWatcher> fileWatcher;
    bool autoReloadEnabled;

    mutable std::mutex reloadMutex;
    std::queue<std::string> pendingReloads;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastReloadTime;

    std::unique_ptr<DirectoryWatcher> modelDirectoryWatcher;
    std::unique_ptr<DirectoryWatcher> stateDirectoryWatcher;
    bool modelWatchingEnabled{true};
    bool stateWatchingEnabled{true};
    bool pendingModelRescan{false};
    bool pendingStateRescan{false};
};