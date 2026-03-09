#include "model_settings_ui.h"
#include "../asset/model_manager.h"
#include "../graph/model_node_base.h"
#include "../graph/vertex_data_node.h"
#include "../graph/ubo_node.h"
#include "../graph/material_node.h"
#include "../graph/node_graph.h"
#include "../shader/shader_manager.h"
#include <glm/gtc/constants.hpp>
#include <imgui.h>

namespace fs = std::filesystem;

void ModelSettingsUI::Draw(
    ModelNodeBase* node,
    ShaderManager* shaderManager,
    NodeGraph* graph
) {
    if (!node) {
        ImGui::TextWrapped("No model node selected.");
        return;
    }

    // Draw common model info
    DrawModelInfo(node);

    // Dispatch to specific node type UI
    if (auto* vertexData = dynamic_cast<VertexDataNode*>(node)) {
        DrawVertexDataSettings(vertexData);
    } else if (auto* ubo = dynamic_cast<UBONode*>(node)) {
        DrawUBOSettings(ubo);
    } else if (auto* material = dynamic_cast<MaterialNode*>(node)) {
        DrawMaterialSettings(material);
    }
}

void ModelSettingsUI::DrawModelInfo(ModelNodeBase* node) {
    ImGui::Text("%s Settings", node->name.c_str());
    ImGui::Separator();

    // Model info (read-only, selection happens in Asset Library tab)
    const CachedModel* cached = node->getCachedModel();

    if (node->modelPath[0] == '\0') {
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.4f, 1.0f),
            "No model assigned"
        );
        ImGui::TextWrapped(
            "Use the Asset Library tab to load and assign a model to this node."
        );
    } else if (node->hasModel() && cached) {
        // Model is loaded - show info
        ImGui::TextColored(
            ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
            "Model: %s",
            cached->displayName.c_str()
        );

        ImGui::TextColored(
            ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Path: %s",
            node->modelPath
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
    } else {
        // Model path set but not loaded
        ModelHandle handle = node->getModelHandle();
        ModelStatus status = g_modelManager ? g_modelManager->getStatus(handle) : ModelStatus::NotLoaded;

        if (status == ModelStatus::Loading) {
            ImGui::TextColored(
                ImVec4(0.8f, 0.8f, 0.4f, 1.0f),
                "Loading: %s",
                node->modelPath
            );
        } else if (status == ModelStatus::Error) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                "Error loading: %s",
                node->modelPath
            );
        } else {
            ImGui::TextColored(
                ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Path: %s (not loaded)",
                node->modelPath
            );
        }
    }

    ImGui::Spacing();
}

void ModelSettingsUI::DrawVertexDataSettings(VertexDataNode* node) {
    const CachedModel* cached = node->getCachedModel();
    if (!cached) return;

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Vertex Data Output");
    ImGui::TextWrapped(
        "Outputs vertex/index data for %zu geometry ranges. "
        "Connect to a Pipeline node's vertex data input.",
        cached->modelData.getGeometryCount()
    );
}

void ModelSettingsUI::DrawUBOSettings(UBONode* node) {
    const CachedModel* cached = node->getCachedModel();
    if (!cached) return;

    // GLTF Cameras dropdown
    if (!cached->cameras.empty()) {
        ImGui::Separator();
        ImGui::Text("GLTF Cameras");

        // Build camera names for combo box
        std::vector<const char*> cameraNames;
        cameraNames.push_back("None (Default)");
        for (const auto& cam : cached->cameras) {
            cameraNames.push_back(cam.name.c_str());
        }

        // selectedCameraIndex: -1 = none, 0+ = GLTF camera index
        int comboIndex = node->selectedCameraIndex + 1;
        if (ImGui::Combo(
                "Selected Camera", &comboIndex, cameraNames.data(),
                static_cast<int>(cameraNames.size())
            )) {
            node->selectedCameraIndex = comboIndex - 1;
            node->updateCameraFromSelection();
        }

        // Show selected camera info
        if (node->selectedCameraIndex >= 0 &&
            node->selectedCameraIndex < static_cast<int>(cached->cameras.size())) {
            const auto& cam = cached->cameras[node->selectedCameraIndex];
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

    // GLTF Lights info
    if (!cached->lights.empty()) {
        ImGui::Separator();
        ImGui::Text("GLTF Lights (%zu total)", cached->lights.size());

        // Build light names for combo box (for viewing details)
        std::vector<const char*> lightNames;
        for (const auto& light : cached->lights) {
            lightNames.push_back(light.name.c_str());
        }

        // selectedLightIndex is used to view individual light details
        if (node->selectedLightIndex < 0 && !lightNames.empty()) {
            node->selectedLightIndex = 0;
        }
        ImGui::Combo(
            "Inspect Light", &node->selectedLightIndex, lightNames.data(),
            static_cast<int>(lightNames.size())
        );

        // Show inspected light info
        if (node->selectedLightIndex >= 0 &&
            node->selectedLightIndex < static_cast<int>(cached->lights.size())) {
            const auto& light = cached->lights[node->selectedLightIndex];
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
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "UBO Outputs");
    ImGui::TextWrapped(
        "Outputs model matrices for %zu geometry ranges. "
        "Camera and light pins appear if the model contains them.",
        cached->modelData.getGeometryCount()
    );
}

void ModelSettingsUI::DrawMaterialSettings(MaterialNode* node) {
    const CachedModel* cached = node->getCachedModel();
    if (!cached) return;

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Material Outputs");
    ImGui::TextWrapped(
        "Outputs PBR texture arrays for %zu geometry ranges:\n"
        "- Base Color\n"
        "- Emissive\n"
        "- Metallic-Roughness\n"
        "- Normal Map\n\n"
        "Missing textures use default fallbacks.",
        cached->modelData.getGeometryCount()
    );

    // Show texture counts
    size_t textureCount = cached->images.size();
    size_t materialCount = cached->materials.size();
    ImGui::Text("Textures: %zu  |  Materials: %zu", textureCount, materialCount);
}
