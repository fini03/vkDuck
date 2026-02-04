#pragma once

#include "io/file_generator.h"
#include "io/graph_serializer.h"
#include "imgui.h"
#include "imgui_node_editor.h"
#include "graph/camera_node.h"
#include "graph/node_graph.h"
#include "graph/pipeline_node.h"
#include "shader/shader_manager.h"
#include "ui/live_view.h"
#include "ui/pipeline_settings_ui.h"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

/**
 * @class Editor
 * @brief Main application coordinator for the Vulkan pipeline visual editor.
 *
 * Manages the interaction between the node graph UI, live GPU preview,
 * shader compilation, and C++ code generation systems. Provides a tab-based
 * interface for pipeline editing, global settings, live preview, and debugging.
 *
 * Key responsibilities:
 * - Project selection and management
 * - Tab-based view rendering (Pipeline, Settings, Live View, Console)
 * - Keyboard shortcuts (Ctrl/Cmd+R for live view refresh)
 * - Coordinating save/load operations with position synchronization
 */
class Editor {
public:
    Editor(
        VkDevice device,
        VmaAllocator vma,
        uint32_t queueFamilyIndex,
        VkQueue queue
    );
    void start();
    void cleanup() {
        liveView.~LiveView();
    }

private:
    void rebuildLiveViewPrimitives();
    void update();
    void showGlobalSettingsView();
    void showPipelineView();
    void showLiveView();
    void askForProjectRoot();
    void renderPopupNotifications();

    ed::EditorContext* context = nullptr;

    std::filesystem::path projectRoot;
    bool projectSelected = false;

    bool pendingLoadPositionSync = false;
    bool pendingSavePositionSync = false;
    std::filesystem::path pendingSavePath;

    std::unique_ptr<NodeGraph> graph;
    std::unique_ptr<FileGenerator> file_generator;
    std::unique_ptr<PipelineState> pipeline_state;
    std::unique_ptr<ShaderManager> shader_manager;
    std::unique_ptr<PipelineSettingsUI> pipeline_settings_ui;
    LiveView liveView;

    std::chrono::high_resolution_clock::time_point lastFrameTime{
        std::chrono::high_resolution_clock::now()
    };
    ImVec2 lastMousePos{0, 0};
    bool isDragging{false};
    CameraNodeBase* findFirstCameraNode();
    void handleLiveViewInput(
        CameraNodeBase* camera,
        float deltaTime
    );
};