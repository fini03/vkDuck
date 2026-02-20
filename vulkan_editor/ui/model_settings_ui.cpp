#include "model_settings_ui.h"
#include "../graph/model_node.h"
#include "../graph/node_graph.h"
#include "../graph/default_renderer.h"
#include "../shader/shader_manager.h"
#include <imgui.h>

namespace fs = std::filesystem;

void ModelSettingsUI::Draw(ModelNode* modelNode, ShaderManager* shaderManager, NodeGraph* graph) {
    if (!modelNode) {
        ImGui::TextWrapped("No model node selected.");
        return;
    }

    ImGui::Text("Model Node Settings");
    ImGui::Separator();

    // Model Path - use dropdown from models/ folder
    ImGui::Text("Model:");

    if (shaderManager) {
        fs::path currentModelPath = modelNode->settings.modelPath;

        if (shaderManager->showModelPicker("##ModelPicker", currentModelPath)) {
            // Model was selected from dropdown - store relative path
            strncpy(
                modelNode->settings.modelPath, currentModelPath.generic_string().c_str(),
                sizeof(modelNode->settings.modelPath) - 1
            );
            modelNode->settings.modelPath[sizeof(modelNode->settings.modelPath) - 1] = '\0';

            // Load the model using project root + relative path
            fs::path projectRoot = shaderManager->getProjectRoot();
            fs::path absolutePath = projectRoot / currentModelPath;
            modelNode->loadModel(absolutePath, projectRoot);
        }

        // Show current loading state
        if (modelNode->settings.modelPath[0] != '\0') {
            auto loadingState = modelNode->getLoadingState();
            if (loadingState == ModelFileWatcher::LoadingState::Loaded) {
                ImGui::TextColored(
                    ImVec4(0.6f, 0.8f, 0.6f, 1.0f),
                    "Loaded: %s",
                    modelNode->settings.modelPath
                );
            } else if (loadingState == ModelFileWatcher::LoadingState::Loading) {
                ImGui::TextColored(
                    ImVec4(0.8f, 0.8f, 0.4f, 1.0f),
                    "Loading: %s",
                    modelNode->settings.modelPath
                );
            } else if (loadingState == ModelFileWatcher::LoadingState::Error) {
                ImGui::TextColored(
                    ImVec4(0.8f, 0.4f, 0.4f, 1.0f),
                    "Error loading: %s",
                    modelNode->settings.modelPath
                );
            } else {
                ImGui::TextColored(
                    ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    "Path: %s (not loaded)",
                    modelNode->settings.modelPath
                );
            }

            // Create Default Renderer button
            if (graph) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Quick Setup");
                ImGui::TextWrapped("Create a basic Phong rendering setup for this model with camera, light, and output.");

                if (ImGui::Button("Create Default Renderer", ImVec2(-1, 0))) {
                    fs::path projectRoot = shaderManager->getProjectRoot();
                    bool success = DefaultRendererSetup::createForModel(
                        *graph, modelNode, *shaderManager, projectRoot
                    );
                    if (success) {
                        ImGui::OpenPopup("DefaultRendererCreated");
                    } else {
                        ImGui::OpenPopup("DefaultRendererFailed");
                    }
                }

                // Success popup
                if (ImGui::BeginPopupModal("DefaultRendererCreated", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Default renderer created successfully!");
                    ImGui::TextWrapped("Camera, Light, Pipeline, and Present nodes have been added and connected.");
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Failure popup
                if (ImGui::BeginPopupModal("DefaultRendererFailed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to create default renderer.");
                    ImGui::TextWrapped("Check if default_phong shaders exist in project/shaders/");
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No project selected");
    }

    ImGui::Spacing();

    // GLTF Cameras dropdown (only show if model has cameras)
    if (!modelNode->gltfCameras.empty()) {
        ImGui::Separator();
        ImGui::Text("GLTF Cameras");

        // Build camera names for combo box
        std::vector<const char*> cameraNames;
        cameraNames.push_back("None (Default)");
        for (const auto& cam : modelNode->gltfCameras) {
            cameraNames.push_back(cam.name.c_str());
        }

        // selectedCameraIndex: -1 = none, 0+ = GLTF camera index
        int comboIndex = modelNode->selectedCameraIndex + 1;
        if (ImGui::Combo(
                "Selected Camera", &comboIndex, cameraNames.data(),
                static_cast<int>(cameraNames.size())
            )) {
            modelNode->selectedCameraIndex = comboIndex - 1;
            // Update camera matrices when selection changes
            modelNode->updateCameraFromSelection();
        }

        // Show selected camera info
        if (modelNode->selectedCameraIndex >= 0) {
            const auto& cam =
                modelNode->gltfCameras[modelNode->selectedCameraIndex];
            ImGui::TextColored(
                ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Camera: %s",
                cam.name.c_str()
            );
            ImGui::Text(
                "Type: %s",
                cam.isPerspective ? "Perspective" : "Orthographic"
            );

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.9f, 1.0f), "GLTF Values:");
            ImGui::Text(
                "Position: (%.2f, %.2f, %.2f)",
                cam.position.x, cam.position.y, cam.position.z
            );
            if (cam.isPerspective) {
                ImGui::Text("FOV: %.1f degrees", cam.fov);
                if (cam.aspectRatio > 0.0f) {
                    ImGui::Text("Aspect Ratio: %.2f", cam.aspectRatio);
                }
            } else {
                ImGui::Text("X Mag: %.2f, Y Mag: %.2f", cam.xmag, cam.ymag);
            }
            ImGui::Text("Near: %.3f, Far: %.1f", cam.nearPlane, cam.farPlane);
        }

        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::Text("Input Assembly");
    ImGui::Combo(
        "Vertex Topology", &modelNode->settings.topology,
        ModelNode::topologyOptions.data(),
        static_cast<int>(ModelNode::topologyOptions.size())
    );
    ImGui::Checkbox(
        "Primitive Restart", &modelNode->settings.primitiveRestart
    );

    ImGui::Spacing();
}
