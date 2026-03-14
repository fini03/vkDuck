#include "multi_model_settings_ui.h"
#include "../asset/model_manager.h"
#include "../graph/multi_model_source_node.h"
#include "../graph/multi_vertex_data_node.h"
#include "../graph/multi_ubo_node.h"
#include "../graph/multi_material_node.h"
#include "../graph/node_graph.h"
#include "../shader/shader_manager.h"
#include <glm/gtc/constants.hpp>
#include <imgui.h>

namespace fs = std::filesystem;

// ============================================================================
// Source Node Settings
// ============================================================================

void MultiModelSettingsUI::Draw(
    MultiModelSourceNode* node,
    ShaderManager* /*shaderManager*/,
    NodeGraph* /*graph*/) {
    if (!node) {
        ImGui::TextWrapped("No source node selected.");
        return;
    }

    ImGui::Text("%s Settings", node->name.c_str());
    ImGui::Separator();

    // Draw model list management
    DrawModelList(node);
}

void MultiModelSettingsUI::DrawModelList(MultiModelSourceNode* node) {
    const size_t modelCount = node->getModelCount();

    // Header with count
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Models (%zu)",
                       modelCount);

    if (modelCount == 0) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.4f, 1.0f), "No models added");
        ImGui::TextWrapped(
            "Use the Asset Library tab to add models to this source node.");
    } else {
        // Draw model list
        ImGui::BeginChild("ModelList", ImVec2(0, 150), true);

        for (size_t i = 0; i < modelCount; ++i) {
            const ModelEntry& entry = node->getModel(i);

            ImGui::PushID(static_cast<int>(i));

            // Enable/disable checkbox
            bool enabled = entry.enabled;
            if (ImGui::Checkbox("##enabled", &enabled)) {
                node->setModelEnabled(i, enabled);
            }
            ImGui::SameLine();

            // Model name
            const CachedModel* cached =
                entry.handle.isValid() && g_modelManager
                    ? g_modelManager->getModel(entry.handle)
                    : nullptr;

            if (cached && cached->status == ModelStatus::Loaded) {
                ImVec4 textColor =
                    enabled ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                            : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                ImGui::TextColored(textColor, "%zu. %s", i + 1,
                                   cached->displayName.c_str());
            } else if (entry.path[0] != '\0') {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f),
                                   "%zu. %s (loading...)", i + 1, entry.path);
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                                   "%zu. (invalid)", i + 1);
            }

            // Remove button
            ImGui::SameLine(ImGui::GetWindowWidth() - 80);
            if (ImGui::SmallButton("Remove")) {
                node->removeModel(i);
                ImGui::PopID();
                break; // List changed, exit loop
            }

            // Show stats for loaded model
            if (cached && cached->status == ModelStatus::Loaded) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "   Verts: %zu  Indices: %zu  Meshes: %zu",
                                   cached->modelData.getTotalVertexCount(),
                                   cached->modelData.getTotalIndexCount(),
                                   cached->modelData.getGeometryCount());
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    // Consolidated stats
    if (modelCount > 0) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Consolidated Data");
        ImGui::Text("Total Vertices: %zu",
                    node->getConsolidatedVertices().size());
        ImGui::Text("Total Indices: %zu",
                    node->getConsolidatedIndices().size());
        ImGui::Text("Total Geometry Ranges: %zu",
                    node->getConsolidatedRanges().size());

        const auto& cameras = node->getMergedCameras();
        const auto& lights = node->getMergedLights();
        if (!cameras.empty() || !lights.empty()) {
            ImGui::Text("Cameras: %zu  |  Lights: %zu", cameras.size(),
                        lights.size());
        }
    }

    ImGui::Spacing();
}

// ============================================================================
// Consumer Node Settings
// ============================================================================

void MultiModelSettingsUI::DrawConnectionStatus(
    const char* nodeType,
    bool hasValidSource,
    const char* sourceName) {
    if (hasValidSource) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                           "Connected to: %s", sourceName);
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f),
                           "Not connected to a Model Source");
        ImGui::TextWrapped(
            "Connect this %s node's Source input to a Model Source node "
            "to receive model data.", nodeType);
    }
    ImGui::Separator();
}

void MultiModelSettingsUI::Draw(
    MultiVertexDataNode* node,
    ShaderManager* /*shaderManager*/,
    NodeGraph* graph) {
    if (!node) {
        ImGui::TextWrapped("No vertex data node selected.");
        return;
    }

    ImGui::Text("%s Settings", node->name.c_str());
    ImGui::Separator();

    // Show connection status
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    DrawConnectionStatus("Vertex Data", source != nullptr,
                         source ? source->name.c_str() : "");

    // Draw node-specific settings
    DrawMultiVertexDataSettings(node, graph);
}

void MultiModelSettingsUI::Draw(
    MultiUBONode* node,
    ShaderManager* /*shaderManager*/,
    NodeGraph* graph) {
    if (!node) {
        ImGui::TextWrapped("No UBO node selected.");
        return;
    }

    ImGui::Text("%s Settings", node->name.c_str());
    ImGui::Separator();

    // Show connection status
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    DrawConnectionStatus("UBO", source != nullptr,
                         source ? source->name.c_str() : "");

    // Draw node-specific settings
    DrawMultiUBOSettings(node, graph);
}

void MultiModelSettingsUI::Draw(
    MultiMaterialNode* node,
    ShaderManager* /*shaderManager*/,
    NodeGraph* graph) {
    if (!node) {
        ImGui::TextWrapped("No material node selected.");
        return;
    }

    ImGui::Text("%s Settings", node->name.c_str());
    ImGui::Separator();

    // Show connection status
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    DrawConnectionStatus("Material", source != nullptr,
                         source ? source->name.c_str() : "");

    // Draw node-specific settings
    DrawMultiMaterialSettings(node, graph);
}

// ============================================================================
// Node-Specific Settings
// ============================================================================

void MultiModelSettingsUI::DrawMultiVertexDataSettings(
    MultiVertexDataNode* node,
    NodeGraph* graph) {
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    if (!source) return;

    const auto& ranges = source->getConsolidatedRanges();

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Vertex Data Output");
    ImGui::TextWrapped(
        "Outputs vertex/index data for %zu consolidated geometry ranges. "
        "Connect to a Pipeline node's vertex data input.",
        ranges.size());
}

void MultiModelSettingsUI::DrawMultiUBOSettings(
    MultiUBONode* node,
    NodeGraph* graph) {
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    if (!source) return;

    const auto& ranges = source->getConsolidatedRanges();
    const auto& cameras = source->getMergedCameras();
    const auto& lights = source->getMergedLights();

    // GLTF Cameras dropdown (from merged cameras)
    if (!cameras.empty()) {
        ImGui::Separator();
        ImGui::Text("Merged Cameras (%zu)", cameras.size());

        // Build camera names for combo box
        std::vector<const char*> cameraNames;
        cameraNames.push_back("None (Default)");
        for (const auto& cam : cameras) {
            cameraNames.push_back(cam.name.c_str());
        }

        // selectedCameraIndex: -1 = none, 0+ = merged camera index
        int comboIndex = node->selectedCameraIndex + 1;
        if (ImGui::Combo("Selected Camera", &comboIndex, cameraNames.data(),
                         static_cast<int>(cameraNames.size()))) {
            node->selectedCameraIndex = comboIndex - 1;
            node->updateCameraFromSelection();
        }

        // Show selected camera info
        if (node->selectedCameraIndex >= 0 &&
            node->selectedCameraIndex < static_cast<int>(cameras.size())) {
            const auto& cam = cameras[node->selectedCameraIndex];
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Camera: %s",
                               cam.name.c_str());
            ImGui::Text("Type: %s",
                        cam.isPerspective ? "Perspective" : "Orthographic");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.9f, 1.0f), "GLTF Values:");
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", cam.position.x,
                        cam.position.y, cam.position.z);
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

    // GLTF Lights info (show merged count)
    if (!lights.empty()) {
        ImGui::Separator();
        ImGui::Text("Merged Lights (%zu)", lights.size());

        // Just show a summary of all lights
        for (size_t i = 0; i < std::min(lights.size(), size_t(5)); ++i) {
            const auto& light = lights[i];
            const char* typeStr = "Unknown";
            switch (light.type) {
            case GLTFLightType::Directional:
                typeStr = "Dir";
                break;
            case GLTFLightType::Point:
                typeStr = "Point";
                break;
            case GLTFLightType::Spot:
                typeStr = "Spot";
                break;
            }
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.7f, 1.0f), "  %s: %s (%s)",
                               std::to_string(i + 1).c_str(), light.name.c_str(),
                               typeStr);
        }

        if (lights.size() > 5) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "  ... and %zu more", lights.size() - 5);
        }

        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "UBO Outputs");
    ImGui::TextWrapped(
        "Outputs model matrices for %zu consolidated geometry ranges. "
        "Camera and light pins appear if any model contains them.",
        ranges.size());
}

void MultiModelSettingsUI::DrawMultiMaterialSettings(
    MultiMaterialNode* node,
    NodeGraph* graph) {
    MultiModelSourceNode* source = graph ? node->findSourceNode(*graph) : nullptr;
    if (!source) return;

    const auto& ranges = source->getConsolidatedRanges();
    const auto& materials = source->getMergedMaterials();
    const auto& images = source->getMergedImages();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Material Outputs");
    ImGui::TextWrapped(
        "Outputs PBR texture arrays for %zu consolidated geometry ranges:\n"
        "- Base Color\n"
        "- Emissive\n"
        "- Metallic-Roughness\n"
        "- Normal Map\n\n"
        "Missing textures use default fallbacks.",
        ranges.size());

    // Show merged counts
    ImGui::Text("Merged Textures: %zu  |  Merged Materials: %zu", images.size(),
                materials.size());
}
