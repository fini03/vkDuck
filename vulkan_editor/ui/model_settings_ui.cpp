#include "model_settings_ui.h"
#include "../graph/model_node.h"
#include "../graph/node_graph.h"
#include "../graph/default_renderer.h"
#include "../shader/shader_manager.h"
#include <glm/gtc/constants.hpp>
#include <imgui.h>

namespace fs = std::filesystem;

void ModelSettingsUI::Draw(ModelNode* modelNode, ShaderManager* shaderManager, NodeGraph* graph) {
    if (!modelNode) {
        ImGui::TextWrapped("No model node selected.");
        return;
    }

    ImGui::Text("Model Node Settings");
    ImGui::Separator();

    // Model info (read-only, selection happens in Asset Library tab)
    const CachedModel* cached = modelNode->getCachedModel();

    if (modelNode->settings.modelPath[0] == '\0') {
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.4f, 1.0f),
            "No model assigned"
        );
        ImGui::TextWrapped(
            "Use the Asset Library tab to load and assign a model to this node."
        );
    } else if (modelNode->hasModel() && cached) {
        // Model is loaded - show info
        ImGui::TextColored(
            ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
            "Model: %s",
            cached->displayName.c_str()
        );

        ImGui::TextColored(
            ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Path: %s",
            modelNode->settings.modelPath
        );

        // Model stats
        ImGui::Spacing();
        ImGui::Text(
            "Vertices: %zu  |  Indices: %zu  |  Meshes: %zu",
            cached->modelData.getTotalVertexCount(),
            cached->modelData.getTotalIndexCount(),
            cached->modelData.getGeometryCount()
        );

        if (!cached->cameras.empty() || !cached->lights.empty()) {
            ImGui::Text(
                "Cameras: %zu  |  Lights: %zu",
                cached->cameras.size(),
                cached->lights.size()
            );
        }

        // Create Default Renderer button
        if (graph && shaderManager) {
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
    } else {
        // Model path set but not loaded
        ModelHandle handle = modelNode->getModelHandle();
        ModelStatus status = g_modelManager ? g_modelManager->getStatus(handle) : ModelStatus::NotLoaded;

        if (status == ModelStatus::Loading) {
            ImGui::TextColored(
                ImVec4(0.8f, 0.8f, 0.4f, 1.0f),
                "Loading: %s",
                modelNode->settings.modelPath
            );
        } else if (status == ModelStatus::Error) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
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
    }

    ImGui::Spacing();

    // GLTF Cameras dropdown (only show if model has cameras)
    if (cached && !cached->cameras.empty()) {
        ImGui::Separator();
        ImGui::Text("GLTF Cameras");

        // Build camera names for combo box
        std::vector<const char*> cameraNames;
        cameraNames.push_back("None (Default)");
        for (const auto& cam : cached->cameras) {
            cameraNames.push_back(cam.name.c_str());
        }

        // selectedCameraIndex: -1 = none, 0+ = GLTF camera index
        int comboIndex = modelNode->selectedCameraIndex + 1;
        if (ImGui::Combo(
                "Selected Camera", &comboIndex, cameraNames.data(),
                static_cast<int>(cameraNames.size())
            )) {
            modelNode->selectedCameraIndex = comboIndex - 1;
            modelNode->updateCameraFromSelection();
        }

        // Show selected camera info
        if (modelNode->selectedCameraIndex >= 0 &&
            modelNode->selectedCameraIndex < static_cast<int>(cached->cameras.size())) {
            const auto& cam = cached->cameras[modelNode->selectedCameraIndex];
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

    // GLTF Lights info (only show if model has lights)
    if (cached && !cached->lights.empty()) {
        ImGui::Separator();
        ImGui::Text("GLTF Lights (%zu total)", cached->lights.size());

        // Build light names for combo box (for viewing details)
        std::vector<const char*> lightNames;
        for (const auto& light : cached->lights) {
            lightNames.push_back(light.name.c_str());
        }

        // selectedLightIndex is used to view individual light details
        if (modelNode->selectedLightIndex < 0 && !lightNames.empty()) {
            modelNode->selectedLightIndex = 0;
        }
        ImGui::Combo(
            "Inspect Light", &modelNode->selectedLightIndex, lightNames.data(),
            static_cast<int>(lightNames.size())
        );

        // Show inspected light info
        if (modelNode->selectedLightIndex >= 0 &&
            modelNode->selectedLightIndex < static_cast<int>(cached->lights.size())) {
            const auto& light = cached->lights[modelNode->selectedLightIndex];
            ImGui::TextColored(
                ImVec4(0.9f, 0.9f, 0.7f, 1.0f), "Light: %s",
                light.name.c_str()
            );

            // Light type
            const char* typeStr = "Unknown";
            switch (light.type) {
                case GLTFLightType::Directional: typeStr = "Directional"; break;
                case GLTFLightType::Point: typeStr = "Point"; break;
                case GLTFLightType::Spot: typeStr = "Spot"; break;
            }
            ImGui::Text("Type: %s", typeStr);

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "GLTF Values:");
            ImGui::Text(
                "Position: (%.2f, %.2f, %.2f)",
                light.position.x, light.position.y, light.position.z
            );
            ImGui::Text(
                "Direction: (%.2f, %.2f, %.2f)",
                light.direction.x, light.direction.y, light.direction.z
            );
            ImGui::Text(
                "Color: (%.2f, %.2f, %.2f)",
                light.color.r, light.color.g, light.color.b
            );
            ImGui::Text("Intensity: %.2f", light.intensity);

            if (light.type == GLTFLightType::Point || light.type == GLTFLightType::Spot) {
                if (light.range > 0.0f) {
                    ImGui::Text("Range: %.2f", light.range);
                } else {
                    ImGui::Text("Range: Infinite");
                }
            }

            if (light.type == GLTFLightType::Spot) {
                ImGui::Text(
                    "Inner Cone: %.1f deg, Outer Cone: %.1f deg",
                    glm::degrees(light.innerConeAngle),
                    glm::degrees(light.outerConeAngle)
                );
            }
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
