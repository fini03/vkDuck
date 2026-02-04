#include "editor.h"
#include "io/file_generator.h"
#include "util/logger.h"
#include "io/graph_serializer.h"
#include "external/SimpleFileDialog.h"
#include "graph/fixed_camera_node.h"
#include "graph/fps_camera_node.h"
#include "graph/node_graph.h"
#include "shader/shader_manager.h"
#include "ui/camera_editor_ui.h"
#include "ui/debug_console_ui.h"
#include "ui/light_editor_ui.h"
#include "ui/live_view.h"
#include "ui/pipeline_editor_ui.h"
#include "ui/pipeline_settings_ui.h"
#include "ui/user_messages_ui.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

Editor::Editor(
    VkDevice device,
    VmaAllocator vma,
    uint32_t queueFamilyIndex,
    VkQueue queue
)
    : liveView{
          device,
          vma,
          queueFamilyIndex,
          queue
      } {
    if (!context)
        context = ed::CreateEditor();

    graph = std::make_unique<NodeGraph>();
    file_generator = std::make_unique<FileGenerator>();
    pipeline_state = std::make_unique<PipelineState>();
    shader_manager = std::make_unique<ShaderManager>();
    pipeline_settings_ui = std::make_unique<PipelineSettingsUI>();
}

void Editor::rebuildLiveViewPrimitives() {
    using ed::PinId;
    using primitives::LinkSlot;
    using primitives::StoreHandle;

    try {
        auto& store = liveView.getStore();
        graph->buildDependencies();
        auto sortedNodes = graph->topologicalSort();

        for (auto node : sortedNodes) {
            auto* pipeline = dynamic_cast<PipelineNode*>(node);
            if (pipeline) {
                if (pipeline->shaderReflection.vertexCode.empty() ||
                    pipeline->shaderReflection.fragmentCode.empty()) {
                    Log::error(
                        "LiveView",
                        "Cannot rebuild live view: Pipeline '{}' has invalid/missing shader code. "
                        "Fix shader errors before updating.",
                        pipeline->name
                    );
                    return;
                }
            }
        }

        for (auto node : sortedNodes)
            node->clearPrimitives();
        liveView.destroyOut();
        store.reset();

        std::vector<std::pair<PinId, StoreHandle>> outputs;
        std::vector<std::pair<PinId, LinkSlot>> inputs;
        for (auto node : sortedNodes) {
            node->createPrimitives(store);
            node->getOutputPrimitives(store, outputs);
            node->getInputPrimitives(store, inputs);
        }

        std::unordered_map<PinId, StoreHandle> outputMap{
            outputs.begin(), outputs.end()
        };
        std::unordered_map<PinId, LinkSlot> inputMap{
            inputs.begin(), inputs.end()
        };
        std::vector<std::pair<StoreHandle, LinkSlot>> links;
        links.reserve(graph->links.size());

        for (const auto& link : graph->links) {
            auto itOutput = outputMap.find(link.startPin);
            auto itInput = inputMap.find(link.endPin);

            if (itOutput == outputMap.end()) {
                auto pinInfo = graph->findPin(link.startPin);
                if (pinInfo.pin && pinInfo.node) {
                    Log::warning(
                        "LiveView",
                        "Output pin {} on node {} not mapped by any primitive",
                        pinInfo.pin->label, pinInfo.node->name
                    );
                } else {
                    Log::warning(
                        "LiveView",
                        "Output pin {} not found (stale link after shader reload)",
                        link.startPin.Get()
                    );
                }
                continue;
            }

            if (itInput == inputMap.end()) {
                auto pinInfo = graph->findPin(link.endPin);
                if (pinInfo.pin && pinInfo.node) {
                    Log::warning(
                        "LiveView",
                        "Input pin {} on node {} not mapped by any primitive",
                        pinInfo.pin->label, pinInfo.node->name
                    );
                } else {
                    Log::warning(
                        "LiveView",
                        "Input pin {} not found (stale link after shader reload)",
                        link.endPin.Get()
                    );
                }
                continue;
            }

            links.push_back(
                {itInput->second.handle,
                 {.handle = itOutput->second, .slot = itInput->second.slot}}
            );
        }

        auto sortFn = [](const auto& a, const auto& b) {
            return a.first.handle < b.first.handle &&
                   a.first.type < b.first.type;
        };

        // Stable sort preserves insertion order of links within each node,
        // ensuring descriptor sets and vertex data maintain their pipeline order.
        std::stable_sort(links.begin(), links.end(), sortFn);

        bool connectOk{true};
        auto chunkFn = [](auto a, auto b) {
            return a.first == b.first;
        };
        for (auto&& chunk : links | std::views::chunk_by(chunkFn)) {
            StoreHandle primitive = std::next(chunk.begin(), 0)->first;
            primitives::Node* node = store.getNode(primitive);

            for (const auto& link : chunk) {
                connectOk = node->connectLink(link.second, store);
                if (!connectOk)
                    break;
            }

            if (!connectOk)
                break;
        }

        if (!connectOk) {
            Log::error(
                "LiveView",
                "Not updating live view, could not connect node primitives"
            );
        } else {
            store.link();

            liveView.orderedPrimitives = store.getNodes();
            liveView.outExtent.width = 0;
            liveView.outExtent.height = 0;

            Log::info("LiveView", "Live view data rebuilt");
        }
    } catch (const std::exception& e) {
        Log::error(
            "LiveView",
            "Failed to rebuild live view: {}. Fix the issue and try again.",
            e.what()
        );
    } catch (...) {
        Log::error(
            "LiveView",
            "Unknown error while rebuilding live view. Check shader code and node connections."
        );
    }
}

void Editor::start() {
    if (!projectSelected) {
        askForProjectRoot();
        return;
    }

    static bool showSaveAsPopup = false;
    static char newStateName[128] = "";

    this->update();

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        rebuildLiveViewPrimitives();
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("Load State")) {
                const auto& states = shader_manager->getStates();
                if (states.empty()) {
                    ImGui::TextDisabled("No saved states found");
                } else {
                    for (const auto& state : states) {
                        if (ImGui::MenuItem(state.filename().string().c_str())) {
                            std::filesystem::path loadPath =
                                std::filesystem::path(shader_manager->getProjectRoot()) / state;

                            bool loaded = pipeline_state->load(
                                *graph, loadPath.string(), *shader_manager
                            );

                            if (loaded && !graph->nodes.empty()) {
                                Log::info("Pipeline", "Auto-rebuilding primitives after load...");
                                pendingLoadPositionSync = true;
                                rebuildLiveViewPrimitives();
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Save State")) {
                const auto& states = shader_manager->getStates();
                if (states.empty()) {
                    ImGui::TextDisabled("No saved states to overwrite");
                } else {
                    for (const auto& state : states) {
                        if (ImGui::MenuItem(state.filename().string().c_str())) {
                            pendingSavePositionSync = true;
                            pendingSavePath =
                                std::filesystem::path(shader_manager->getProjectRoot()) / state;
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save State As...")) {
                showSaveAsPopup = true;
                newStateName[0] = '\0';
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Generate Project")) {
                std::filesystem::path outputDir = projectRoot / "generated_files";
                file_generator->generateProject(
                    *graph, liveView.getStore(), outputDir.string()
                );
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
#ifdef __APPLE__
            if (ImGui::MenuItem("Update Live View", "Cmd+R")) {
#else
            if (ImGui::MenuItem("Update Live View", "Ctrl+R")) {
#endif
                rebuildLiveViewPrimitives();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (showSaveAsPopup) {
        ImGui::OpenPopup("Save State As");
    }

    if (ImGui::BeginPopupModal("Save State As", &showSaveAsPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter a name for the new state:");
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("##NewStateName", newStateName, sizeof(newStateName));

        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string name = newStateName;
            if (!name.empty()) {
                // Ensure .json extension
                if (name.size() < 5 || name.substr(name.size() - 5) != ".json") {
                    name += ".json";
                }
                pendingSavePositionSync = true;
                pendingSavePath =
                    std::filesystem::path(shader_manager->getProjectRoot()) / "saved_states" / name;

                showSaveAsPopup = false;
                newStateName[0] = '\0';
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveAsPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginTabBar("MainTabBar")) {
        if (ImGui::BeginTabItem("Graphics Pipeline")) {
            showPipelineView();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Global Settings")) {
            showGlobalSettingsView();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Live View")) {
            showLiveView();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug Console")) {
            DebugConsoleUI::Draw();
            ImGui::EndTabItem();
        }

        // Show unread count in tab name if there are issues
        size_t unreadCount =
            Logger::instance().getUnreadWarningErrorCount();
        std::string messagesTabName =
            unreadCount > 0
                ? std::format(
                      "Messages ({})###MessagesTab", unreadCount
                  )
                : "Messages###MessagesTab";

        if (ImGui::BeginTabItem(messagesTabName.c_str())) {
            UserMessagesUI::Draw();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

}

void Editor::renderPopupNotifications() {
    auto popups = Logger::instance().consumePopups();

    for (const auto& popup : popups) {
        ImVec4 color;
        const char* title;
        if (popup.level == LogLevel::Error) {
            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            title = "Error";
        } else {
            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            title = "Warning";
        }

        std::string popupId = std::format(
            "##popup_{}_{}", popup.category, popup.message.substr(0, 20)
        );

        ImGui::OpenPopup(popupId.c_str());

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(
            center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)
        );

        if (ImGui::BeginPopupModal(
                popupId.c_str(), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoTitleBar
            )) {

            ImGui::TextColored(color, "%s", title);
            if (!popup.category.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[%s]",
                    popup.category.c_str()
                );
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Message
            ImGui::TextWrapped("%s", popup.message.c_str());

            ImGui::Spacing();
            ImGui::Separator();

            float buttonWidth = 120.0f;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}

void Editor::update() {}

void Editor::showGlobalSettingsView() {
    ImGui::TextColored(
        ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Global Scene Overrides"
    );
    ImGui::TextDisabled(
        "Configure settings here to share across multiple pipelines."
    );
    ImGui::TextDisabled("Pipelines use their own settings by default.");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(
            "Project Settings", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        ImGui::TextColored(
            ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Project Root Folder"
        );

        std::string currentRoot = shader_manager->getProjectRoot();

        if (currentRoot.empty() || !projectSelected) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "No project folder selected"
            );
        } else {
            ImGui::TextWrapped("Current path: %s", currentRoot.c_str());
        }

        ImGui::Spacing();

        ImGui::PushStyleColor(
            ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.8f, 1.0f)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.9f, 1.0f)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.75f, 1.0f)
        );

        if (ImGui::Button(
                projectSelected ? "Change Project Folder"
                                : "Select Project Folder"
            )) {
            std::string root = cr::utils::FileDialogs::SelectDirectory(
                "Select Project Root"
            );

            if (!root.empty()) {
                projectRoot = root;
                shader_manager->setProjectRoot(root);
                Logger::instance().setProjectRoot(root);
                shader_manager->scanShaders();
                projectSelected = true;
            }
        }

        ImGui::PopStyleColor(3);

        if (projectSelected && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Change the root folder for shader and asset discovery"
            );
        }

        ImGui::Spacing();
        ImGui::Separator();
    }
}

void Editor::askForProjectRoot() {
    ImGui::OpenPopup("Select Project Folder");

    if (ImGui::BeginPopupModal(
            "Select Project Folder", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {

        ImGui::Text("Select the root folder of your project");
        ImGui::Spacing();

        if (ImGui::Button("Browse...", ImVec2(200, 0))) {
            std::string root = cr::utils::FileDialogs::SelectDirectory(
                "Select Project Root"
            );

            if (!root.empty()) {
                projectRoot = root;
                shader_manager->setProjectRoot(root);
                Logger::instance().setProjectRoot(root);
                projectSelected = true;

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void Editor::showPipelineView() {
    static PipelineEditorUI pipelineEditor(*graph);

    if (pendingSavePositionSync) {
        pipelineEditor.SyncNodePositionsFromEditor();
        pipeline_state->save(*graph, pendingSavePath.string());
        shader_manager->scanStates();
        pendingSavePositionSync = false;
        pendingSavePath.clear();
    }

    if (pendingLoadPositionSync) {
        pipelineEditor.ClearSelection();
        pipelineEditor.ApplyNodePositionsToEditor();
        pendingLoadPositionSync = false;
    }

    shader_manager->processPendingReloads(*graph);
    pipelineEditor.draw(
        shader_manager.get(), pipeline_settings_ui.get()
    );
}

void Editor::showLiveView() {
    const auto contentRegion = ImGui::GetContentRegionAvail();
    liveView.render(contentRegion.x, contentRegion.y);

    VkDescriptorSet imageDS = liveView.getImage();
    if (imageDS != VK_NULL_HANDLE) {
        ImGui::Image((ImTextureID)imageDS, ImGui::GetContentRegionAvail());
    } else {
        ImGui::TextDisabled("Live view not available - check pipeline configuration");
    }

    auto now = std::chrono::high_resolution_clock::now();
    float deltaTime =
        std::chrono::duration<float>(now - lastFrameTime).count();
    lastFrameTime = now;

    CameraNodeBase* camera = findFirstCameraNode();
    if (camera && ImGui::IsWindowHovered()) {
        handleLiveViewInput(camera, deltaTime);
    }
}

CameraNodeBase* Editor::findFirstCameraNode() {
    for (auto& nodePtr : graph->nodes) {
        if (auto* camera = dynamic_cast<CameraNodeBase*>(nodePtr.get())) {
            if (dynamic_cast<FixedCameraNode*>(nodePtr.get()) != nullptr) {
                continue;
            }
            return camera;
        }
    }
    for (auto& nodePtr : graph->nodes) {
        if (auto* camera = dynamic_cast<CameraNodeBase*>(nodePtr.get())) {
            return camera;
        }
    }
    return nullptr;
}

void Editor::handleLiveViewInput(
    CameraNodeBase* camera,
    float deltaTime
) {
    ImGuiIO& io = ImGui::GetIO();

    bool forward = ImGui::IsKeyDown(ImGuiKey_W) ||
                   ImGui::IsKeyDown(ImGuiKey_UpArrow);
    bool backward = ImGui::IsKeyDown(ImGuiKey_S) ||
                    ImGui::IsKeyDown(ImGuiKey_DownArrow);
    bool left = ImGui::IsKeyDown(ImGuiKey_A) ||
                ImGui::IsKeyDown(ImGuiKey_LeftArrow);
    bool right = ImGui::IsKeyDown(ImGuiKey_D) ||
                 ImGui::IsKeyDown(ImGuiKey_RightArrow);

    if (forward || backward || left || right)
        camera->processKeyboard(
            deltaTime, forward, backward, left, right, false, false
        );

    ImVec2 mousePos = io.MousePos;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (!isDragging) {
            isDragging = true;
            lastMousePos = mousePos;
        } else {
            float deltaX = mousePos.x - lastMousePos.x;
            float deltaY = mousePos.y - lastMousePos.y;
            if (deltaX != 0 || deltaY != 0)
                camera->processMouseDrag(deltaX, deltaY);
            lastMousePos = mousePos;
        }
    } else {
        isDragging = false;
    }

    float scroll = io.MouseWheel;
    if (scroll != 0)
        camera->processScroll(scroll);
}
