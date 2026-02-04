#include "pipeline_settings_ui.h"
#include "../graph/model_node.h"
#include "../graph/node_graph.h"
#include "../graph/pipeline_node.h"
#include "../shader/shader_manager.h"
#include "attachment_editor_ui.h"
#include "camera_editor_ui.h"
#include "light_editor_ui.h"
#include "pipeline_settings.h"

using namespace ShaderTypes;

static const char* GetExtentTypeName(ExtentType type) {
    switch (type) {
    case ExtentType::SwapchainRelative:
        return "Swapchain Relative";
    case ExtentType::Custom:
        return "Custom Size";
    default:
        return "Unknown";
    }
}

void PipelineSettingsUI::DrawCameraLightSettings(
    PipelineNode* pipeline,
    NodeGraph& graph
) {
    if (!pipeline)
        return;

    // Camera bindings
    if (!pipeline->detectedCameras.empty()) {
        ImGui::SeparatorText("Camera Bindings");

        for (auto& camera : pipeline->detectedCameras) {
            ImGui::PushID(camera.uniformName.c_str());

            ImGui::Text("%s", camera.uniformName.c_str());
            ImGui::Indent();

            ImGui::Checkbox("Use Global Camera", &camera.useGlobal);

            if (!camera.useGlobal) {
                ImGui::TextDisabled(
                    "→ Connect a Camera Node to the input pin"
                );

                // Show if connected
                if (graph.isPinLinked(camera.pin.id)) {
                    ImGui::TextColored(
                        ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Connected"
                    );
                } else {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                        "⚠ Not connected"
                    );
                }
            } else {
                ImGui::TextDisabled("Using global camera settings");
            }

            // Show expected members
            if (ImGui::TreeNode("Expected Members")) {
                for (const auto& member : camera.expectedMembers) {
                    ImGui::BulletText("%s", member.c_str());
                }
                ImGui::TreePop();
            }

            ImGui::Unindent();
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // Light bindings
    if (!pipeline->detectedLights.empty()) {
        ImGui::SeparatorText("Light Bindings");

        for (auto& light : pipeline->detectedLights) {
            ImGui::PushID(light.uniformName.c_str());

            ImGui::Text(
                "%s (%d lights)", light.uniformName.c_str(),
                light.arraySize
            );
            ImGui::Indent();

            ImGui::Checkbox("Use Global Lights", &light.useGlobal);

            if (!light.useGlobal) {
                ImGui::TextDisabled(
                    "→ Connect a Light Node to the input pin"
                );

                // Show if connected
                if (graph.isPinLinked(light.pin.id)) {
                    ImGui::TextColored(
                        ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Connected"
                    );
                } else {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                        "⚠ Not connected"
                    );
                }

                ImGui::TextDisabled(
                    "Note: Light Node must have %d lights",
                    light.arraySize
                );
            } else {
                ImGui::TextDisabled("Using global light settings");
            }

            ImGui::Unindent();
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    if (pipeline->detectedCameras.empty() &&
        pipeline->detectedLights.empty()) {
        ImGui::TextDisabled(
            "No camera or light bindings detected in shaders"
        );
    }
}

void PipelineSettingsUI::Draw(
    PipelineNode* selectedNode,
    NodeGraph& graph,
    ShaderManager* shader_manager
) {
    ImGui::Text("Pipeline Settings");
    ImGui::Separator();

    ImGui::Text("Image Extents");

    ExtentConfig& extentConfig = selectedNode->settings.extentConfig;

    if (ImGui::BeginCombo(
            "Mode", GetExtentTypeName(extentConfig.type)
        )) {
        for (auto type :
             {ExtentType::SwapchainRelative, ExtentType::Custom}) {
            bool isSelected = (extentConfig.type == type);
            if (ImGui::Selectable(
                    GetExtentTypeName(type), isSelected
                )) {
                // When the type changes, reset to the defaults for that
                // enum
                extentConfig = ExtentConfig::GetDefault(type);
            }
        }
        ImGui::EndCombo();
    }

    // Only show manual inputs if we are in any "Fixed" mode
    if (extentConfig.type != ExtentType::SwapchainRelative) {
        ImGui::Indent();

        // If the user manually edits the values, we automatically flip
        // the type to "Custom" so their changes aren't lost if they
        // click off and back onto the node.
        if (ImGui::InputInt("Width", &extentConfig.width))
            extentConfig.type = ExtentType::Custom;
        if (ImGui::InputInt("Height", &extentConfig.height))
            extentConfig.type = ExtentType::Custom;

        ImGui::Unindent();
    }

    ImGui::Separator();

    // Input Assembly
    ImGui::Text("Input Assembly");

    // Helper lambda to find a connected ModelNode
    auto findConnectedModel = [&]() -> ModelNode* {
        for (const auto& binding : selectedNode->inputBindings) {
            auto& pin = binding.pin;
            // Check links for this specific pin
            for (const auto& link : graph.links) {
                if (link.endPin == pin.id) {
                    // Find the source node
                    for (auto& nodePtr : graph.nodes) {
                        if (auto* modelNode = dynamic_cast<ModelNode*>(
                                nodePtr.get()
                            )) {
                            if (modelNode->modelMatrixPin.id ==
                                    link.startPin ||
                                modelNode->texturePin.id ==
                                    link.startPin) {
                                return modelNode;
                            }
                        }
                    }
                }
            }
        }
        return nullptr;
    };

    ModelNode* connectedModel = findConnectedModel();

    if (connectedModel) {
        // Sync logic
        selectedNode->settings.inputAssembly =
            connectedModel->settings.topology;
        selectedNode->settings.primitiveRestart =
            connectedModel->settings.primitiveRestart;

        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        );
        ImGui::TextWrapped(
            "Topology: %s",
            ModelNode::topologyOptions[selectedNode->settings
                                           .inputAssembly]
        );
        ImGui::TextWrapped(
            "Primitive Restart: %s",
            selectedNode->settings.primitiveRestart ? "Enabled"
                                                    : "Disabled"
        );
        ImGui::Text("(Managed by connected Model)");
        ImGui::PopStyleColor();
    } else {
        // Fallback if no model is connected
        ImGui::Text("No Model connected to provide Topology.");
    }

    // Rasterizer
    ImGui::Separator();
    ImGui::Text("Rasterizer");

    ImGui::Checkbox(
        "Depth Clamp", &selectedNode->settings.depthClamp
    );
    ImGui::Checkbox(
        "Rasterizer Discard", &selectedNode->settings.rasterizerDiscard
    );
    ImGui::Combo(
        "Polygon Mode", &selectedNode->settings.polygonMode,
        PipelineNode::polygonModes.data(),
        static_cast<int>(PipelineNode::polygonModes.size())
    );
    ImGui::InputFloat("Line Width", &selectedNode->settings.lineWidth);
    ImGui::Combo(
        "Cull Mode", &selectedNode->settings.cullMode,
        PipelineNode::cullModes.data(),
        static_cast<int>(PipelineNode::cullModes.size())
    );
    ImGui::Combo(
        "Front Face", &selectedNode->settings.frontFace,
        PipelineNode::frontFaceOptions.data(),
        static_cast<int>(PipelineNode::frontFaceOptions.size())
    );
    ImGui::Checkbox(
        "Depth Bias Enabled", &selectedNode->settings.depthBiasEnabled
    );
    if (selectedNode->settings.depthBiasEnabled) {
        ImGui::Indent();
        ImGui::InputFloat(
            "Constant Factor",
            &selectedNode->settings.depthBiasConstantFactor
        );
        ImGui::InputFloat(
            "Clamp", &selectedNode->settings.depthBiasClamp
        );
        ImGui::InputFloat(
            "Slope Factor", &selectedNode->settings.depthBiasSlopeFactor
        );
        ImGui::Unindent();
    }

    // Multisampling
    ImGui::Separator();
    ImGui::Text("Multisampling");
    ImGui::Checkbox(
        "Sample Shading", &selectedNode->settings.sampleShading
    );
    ImGui::Combo(
        "Rasterization Samples",
        &selectedNode->settings.rasterizationSamples,
        PipelineNode::sampleCountOptions.data(),
        static_cast<int>(PipelineNode::sampleCountOptions.size())
    );

    // Color blending
    ImGui::Separator();
    ImGui::Text("Color Blending");
    ImGui::Checkbox(
        "Logic Operation Enabled", &selectedNode->settings.logicOpEnable
    );
    ImGui::Combo(
        "Logic Operation", &selectedNode->settings.logicOp,
        PipelineNode::logicOps.data(),
        static_cast<int>(PipelineNode::logicOps.size())
    );
    ImGui::InputFloat4(
        "Color Blend Constants", selectedNode->settings.blendConstants
    );

    // ========================================================================
    // Shader Selection with File Watcher Controls
    // ========================================================================
    ImGui::Separator();
    ImGui::Text("Shaders");

    // File watcher status and controls
    ImGui::BeginGroup();
    {
        // Auto-reload toggle
        bool autoReload = shader_manager->isAutoReloadEnabled();
        if (ImGui::Checkbox("Auto-Reload Shaders", &autoReload)) {
            shader_manager->setAutoReloadEnabled(autoReload);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Automatically reload shaders when files change in the "
                "shader directory"
            );
        }

        ImGui::SameLine();

        // Status indicator
        if (shader_manager->isAutoReloadEnabled()) {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
            );
            ImGui::TextUnformatted("[Active]");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
            );
            ImGui::TextUnformatted("[Inactive]");
            ImGui::PopStyleColor();
        }

        // Pending reloads indicator
        if (shader_manager->hasPendingReloads()) {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)
            );

            // Pulsing animation
            float t = static_cast<float>(ImGui::GetTime());
            float alpha = 0.5f + 0.5f * sinf(t * 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

            ImGui::TextUnformatted("Reload pending...");

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();

    // Shader pickers (all paths are project-relative)
    shader_manager->showShaderPicker(
        selectedNode, "Vertex Shader",
        selectedNode->settings.vertexShaderPath,
        selectedNode->settings.compiledVertexShaderPath, graph
    );

    shader_manager->showShaderPicker(
        selectedNode, "Fragment Shader",
        selectedNode->settings.fragmentShaderPath,
        selectedNode->settings.compiledFragmentShaderPath, graph
    );

    // ========================================================================
    // Rest of the settings
    // ========================================================================

    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(
            "Framebuffer Attachments", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        AttachmentEditorUI::Draw(selectedNode);
    }
}