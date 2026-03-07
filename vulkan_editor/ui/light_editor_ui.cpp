#include "light_editor_ui.h"
#include "vulkan_editor/graph/light_node.h"
#include <numbers>

void LightEditorUI::Draw(LightNode* lightNode) {
    if (!lightNode)
        return;

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
