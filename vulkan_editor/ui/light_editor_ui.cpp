#include "light_editor_ui.h"
#include "vulkan_editor/graph/light_node.h"
#include "vulkan_editor/graph/ubo_node.h"
#include "vulkan_editor/asset/model_manager.h"
#include <vkDuck/model_loader.h>
#include <numbers>
#include <vector>

void LightEditorUI::Draw(LightNode* lightNode, UBONode* uboNode) {
    if (!lightNode)
        return;

    // GLTF Lights import section (if UBO node has lights)
    const CachedModel* cached = uboNode ? uboNode->getCachedModel() : nullptr;
    if (cached && !cached->lights.empty()) {
        ImGui::SeparatorText("Import from GLTF");

        // Build light names for combo box
        std::vector<const char*> lightNames;
        for (const auto& light : cached->lights) {
            lightNames.push_back(light.name.c_str());
        }

        static int selectedGLTFLight = 0;
        if (selectedGLTFLight >= static_cast<int>(cached->lights.size())) {
            selectedGLTFLight = 0;
        }

        ImGui::Combo(
            "GLTF Light", &selectedGLTFLight, lightNames.data(),
            static_cast<int>(lightNames.size())
        );

        // Show GLTF light info
        const auto& gltfLight = cached->lights[selectedGLTFLight];
        const char* typeStr = "Unknown";
        switch (gltfLight.type) {
            case GLTFLightType::Directional: typeStr = "Directional"; break;
            case GLTFLightType::Point: typeStr = "Point"; break;
            case GLTFLightType::Spot: typeStr = "Spot"; break;
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.9f, 1.0f), "Type: %s", typeStr);
        ImGui::Text(
            "Position: (%.2f, %.2f, %.2f)",
            gltfLight.position.x, gltfLight.position.y, gltfLight.position.z
        );
        ImGui::Text(
            "Color: (%.2f, %.2f, %.2f)",
            gltfLight.color.r, gltfLight.color.g, gltfLight.color.b
        );
        ImGui::Text("Intensity: %.2f", gltfLight.intensity);

        ImGui::Spacing();

        // Target light selection for single import
        static int targetLightIndex = 0;
        if (targetLightIndex >= lightNode->numLights) {
            targetLightIndex = 0;
        }

        // Build target light names
        std::vector<std::string> targetLightNames;
        for (int i = 0; i < lightNode->numLights; ++i) {
            targetLightNames.push_back("Light " + std::to_string(i));
        }
        std::vector<const char*> targetLightNamePtrs;
        for (const auto& name : targetLightNames) {
            targetLightNamePtrs.push_back(name.c_str());
        }

        ImGui::Combo(
            "Target Light", &targetLightIndex, targetLightNamePtrs.data(),
            static_cast<int>(targetLightNamePtrs.size())
        );

        // Import buttons
        if (ImGui::Button("Import to Target Light")) {
            if (targetLightIndex < lightNode->numLights) {
                auto& targetLight = lightNode->lightsBuffer.lights[targetLightIndex];
                targetLight.position = gltfLight.position;
                targetLight.color = gltfLight.color;
                targetLight.intensity = gltfLight.intensity;
                targetLight.radius = gltfLight.range > 0.0f ? gltfLight.range : 10.0f;
                lightNode->lightsBuffer.updateGpuBuffer();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Import All GLTF Lights")) {
            // Replace all lights with GLTF lights
            lightNode->numLights = static_cast<int>(cached->lights.size());
            lightNode->lightsBuffer.lights.resize(lightNode->numLights);

            for (size_t i = 0; i < cached->lights.size(); ++i) {
                const auto& src = cached->lights[i];
                auto& dst = lightNode->lightsBuffer.lights[i];
                dst.position = src.position;
                dst.color = src.color;
                dst.intensity = src.intensity;
                dst.radius = src.range > 0.0f ? src.range : 10.0f;
            }
            lightNode->lightsBuffer.updateGpuBuffer();
        }

        ImGui::Spacing();
    }

    ImGui::SeparatorText("Light Array Settings");

    // Number of lights - read-only when controlled by shader
    if (lightNode->shaderControlledCount) {
        ImGui::BeginDisabled();
        int count = lightNode->numLights;
        ImGui::InputInt("Number of Lights", &count);
        ImGui::EndDisabled();
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.3f, 1.0f),
            "Light count controlled by connected shader"
        );
    } else {
        int prevNumLights = lightNode->numLights;
        ImGui::InputInt("Number of Lights", &lightNode->numLights);
        if (lightNode->numLights < 1) lightNode->numLights = 1;

        if (prevNumLights != lightNode->numLights) {
            lightNode->ensureLightCount();
        }
    }

    // Presets
    if (ImGui::CollapsingHeader("Presets")) {
        if (ImGui::Button("Arrange in Circle")) {
            for (int i = 0; i < lightNode->numLights; ++i) {
                float angle = (float)i / (float)lightNode->numLights *
                              2.0f * std::numbers::pi_v<float>;
                float radius = 5.0f;

                lightNode->lightsBuffer.lights[i].position = glm::vec3(
                    cos(angle) * radius, 2.0f, sin(angle) * radius
                );
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Random Colors")) {
            for (int i = 0; i < lightNode->numLights; ++i) {
                lightNode->lightsBuffer.lights[i].color = glm::vec3(
                    (float)rand() / RAND_MAX, (float)rand() / RAND_MAX,
                    (float)rand() / RAND_MAX
                );
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("White Light")) {
            for (int i = 0; i < lightNode->numLights; ++i) {
                lightNode->lightsBuffer.lights[i].color = glm::vec3(1.0f, 1.0f, 1.0f);
            }
        }
    }

    // Individual light settings
    ImGui::SeparatorText("Individual Lights");

    for (int i = 0; i < lightNode->numLights; ++i) {
        auto& light = lightNode->lightsBuffer.lights[i];

        ImGui::PushID(i);

        std::string header = "Light " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Indent();

            ImGui::DragFloat3("Position", &light.position.x, 0.1f);
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.1f, 0.0f, "%.2f");
            ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f, "%.2f");

            ImGui::Unindent();
        }

        ImGui::PopID();
    }
}
