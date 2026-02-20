#include "shader_manager.h"
#include "../util/logger.h"
#include "../external/SimpleFileDialog.h"
#include "../graph/node_graph.h"
#include "../graph/pipeline_node.h"
#include "../ui/pipeline_settings.h"
#include "shader_reflection.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_node_editor.h>

ShaderManager::ShaderManager()
    : fileWatcher(nullptr)
    , autoReloadEnabled(true)
    , modelDirectoryWatcher(nullptr)
    , stateDirectoryWatcher(nullptr)
    , modelWatchingEnabled(true)
    , stateWatchingEnabled(true)
    , pendingModelRescan(false)
    , pendingStateRescan(false) {
    // File watcher will be initialized when project root is set
}

ShaderManager::~ShaderManager() {
    shutdownFileWatcher();
    shutdownDirectoryWatchers();
}

void ShaderManager::setProjectRoot(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    projectRoot = root;

    // Create necessary workflow directories if they don't exist
    std::array<fs::path, 7> requiredDirs = {
        projectRoot / "shaders",
        projectRoot / "compiled_shaders",
        projectRoot / "data" / "models",
        projectRoot / "data" / "textures",
        projectRoot / "data" / "images",
        projectRoot / "saved_states",
        projectRoot / "logs"
    };

    for (const auto& dir : requiredDirs) {
        if (!fs::exists(dir)) {
            try {
                fs::create_directories(dir);
                Log::info(
                    "ShaderManager", "Created directory: {}", dir.string()
                );
            } catch (const fs::filesystem_error& e) {
                Log::error(
                    "ShaderManager", "Failed to create directory {}: {}",
                    dir.string(), e.what()
                );
            }
        }
    }

    // Scan all project assets
    scanShaders();
    scanModels();
    scanStates();

    // Initialize file watcher with the shader directory
    initializeFileWatcher();

    // Initialize directory watchers for models and states
    initializeDirectoryWatchers();
}

void ShaderManager::initializeFileWatcher() {
    namespace fs = std::filesystem;

    // Shutdown existing watcher if any
    shutdownFileWatcher();

    fs::path shaderDir = projectRoot / "shaders";

    // Check if shader directory exists
    if (!fs::exists(shaderDir) || !fs::is_directory(shaderDir)) {
        Log::warning(
            "ShaderManager",
            "Shader directory does not exist: {}. File watcher not "
            "initialized.",
            shaderDir.string()
        );
        return;
    }

    Log::info(
        "ShaderManager", "Initializing file watcher for: {}",
        shaderDir.string()
    );

    try {
        // Create file watcher for shader directory
        fileWatcher =
            std::make_unique<ShaderFileWatcher>(shaderDir.string());

        // Set up callback for file changes
        fileWatcher->setReloadCallback(
            [this](const std::string& filepath) {
                this->onShaderFileChanged(filepath);
            }
        );

        // Configure debounce delay (500ms default)
        fileWatcher->setDebounceDelay(500);

        // Start watching if auto-reload is enabled
        if (autoReloadEnabled) {
            fileWatcher->start();
            Log::info("ShaderManager", "File watcher started");
        }

    } catch (const std::exception& e) {
        Log::error(
            "ShaderManager", "Failed to initialize file watcher: {}",
            e.what()
        );
        fileWatcher.reset();
    }
}

void ShaderManager::shutdownFileWatcher() {
    if (fileWatcher) {
        fileWatcher->stop();
        fileWatcher.reset();
        Log::info("ShaderManager", "File watcher shutdown");
    }
}

void ShaderManager::initializeDirectoryWatchers() {
    namespace fs = std::filesystem;

    // Shutdown existing watchers if any
    shutdownDirectoryWatchers();

    // Initialize model directory watcher
    fs::path modelDir = projectRoot / "data/models";
    if (fs::exists(modelDir) && fs::is_directory(modelDir)) {
        Log::info(
            "ShaderManager", "Initializing model directory watcher for: {}",
            modelDir.string()
        );

        try {
            modelDirectoryWatcher = std::make_unique<DirectoryWatcher>("ModelDirWatcher");
            modelDirectoryWatcher->setFileChangeCallback(
                [this](const std::string& filepath, const std::string& filename, DirectoryWatcher::FileAction action) {
                    this->onModelDirectoryChanged(filepath, filename, action);
                }
            );
            modelDirectoryWatcher->setDebounceDelay(500);

            if (modelWatchingEnabled) {
                modelDirectoryWatcher->watchDirectory(
                    modelDir.string(),
                    {".gltf", ".glb", ".obj"},
                    true  // recursive
                );
            }
        } catch (const std::exception& e) {
            Log::error(
                "ShaderManager", "Failed to initialize model directory watcher: {}",
                e.what()
            );
            modelDirectoryWatcher.reset();
        }
    }

    // Initialize state directory watcher
    fs::path stateDir = projectRoot / "saved_states";
    if (fs::exists(stateDir) && fs::is_directory(stateDir)) {
        Log::info(
            "ShaderManager", "Initializing state directory watcher for: {}",
            stateDir.string()
        );

        try {
            stateDirectoryWatcher = std::make_unique<DirectoryWatcher>("StateDirWatcher");
            stateDirectoryWatcher->setFileChangeCallback(
                [this](const std::string& filepath, const std::string& filename, DirectoryWatcher::FileAction action) {
                    this->onStateDirectoryChanged(filepath, filename, action);
                }
            );
            stateDirectoryWatcher->setDebounceDelay(500);

            if (stateWatchingEnabled) {
                stateDirectoryWatcher->watchDirectory(
                    stateDir.string(),
                    {".json"},
                    false  // non-recursive for states
                );
            }
        } catch (const std::exception& e) {
            Log::error(
                "ShaderManager", "Failed to initialize state directory watcher: {}",
                e.what()
            );
            stateDirectoryWatcher.reset();
        }
    }
}

void ShaderManager::shutdownDirectoryWatchers() {
    if (modelDirectoryWatcher) {
        modelDirectoryWatcher->stopWatching();
        modelDirectoryWatcher.reset();
        Log::info("ShaderManager", "Model directory watcher shutdown");
    }
    if (stateDirectoryWatcher) {
        stateDirectoryWatcher->stopWatching();
        stateDirectoryWatcher.reset();
        Log::info("ShaderManager", "State directory watcher shutdown");
    }
}

void ShaderManager::onModelDirectoryChanged(
    const std::string& filepath,
    const std::string& filename,
    DirectoryWatcher::FileAction action
) {
    switch (action) {
    case DirectoryWatcher::FileAction::Added:
        Log::info("ShaderManager", "New model detected: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Deleted:
        Log::info("ShaderManager", "Model deleted: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Modified:
        Log::info("ShaderManager", "Model modified: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Moved:
        Log::info("ShaderManager", "Model moved: {}", filename);
        break;
    }

    // Flag for rescan
    pendingModelRescan = true;
}

void ShaderManager::onStateDirectoryChanged(
    const std::string& filepath,
    const std::string& filename,
    DirectoryWatcher::FileAction action
) {
    switch (action) {
    case DirectoryWatcher::FileAction::Added:
        Log::info("ShaderManager", "New state file detected: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Deleted:
        Log::info("ShaderManager", "State file deleted: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Modified:
        Log::info("ShaderManager", "State file modified: {}", filename);
        break;
    case DirectoryWatcher::FileAction::Moved:
        Log::info("ShaderManager", "State file moved: {}", filename);
        break;
    }

    // Flag for rescan
    pendingStateRescan = true;
}

void ShaderManager::processPendingDirectoryChanges() {
    if (pendingModelRescan) {
        Log::info("ShaderManager", "Rescanning models directory...");
        scanModels();
        pendingModelRescan = false;
    }

    if (pendingStateRescan) {
        Log::info("ShaderManager", "Rescanning states directory...");
        scanStates();
        pendingStateRescan = false;
    }
}

void ShaderManager::setModelWatchingEnabled(bool enabled) {
    modelWatchingEnabled = enabled;

    if (modelDirectoryWatcher) {
        if (enabled && !modelDirectoryWatcher->isWatching()) {
            namespace fs = std::filesystem;
            fs::path modelDir = projectRoot / "data/models";
            if (fs::exists(modelDir)) {
                modelDirectoryWatcher->watchDirectory(
                    modelDir.string(),
                    {".gltf", ".glb", ".obj"},
                    true
                );
                Log::info("ShaderManager", "Model directory watching enabled");
            }
        } else if (!enabled && modelDirectoryWatcher->isWatching()) {
            modelDirectoryWatcher->stopWatching();
            Log::info("ShaderManager", "Model directory watching disabled");
        }
    }
}

void ShaderManager::setStateWatchingEnabled(bool enabled) {
    stateWatchingEnabled = enabled;

    if (stateDirectoryWatcher) {
        if (enabled && !stateDirectoryWatcher->isWatching()) {
            namespace fs = std::filesystem;
            fs::path stateDir = projectRoot / "saved_states";
            if (fs::exists(stateDir)) {
                stateDirectoryWatcher->watchDirectory(
                    stateDir.string(),
                    {".json"},
                    false
                );
                Log::info("ShaderManager", "State directory watching enabled");
            }
        } else if (!enabled && stateDirectoryWatcher->isWatching()) {
            stateDirectoryWatcher->stopWatching();
            Log::info("ShaderManager", "State directory watching disabled");
        }
    }
}

bool ShaderManager::isModelWatchingEnabled() const {
    return modelWatchingEnabled && modelDirectoryWatcher && modelDirectoryWatcher->isWatching();
}

bool ShaderManager::isStateWatchingEnabled() const {
    return stateWatchingEnabled && stateDirectoryWatcher && stateDirectoryWatcher->isWatching();
}

void ShaderManager::onShaderFileChanged(const std::string& filepath) {
    if (!autoReloadEnabled) {
        return;
    }

    namespace fs = std::filesystem;

    Log::debug("ShaderManager", "Detected change in: {}", filepath);

    // Convert absolute path to project-relative path
    fs::path absPath = fs::absolute(filepath);
    fs::path relPath;

    try {
        relPath = fs::relative(absPath, projectRoot);
    } catch (const std::exception& e) {
        Log::error(
            "ShaderManager", "Failed to compute relative path: {}",
            e.what()
        );
        return;
    }

    // Queue the reload
    queueReload(relPath.string());
}

void ShaderManager::queueReload(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(reloadMutex);
    pendingReloads.push(filepath);

    Log::debug("ShaderManager", "Queued reload for: {}", filepath);
}

bool ShaderManager::hasPendingReloads() const {
    std::lock_guard<std::mutex> lock(reloadMutex);
    return !pendingReloads.empty();
}

void ShaderManager::processPendingReloads(NodeGraph& graph) {
    std::lock_guard<std::mutex> lock(reloadMutex);

    if (pendingReloads.empty()) {
        return;
    }

    Log::info(
        "ShaderManager", "Processing {} pending reload(s)",
        pendingReloads.size()
    );

    // Process all pending reloads
    while (!pendingReloads.empty()) {
        std::string filepath = pendingReloads.front();
        pendingReloads.pop();

        // Check if we've recently reloaded this file (avoid duplicates)
        auto now = std::chrono::steady_clock::now();
        auto it = lastReloadTime.find(filepath);

        if (it != lastReloadTime.end()) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second
                )
                    .count();

            // Skip if reloaded within last 1000ms
            if (elapsed < 1000) {
                Log::debug(
                    "ShaderManager", "Skipping duplicate reload: {}",
                    filepath
                );
                continue;
            }
        }

        // Update last reload time
        lastReloadTime[filepath] = now;

        Log::info("ShaderManager", "Reloading shader: {}", filepath);

        // Reset Slang session to force fresh parsing (avoid cached
        // modules)
        ShaderReflection::resetSession();

        // Rescan shaders to pick up any changes
        scanShaders();

        // Find and update all pipelines using this shader
        auto affectedPipelines =
            findPipelinesUsingShader(filepath, graph);

        if (affectedPipelines.empty()) {
            Log::debug(
                "ShaderManager", "No pipelines using this shader"
            );
        } else {
            Log::info(
                "ShaderManager", "Updating {} pipeline(s)",
                affectedPipelines.size()
            );

            for (auto* pipeline : affectedPipelines) {
                try {
                    // Trigger shader reflection to recompile
                    bool success = reflectShader(pipeline, graph);
                    if (success) {
                        Log::info(
                            "ShaderManager", "Updated pipeline: {}",
                            pipeline->name
                        );
                    } else {
                        Log::error(
                            "ShaderManager",
                            "Shader syntax error in pipeline '{}' - keeping previous state",
                            pipeline->name
                        );
                    }
                } catch (const std::exception& e) {
                    Log::error(
                        "ShaderManager",
                        "Failed to update pipeline {}: {}",
                        pipeline->name, e.what()
                    );
                }
            }
        }
    }

    Log::debug("ShaderManager", "Reload processing complete");
}

std::vector<PipelineNode*> ShaderManager::findPipelinesUsingShader(
    const std::string& shaderPath,
    NodeGraph& graph
) {
    namespace fs = std::filesystem;
    std::vector<PipelineNode*> result;
    std::vector<PipelineNode*> allPipelines;

    fs::path normalizedPath = fs::path(shaderPath).lexically_normal();
    std::string modifiedFilename = normalizedPath.filename().string();

    for (auto& nodePtr : graph.nodes) {
        auto* pipeline = dynamic_cast<PipelineNode*>(nodePtr.get());
        if (!pipeline)
            continue;

        allPipelines.push_back(pipeline);

        // Check vertex shader (direct match)
        if (!pipeline->settings.vertexShaderPath.empty()) {
            fs::path vertPath =
                pipeline->settings.vertexShaderPath.lexically_normal();
            if (vertPath == normalizedPath) {
                result.push_back(pipeline);
                continue;
            }
        }

        // Check fragment shader (direct match)
        if (!pipeline->settings.fragmentShaderPath.empty()) {
            fs::path fragPath = pipeline->settings.fragmentShaderPath
                                    .lexically_normal();
            if (fragPath == normalizedPath) {
                result.push_back(pipeline);
                continue;
            }
        }

        // Check if shaders import the modified file
        // Read shader files and check for import statements
        auto checkShaderImports =
            [&](const fs::path& shaderRelPath) -> bool {
            if (shaderRelPath.empty())
                return false;

            // Resolve to absolute path using project root
            fs::path shaderFile = projectRoot / shaderRelPath;
            if (!fs::exists(shaderFile))
                return false;

            std::ifstream file(shaderFile);
            if (!file.is_open())
                return false;

            std::string line;
            while (std::getline(file, line)) {
                // Check for Slang import: import common;
                if (line.find("import ") != std::string::npos) {
                    // Extract module name from "import moduleName;"
                    std::string importModule = modifiedFilename;
                    // Remove .slang extension for comparison
                    if (importModule.size() > 6 &&
                        importModule.substr(importModule.size() - 6) ==
                            ".slang") {
                        importModule = importModule.substr(
                            0, importModule.size() - 6
                        );
                    }
                    if (line.find("import " + importModule) !=
                        std::string::npos) {
                        return true;
                    }
                }
                // Check for #include "filename"
                if (line.find("#include") != std::string::npos &&
                    line.find(modifiedFilename) != std::string::npos) {
                    return true;
                }
            }
            return false;
        };

        if (checkShaderImports(pipeline->settings.vertexShaderPath) ||
            checkShaderImports(pipeline->settings.fragmentShaderPath)) {
            result.push_back(pipeline);
            Log::debug(
                "ShaderManager",
                "Pipeline '{}' imports modified file: {}",
                pipeline->name, modifiedFilename
            );
        }
    }

    // If no direct matches and no import matches found, but there are
    // pipelines, it might be a deeply nested include - reload all
    // pipelines to be safe
    if (result.empty() && !allPipelines.empty()) {
        Log::debug(
            "ShaderManager",
            "No direct match found for '{}', reloading all pipelines "
            "(might be nested include)",
            shaderPath
        );
        return allPipelines;
    }

    return result;
}

void ShaderManager::setAutoReloadEnabled(bool enabled) {
    autoReloadEnabled = enabled;

    if (fileWatcher) {
        if (enabled && !fileWatcher->isWatching()) {
            fileWatcher->start();
            Log::info("ShaderManager", "Auto-reload enabled");
        } else if (!enabled && fileWatcher->isWatching()) {
            fileWatcher->stop();
            Log::info("ShaderManager", "Auto-reload disabled");
        }
    }
}

bool ShaderManager::isAutoReloadEnabled() const {
    return autoReloadEnabled && fileWatcher &&
           fileWatcher->isWatching();
}

int ShaderManager::getDebounceDelay() const {
    if (fileWatcher) {
        return 500;
    }
    return 500;
}

void ShaderManager::setDebounceDelay(int milliseconds) {
    if (fileWatcher) {
        fileWatcher->setDebounceDelay(milliseconds);
        Log::debug(
            "ShaderManager", "Debounce delay set to {}ms", milliseconds
        );
    }
}

void ShaderManager::reloadAllShaders(NodeGraph& graph) {
    scanShaders();

    for (auto& nodePtr : graph.nodes) {
        auto* pipeline = dynamic_cast<PipelineNode*>(nodePtr.get());
        if (!pipeline)
            continue;

        // Fix: Correct path emptiness check
        if (!pipeline->settings.vertexShaderPath.empty() ||
            !pipeline->settings.fragmentShaderPath.empty()) {
            try {
                bool success = reflectShader(pipeline, graph);
                if (!success) {
                    Log::error(
                        "ShaderManager",
                        "Shader syntax error in pipeline '{}' - keeping previous state",
                        pipeline->name
                    );
                }
            } catch (const std::exception& e) {
                Log::error(
                    "ShaderManager", "Failed to reload pipeline {}: {}",
                    pipeline->name, e.what()
                );
            }
        }
    }
}

void ShaderManager::scanShaders() {
    namespace fs = std::filesystem;
    slangShaders.clear();

    fs::path shaderDir = projectRoot / "shaders";

    if (!fs::exists(shaderDir) || !fs::is_directory(shaderDir)) {
        Log::warning(
            "ShaderManager", "Shader folder does not exist: {}",
            shaderDir.string()
        );
        return;
    }

    for (auto& entry : fs::directory_iterator(shaderDir)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".slang") {
            fs::path relative = fs::relative(entry.path(), projectRoot);
            slangShaders.push_back(relative);
            Log::debug(
                "ShaderManager", "Found shader: {}", relative.string()
            );
        }
    }

    std::sort(slangShaders.begin(), slangShaders.end());
    Log::info(
        "ShaderManager", "Total shaders found: {}", slangShaders.size()
    );
}

const std::vector<std::filesystem::path>&
ShaderManager::getShaders() const {
    return slangShaders;
}

void ShaderManager::showShaderPicker(
    PipelineNode* node,
    const char* label,
    std::filesystem::path& outPathProject,
    std::filesystem::path& outCompiledPath,
    NodeGraph& graph
) {
    namespace fs = std::filesystem;

    int currentIndex = -1;

    fs::path currentPath;
    if (!outPathProject.empty()) {
        currentPath = outPathProject.lexically_normal();
    }

    // Find current index by path comparison
    for (int i = 0; const auto& shader : slangShaders) {
        fs::path shaderPath = shader.lexically_normal();
        if (!currentPath.empty() && shaderPath == currentPath) {
            currentIndex = i;
            break;
        }
        i++;
    }

    std::string previewStr =
        (currentIndex >= 0)
            ? slangShaders[currentIndex].filename().string()
            : "<select shader>";

    if (ImGui::BeginCombo(label, previewStr.c_str())) {
        for (int i = 0; const auto& shader : slangShaders) {
            bool isSelected = (i == currentIndex);

	    auto shaderPathStr{shader.generic_string()};
            if (ImGui::Selectable(shaderPathStr.c_str(), isSelected)) {
                // Save old paths in case compilation fails
                fs::path oldPathProject = outPathProject;
                fs::path oldCompiledPath = outCompiledPath;

                // Store project-relative path
                outPathProject = shader;
                // Compiled path is also project-relative
                outCompiledPath = shader;
                outCompiledPath.replace_extension(".spv");

                if (node) {
                    bool success = reflectShader(node, graph);
                    if (!success) {
                        // Restore old paths on syntax error
                        outPathProject = oldPathProject;
                        outCompiledPath = oldCompiledPath;
                    }
                }
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();

            i++;
        }
        ImGui::EndCombo();
    }
}

bool ShaderManager::reflectShader(
    PipelineNode* pipeline,
    NodeGraph& graph
) {
    if (!pipeline)
        return false;
    ShaderReflection::initializeSlang();
    return pipeline->updateShaderReflection(graph, projectRoot);
}

void ShaderManager::scanModels() {
    namespace fs = std::filesystem;
    modelFiles.clear();

    fs::path modelDir = projectRoot / "data/models";

    if (!fs::exists(modelDir) || !fs::is_directory(modelDir)) {
        Log::warning(
            "ShaderManager", "Models folder does not exist: {}",
            modelDir.string()
        );
        return;
    }

    // Recursively scan for model files
    for (auto& entry : fs::recursive_directory_iterator(modelDir)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".gltf" || ext == ".glb") {
            fs::path relative = fs::relative(entry.path(), projectRoot);
            modelFiles.push_back(relative);
            Log::debug(
                "ShaderManager", "Found model: {}", relative.string()
            );
        }
    }

    std::sort(modelFiles.begin(), modelFiles.end());
    Log::info(
        "ShaderManager", "Total models found: {}", modelFiles.size()
    );
}

const std::vector<std::filesystem::path>&
ShaderManager::getModels() {
    // Process any pending directory changes to ensure list is up-to-date
    if (pendingModelRescan) {
        Log::info("ShaderManager", "Auto-rescanning models directory...");
        scanModels();
        pendingModelRescan = false;
    }
    return modelFiles;
}

void ShaderManager::scanStates() {
    namespace fs = std::filesystem;
    stateFiles.clear();

    fs::path stateDir = projectRoot / "saved_states";

    if (!fs::exists(stateDir) || !fs::is_directory(stateDir)) {
        Log::warning(
            "ShaderManager", "Saved states folder does not exist: {}",
            stateDir.string()
        );
        return;
    }

    for (auto& entry : fs::directory_iterator(stateDir)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".json") {
            fs::path relative = fs::relative(entry.path(), projectRoot);
            stateFiles.push_back(relative);
            Log::debug(
                "ShaderManager", "Found state: {}", relative.string()
            );
        }
    }

    std::sort(stateFiles.begin(), stateFiles.end());
    Log::info(
        "ShaderManager", "Total states found: {}", stateFiles.size()
    );
}

const std::vector<std::filesystem::path>&
ShaderManager::getStates() {
    // Process any pending directory changes to ensure list is up-to-date
    if (pendingStateRescan) {
        Log::info("ShaderManager", "Auto-rescanning states directory...");
        scanStates();
        pendingStateRescan = false;
    }
    return stateFiles;
}

bool ShaderManager::showModelPicker(
    const char* label,
    std::filesystem::path& outModelPath
) {
    namespace fs = std::filesystem;

    // Process any pending directory changes to ensure list is up-to-date
    if (pendingModelRescan) {
        Log::info("ShaderManager", "Updating model list before picker display...");
        scanModels();
        pendingModelRescan = false;
    }

    int currentIndex = -1;

    fs::path currentPath;
    if (!outModelPath.empty()) {
        currentPath = outModelPath.lexically_normal();
    }

    // Find current index by path comparison
    for (int i = 0; const auto& model : modelFiles) {
        fs::path modelPath = model.lexically_normal();
        if (!currentPath.empty() && modelPath == currentPath) {
            currentIndex = i;
            break;
        }
        i++;
    }

    std::string previewStr =
        (currentIndex >= 0)
            ? modelFiles[currentIndex].filename().string()
            : "<select model>";

    bool selected = false;

    if (ImGui::BeginCombo(label, previewStr.c_str())) {
        for (int i = 0; const auto& model : modelFiles) {
            bool isSelected = (i == currentIndex);

	    auto modelPathStr{model.generic_string()};
            if (ImGui::Selectable(modelPathStr.c_str(), isSelected)) {
                outModelPath = model;
                selected = true;
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();

            i++;
        }
        ImGui::EndCombo();
    }

    return selected;
}

std::filesystem::path ShaderManager::showStatePicker(const char* label) {
    namespace fs = std::filesystem;

    // Process any pending directory changes to ensure list is up-to-date
    if (pendingStateRescan) {
        Log::info("ShaderManager", "Updating state list before picker display...");
        scanStates();
        pendingStateRescan = false;
    }

    std::string previewStr = "<select state>";

    fs::path selectedPath;

    if (ImGui::BeginCombo(label, previewStr.c_str())) {
        for (const auto& state : stateFiles) {
            if (ImGui::Selectable(state.filename().string().c_str(), false)) {
                selectedPath = projectRoot / state;
            }
        }
        ImGui::EndCombo();
    }

    return selectedPath;
}
