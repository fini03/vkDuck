#include "asset_library_ui.h"
#include "vulkan_editor/util/logger.h"
#include <algorithm>
#include <cstring>
#include <format>

namespace {
constexpr const char* LOG_CATEGORY = "AssetLibrary";

// Layout constants
constexpr float LEFT_PANEL_WIDTH = 300.0f;

const char* lightTypeToString(GLTFLightType type) {
    switch (type) {
        case GLTFLightType::Directional: return "Directional";
        case GLTFLightType::Point:       return "Point";
        case GLTFLightType::Spot:        return "Spot";
    }
    return "Unknown";
}

// Colors
const ImVec4 COLOR_LOADED = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
const ImVec4 COLOR_LOADING = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
const ImVec4 COLOR_ERROR = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
const ImVec4 COLOR_NOT_LOADED = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
const ImVec4 COLOR_HEADER = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
const ImVec4 COLOR_STAT = ImVec4(0.8f, 0.8f, 0.3f, 1.0f);

}  // namespace

// Static member definitions
ModelHandle AssetLibraryUI::selectedModel_{};
ModelSelectedCallback AssetLibraryUI::onModelSelected_{};
char AssetLibraryUI::searchFilter_[256] = "";
bool AssetLibraryUI::showOnlyLoaded_ = false;

void AssetLibraryUI::Draw() {
    auto& manager = *g_modelManager;

    // Validate selected model - clear if it no longer exists or has error status
    if (selectedModel_.isValid()) {
        const CachedModel* model = manager.getModel(selectedModel_);
        if (!model || model->status == ModelStatus::Error) {
            selectedModel_ = {};
        }
    }

    // Main layout: two-column with splitter
    ImGui::BeginChild("AssetLibraryMain", ImVec2(0, 0), false);

    // Left panel - model list
    ImGui::BeginChild("ModelList", ImVec2(LEFT_PANEL_WIDTH, 0), true);

    drawToolbar();
    ImGui::Separator();

    // Tabs for Available vs Loaded
    if (ImGui::BeginTabBar("ModelListTabs")) {
        if (ImGui::BeginTabItem("Available")) {
            drawAvailableModels();
            ImGui::EndTabItem();
        }

        size_t loadedCount = manager.getLoadedModelCount();
        std::string loadedTabName = loadedCount > 0
            ? std::format("Loaded ({})###LoadedTab", loadedCount)
            : "Loaded###LoadedTab";

        if (ImGui::BeginTabItem(loadedTabName.c_str())) {
            drawLoadedModels();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel - model details
    ImGui::BeginChild("ModelDetails", ImVec2(0, 0), true);

    if (selectedModel_.isValid()) {
        drawModelDetails();
    } else {
        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "Select a model to view details"
        );
    }

    ImGui::EndChild();

    ImGui::EndChild();
}

void AssetLibraryUI::drawToolbar() {
    auto& manager = *g_modelManager;

    // Search filter
    ImGui::SetNextItemWidth(LEFT_PANEL_WIDTH - 80);
    ImGui::InputTextWithHint(
        "##Search",
        "Search models...",
        searchFilter_,
        sizeof(searchFilter_)
    );

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button("Refresh")) {
        manager.scanModels();
        Log::info(LOG_CATEGORY, "Rescanned models directory");
    }

    // Cache stats in a compact row
    drawCacheStats();
}

void AssetLibraryUI::drawCacheStats() {
    auto& manager = *g_modelManager;

    size_t loadedCount = manager.getLoadedModelCount();
    size_t memoryUsage = manager.getTotalMemoryUsage();

    ImGui::TextColored(
        COLOR_STAT,
        "%zu loaded | %s",
        loadedCount,
        formatBytes(memoryUsage).c_str()
    );

    ImGui::SameLine();

    // Unload unused button (only if there are loaded models)
    if (loadedCount > 0) {
        if (ImGui::SmallButton("Clear Unused")) {
            manager.unloadUnusedModels();
        }
    }
}

void AssetLibraryUI::drawAvailableModels() {
    auto& manager = *g_modelManager;
    auto availableModels = manager.getAvailableModels();

    if (availableModels.empty()) {
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.3f, 1.0f),
            "No models found in project.\n\n"
            "Place .gltf, .glb, or .obj files in:\n"
            "  <project>/models/"
        );
        return;
    }

    // Filter models by search
    std::vector<std::filesystem::path> filteredModels;
    std::string filter = searchFilter_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    for (const auto& path : availableModels) {
        std::string name = path.filename().string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (filter.empty() || name.find(filter) != std::string::npos) {
            filteredModels.push_back(path);
        }
    }

    if (filteredModels.empty()) {
        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No models match filter"
        );
        return;
    }

    // Display models
    for (size_t i = 0; i < filteredModels.size(); ++i) {
        const auto& path = filteredModels[i];
        std::string displayName = path.filename().string();

        // Use path hash as unique ID to avoid conflicts with same filenames
        ImGui::PushID(static_cast<int>(std::hash<std::string>{}(path.string())));

        // Check if already loaded
        const CachedModel* cached = manager.getModelByPath(path);
        ModelStatus status = cached ? cached->status : ModelStatus::NotLoaded;

        // Status indicator
        ImGui::PushStyleColor(ImGuiCol_Text, statusToColor(status));
        ImGui::Text("%s", statusToString(status));
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // Selectable model name
        bool isSelected = selectedModel_.isValid() &&
                          cached && cached->handle == selectedModel_;

        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
            if (cached && cached->status == ModelStatus::Loaded) {
                selectedModel_ = cached->handle;
            } else {
                // Load and select
                ModelHandle handle = manager.loadModel(path);
                if (manager.isLoaded(handle)) {
                    selectedModel_ = handle;
                }
            }
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (status == ModelStatus::Loaded) {
                if (ImGui::MenuItem("Reload")) {
                    if (cached) {
                        manager.reloadModel(cached->handle);
                    }
                }
                if (ImGui::MenuItem("Unload")) {
                    if (cached) {
                        manager.unloadModel(cached->handle);
                        if (selectedModel_ == cached->handle) {
                            selectedModel_ = {};
                        }
                    }
                }
            } else if (status == ModelStatus::NotLoaded) {
                if (ImGui::MenuItem("Load")) {
                    ModelHandle handle = manager.loadModel(path);
                    if (manager.isLoaded(handle)) {
                        selectedModel_ = handle;
                    }
                }
            }

            ImGui::EndPopup();
        }

        // Tooltip with full path
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path.string().c_str());
        }

        ImGui::PopID();
    }
}

void AssetLibraryUI::drawLoadedModels() {
    auto& manager = *g_modelManager;
    auto handles = manager.getAllModels();

    // Filter to only loaded models
    std::vector<const CachedModel*> loadedModels;
    for (ModelHandle handle : handles) {
        if (const CachedModel* model = manager.getModel(handle)) {
            loadedModels.push_back(model);
        }
    }

    if (loadedModels.empty()) {
        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No models loaded.\n\n"
            "Click a model in 'Available' tab to load it."
        );
        return;
    }

    for (const CachedModel* model : loadedModels) {
        ImGui::PushID(static_cast<int>(model->handle.id));

        bool isSelected = selectedModel_ == model->handle;

        // Compact info row
        std::string label = std::format(
            "{} ({} refs)",
            model->displayName,
            model->referenceCount
        );

        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedModel_ = model->handle;
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Reload")) {
                manager.reloadModel(model->handle);
            }

            bool canUnload = model->referenceCount == 0;
            if (ImGui::MenuItem("Unload", nullptr, false, canUnload)) {
                ModelHandle handleToUnload = model->handle;
                if (selectedModel_ == handleToUnload) {
                    selectedModel_ = {};
                }
                manager.unloadModel(handleToUnload);
            }

            if (!canUnload) {
                ImGui::TextColored(
                    ImVec4(0.7f, 0.7f, 0.3f, 1.0f),
                    "In use by %zu node(s)",
                    model->referenceCount
                );
            }

            ImGui::EndPopup();
        }

        // Tooltip with stats
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Vertices: %zu", model->modelData.getTotalVertexCount());
            ImGui::Text("Indices: %zu", model->modelData.getTotalIndexCount());
            ImGui::Text("Geometries: %zu", model->modelData.getGeometryCount());
            ImGui::Text("Memory: %s", formatBytes(model->memoryUsageBytes).c_str());
            ImGui::Text("Loaded: %s", formatTimeAgo(model->loadedAt).c_str());
            ImGui::EndTooltip();
        }

        ImGui::PopID();
    }
}

void AssetLibraryUI::drawModelDetails() {
    auto& manager = *g_modelManager;
    const CachedModel* model = manager.getModel(selectedModel_);

    if (!model) {
        ImGui::TextColored(
            ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
            "Model not found or not loaded"
        );
        return;
    }

    // Header
    ImGui::TextColored(COLOR_HEADER, "%s", model->displayName.c_str());
    ImGui::SameLine();
    ImGui::TextColored(statusToColor(model->status), "[%s]", statusToString(model->status));

    ImGui::Separator();

    // Actions
    if (ImGui::Button("Reload")) {
        manager.reloadModel(selectedModel_);
    }
    ImGui::SameLine();

    bool canUnload = model->referenceCount == 0;
    ImGui::BeginDisabled(!canUnload);
    if (ImGui::Button("Unload")) {
        manager.unloadModel(selectedModel_);
        selectedModel_ = {};
        ImGui::EndDisabled();
        return;  // Model is gone
    }
    ImGui::EndDisabled();

    if (!canUnload) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.3f, 1.0f),
            "(In use by %zu node%s)",
            model->referenceCount,
            model->referenceCount == 1 ? "" : "s"
        );
    }

    // Use in node callback
    if (onModelSelected_) {
        ImGui::SameLine();
        if (ImGui::Button("Use in Node")) {
            onModelSelected_(selectedModel_);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Geometry section
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        ImGui::Text("Vertices:   %zu", model->modelData.getTotalVertexCount());
        ImGui::Text("Indices:    %zu", model->modelData.getTotalIndexCount());
        ImGui::Text("Geometries: %zu", model->modelData.getGeometryCount());

        if (!model->modelData.ranges.empty()) {
            ImGui::Spacing();
            if (ImGui::TreeNode("##GeometryRanges", "Geometry Ranges")) {
                for (size_t i = 0; i < model->modelData.ranges.size(); ++i) {
                    const auto& range = model->modelData.ranges[i];
                    ImGui::BulletText(
                        "[%zu] %u verts, %u indices (mat %d)",
                        i, range.vertexCount, range.indexCount, range.materialIndex
                    );
                }
                ImGui::TreePop();
            }
        }

        ImGui::Unindent();
    }

    // Textures section
    if (!model->images.empty()) {
        if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            ImGui::Text("%zu texture(s)", model->images.size());

            for (size_t i = 0; i < model->images.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto& img = model->images[i];
                std::string filename = img.path.filename().string();

                bool loaded = img.pixels != nullptr;
                ImVec4 color = loaded ? COLOR_LOADED : COLOR_ERROR;

                ImGui::TextColored(color, "%s", loaded ? "OK" : "FAIL");
                ImGui::SameLine();
                ImGui::Text("[%zu] %s", i, filename.c_str());

                if (loaded) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        "(%ux%u)",
                        img.width, img.height
                    );
                }
                ImGui::PopID();
            }

            ImGui::Unindent();
        }
    }

    // Cameras section
    if (!model->cameras.empty()) {
        std::string header = std::format("Cameras ({})", model->cameras.size());
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Indent();

            for (size_t i = 0; i < model->cameras.size(); ++i) {
                const auto& cam = model->cameras[i];

                std::string camLabel = cam.name.empty()
                    ? std::format("Camera {}", i)
                    : cam.name;

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("##camera", "%s", camLabel.c_str())) {
                    ImGui::Text(
                        "Type: %s",
                        cam.isPerspective ? "Perspective" : "Orthographic"
                    );
                    ImGui::Text(
                        "Position: (%.2f, %.2f, %.2f)",
                        cam.position.x, cam.position.y, cam.position.z
                    );

                    if (cam.isPerspective) {
                        ImGui::Text("FOV: %.1f deg", cam.fov);
                        ImGui::Text("Aspect: %.2f", cam.aspectRatio);
                    } else {
                        ImGui::Text("X Mag: %.2f", cam.xmag);
                        ImGui::Text("Y Mag: %.2f", cam.ymag);
                    }

                    ImGui::Text("Near: %.3f, Far: %.1f", cam.nearPlane, cam.farPlane);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::Unindent();
        }
    }

    // Lights section
    if (!model->lights.empty()) {
        std::string header = std::format("Lights ({})", model->lights.size());
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Indent();

            for (size_t i = 0; i < model->lights.size(); ++i) {
                const auto& light = model->lights[i];

                std::string lightLabel = light.name.empty()
                    ? std::format("Light {}", i)
                    : light.name;

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("##light", "%s", lightLabel.c_str())) {
                    ImGui::Text("Type: %s", lightTypeToString(light.type));
                    ImGui::Text(
                        "Position: (%.2f, %.2f, %.2f)",
                        light.position.x, light.position.y, light.position.z
                    );
                    ImGui::Text(
                        "Color: (%.2f, %.2f, %.2f)",
                        light.color.r, light.color.g, light.color.b
                    );
                    ImGui::Text("Intensity: %.2f", light.intensity);

                    if (light.range > 0.0f) {
                        ImGui::Text("Range: %.2f", light.range);
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::Unindent();
        }
    }

    // File info section
    if (ImGui::CollapsingHeader("File Info")) {
        ImGui::Indent();

        ImGui::Text("Path: %s", model->path.string().c_str());
        ImGui::Text("Memory: %s", formatBytes(model->memoryUsageBytes).c_str());
        ImGui::Text("Loaded: %s", formatTimeAgo(model->loadedAt).c_str());
        ImGui::Text("References: %zu", model->referenceCount);

        ImGui::Spacing();

        // Auto-reload toggle
        // Note: We need non-const access for this
        if (CachedModel* mutableModel = const_cast<CachedModel*>(model)) {
            if (ImGui::Checkbox("Auto-reload on file change", &mutableModel->autoReload)) {
                Log::info(
                    LOG_CATEGORY,
                    "Auto-reload {} for '{}'",
                    mutableModel->autoReload ? "enabled" : "disabled",
                    model->displayName
                );
            }
        }

        ImGui::Unindent();
    }
}

void AssetLibraryUI::setModelSelectedCallback(ModelSelectedCallback callback) {
    onModelSelected_ = std::move(callback);
}

std::optional<ModelHandle> AssetLibraryUI::getSelectedModel() {
    if (selectedModel_.isValid()) {
        return selectedModel_;
    }
    return std::nullopt;
}

void AssetLibraryUI::selectModel(ModelHandle handle) {
    selectedModel_ = handle;
}

void AssetLibraryUI::clearSelection() {
    selectedModel_ = {};
}

const char* AssetLibraryUI::statusToString(ModelStatus status) {
    switch (status) {
        case ModelStatus::NotLoaded: return "-";
        case ModelStatus::Loading:   return "...";
        case ModelStatus::Loaded:    return "OK";
        case ModelStatus::Error:     return "ERR";
    }
    return "?";
}

ImVec4 AssetLibraryUI::statusToColor(ModelStatus status) {
    switch (status) {
        case ModelStatus::NotLoaded: return COLOR_NOT_LOADED;
        case ModelStatus::Loading:   return COLOR_LOADING;
        case ModelStatus::Loaded:    return COLOR_LOADED;
        case ModelStatus::Error:     return COLOR_ERROR;
    }
    return COLOR_NOT_LOADED;
}

std::string AssetLibraryUI::formatBytes(size_t bytes) {
    constexpr size_t KB = 1024;
    constexpr size_t MB = KB * 1024;
    constexpr size_t GB = MB * 1024;

    if (bytes >= GB) {
        return std::format("{:.2f} GB", static_cast<double>(bytes) / GB);
    } else if (bytes >= MB) {
        return std::format("{:.1f} MB", static_cast<double>(bytes) / MB);
    } else if (bytes >= KB) {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / KB);
    } else {
        return std::format("{} B", bytes);
    }
}

std::string AssetLibraryUI::formatTimeAgo(std::chrono::system_clock::time_point time) {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - time);

    if (diff.count() < 60) {
        return "just now";
    } else if (diff.count() < 3600) {
        return std::format("{}m ago", diff.count() / 60);
    } else if (diff.count() < 86400) {
        return std::format("{}h ago", diff.count() / 3600);
    } else {
        return std::format("{}d ago", diff.count() / 86400);
    }
}
