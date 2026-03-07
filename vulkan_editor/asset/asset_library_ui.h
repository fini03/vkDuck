#pragma once

#include "vulkan_editor/asset/model_manager.h"
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

/**
 * @brief Callback when a model is selected for use in a node.
 */
using ModelSelectedCallback = std::function<void(ModelHandle)>;

/**
 * @class AssetLibraryUI
 * @brief UI for the Asset Library tab - manages loading and browsing models.
 *
 * Provides a centralized interface for:
 * - Browsing available models in the project
 * - Loading/unloading models
 * - Viewing model details (vertices, textures, cameras, lights)
 * - Managing loaded model cache
 * - Selecting models for use in ModelNodes
 *
 * The UI is organized into sections:
 * 1. **Available Models** - List of models in project's models/ directory
 * 2. **Loaded Models** - Currently cached models with details
 * 3. **Model Details** - Inspector for selected model
 */
class AssetLibraryUI {
public:
    /**
     * @brief Draw the Asset Library tab content.
     *
     * Call this inside an ImGui::BeginTabItem/EndTabItem block.
     */
    static void Draw();

    /**
     * @brief Set callback for when a model is selected for use.
     *
     * This is called when the user clicks "Use in Node" or similar.
     */
    static void setModelSelectedCallback(ModelSelectedCallback callback);

    /**
     * @brief Get the currently inspected model handle (if any).
     */
    static std::optional<ModelHandle> getSelectedModel();

    /**
     * @brief Select a model for inspection.
     */
    static void selectModel(ModelHandle handle);

    /**
     * @brief Clear selection.
     */
    static void clearSelection();

private:
    // UI state
    static ModelHandle selectedModel_;
    static ModelSelectedCallback onModelSelected_;
    static char searchFilter_[256];
    static bool showOnlyLoaded_;

    // Section drawing
    static void drawToolbar();
    static void drawAvailableModels();
    static void drawLoadedModels();
    static void drawModelDetails();
    static void drawCacheStats();

    // Helpers
    static const char* statusToString(ModelStatus status);
    static ImVec4 statusToColor(ModelStatus status);
    static std::string formatBytes(size_t bytes);
    static std::string formatTimeAgo(std::chrono::system_clock::time_point time);
};
