#include "camera_editor_ui.h"
#include "vulkan_editor/graph/camera_node.h"
#include "vulkan_editor/graph/fixed_camera_node.h"
#include "vulkan_editor/graph/fps_camera_node.h"
#include "vulkan_editor/graph/model_node.h"
#include "vulkan_editor/graph/node_graph.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

void CameraEditorUI::Draw(
    CameraNodeBase* camera,
    NodeGraph* graph,
    ModelNode* modelNode
) {
    if (!camera)
        return;

    ImGui::SeparatorText("Camera Settings");

    // Dispatch to specific camera type UI
    if (auto* orbital = dynamic_cast<OrbitalCameraNode*>(camera)) {
        DrawOrbitalCamera(orbital, modelNode);
    } else if (auto* fps = dynamic_cast<FPSCameraNode*>(camera)) {
        DrawFPSCamera(fps, modelNode);
    } else if (auto* fixed = dynamic_cast<FixedCameraNode*>(camera)) {
        DrawFixedCamera(fixed);
    }

    // Common debug info
    if (ImGui::CollapsingHeader("Debug Info")) {
        DrawDebugInfo(camera);
    }
}

void CameraEditorUI::DrawOrbitalCamera(
    OrbitalCameraNode* camera,
    ModelNode* modelNode
) {
    // GLTF Cameras from model file (if model node provided and has cameras)
    if (modelNode && !modelNode->gltfCameras.empty()) {
        // Auto-apply GLTF camera on first render after model load
        if (modelNode->needsCameraApply && modelNode->selectedCameraIndex >= 0) {
            camera->applyGLTFCamera(
                modelNode->gltfCameras[modelNode->selectedCameraIndex]
            );
            modelNode->needsCameraApply = false;
        }

        if (ImGui::CollapsingHeader(
                "GLTF Cameras", ImGuiTreeNodeFlags_DefaultOpen
            )) {
            // Build camera names for combo box
            std::vector<const char*> cameraNames;
            cameraNames.push_back("Default Camera");
            for (const auto& cam : modelNode->gltfCameras) {
                cameraNames.push_back(cam.name.c_str());
            }

            // selectedCameraIndex: -1 = default, 0+ = GLTF camera index
            int comboIndex = modelNode->selectedCameraIndex + 1;
            if (ImGui::Combo(
                    "Active Camera", &comboIndex, cameraNames.data(),
                    static_cast<int>(cameraNames.size())
                )) {
                modelNode->selectedCameraIndex = comboIndex - 1;
                modelNode->updateCameraFromSelection();

                // Auto-apply when selection changes
                if (modelNode->selectedCameraIndex >= 0) {
                    camera->applyGLTFCamera(
                        modelNode->gltfCameras[modelNode->selectedCameraIndex]
                    );
                }
            }

            // Show selected camera info from GLTF file
            if (modelNode->selectedCameraIndex >= 0) {
                const auto& cam =
                    modelNode->gltfCameras[modelNode->selectedCameraIndex];
                ImGui::TextColored(
                    ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "GLTF Camera: %s",
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
                }
                ImGui::Text(
                    "Near: %.3f, Far: %.1f", cam.nearPlane, cam.farPlane
                );

                ImGui::Spacing();
                if (ImGui::Button("Re-apply Camera")) {
                    camera->applyGLTFCamera(cam);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Re-applies the selected GLTF camera's settings\n"
                        "(position, FOV, near/far planes) to reset any manual "
                        "changes"
                    );
                }
            }
        }
    }

    bool changed = false;

    // Transform (similar to Fixed Camera layout)
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |=
            ImGui::DragFloat3("Position", &camera->position.x, 0.1f);
        changed |= ImGui::DragFloat3(
            "Look At Target", &camera->target.x, 0.1f
        );
        changed |= ImGui::DragFloat3("Up Vector", &camera->up.x, 0.01f);

        if (ImGui::Button("Normalize Up")) {
            camera->up = glm::normalize(camera->up);
            changed = true;
        }

        // Recalculate orbit parameters if position/target changed
        if (changed) {
            glm::vec3 offset = camera->position - camera->target;
            camera->distance = glm::length(offset);
            if (camera->distance > 0.001f) {
                camera->pitch = asin(offset.y / camera->distance);
                camera->yaw = atan2(offset.x, offset.z);
            }
        }
    }

    // Projection
    changed |= DrawProjectionSettings(camera);

    // Update matrices if anything changed
    if (changed) {
        camera->updateMatrices();
    }

    // Control Speeds (at the end as requested)
    if (ImGui::CollapsingHeader("Control Speeds")) {
        ImGui::SliderFloat("Move Speed", &camera->moveSpeed, 0.1f, 20.0f);
        ImGui::SliderFloat(
            "Rotate Speed", &camera->rotateSpeed, 0.001f, 0.02f
        );
        ImGui::SliderFloat("Zoom Speed", &camera->zoomSpeed, 0.1f, 2.0f);
    }

    // Live Controls Info
    if (ImGui::CollapsingHeader(
            "Live Controls", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        ImGui::TextColored(
            ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "In Live View window:"
        );
        ImGui::BulletText("WASD: Move target");
        ImGui::BulletText("Q/E: Move up/down");
        ImGui::BulletText("Right-click + Drag: Orbit camera");
        ImGui::BulletText("Scroll wheel: Zoom in/out");
    }

    // Reset Button
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (camera->hasInitialState()) {
        if (ImGui::Button("Reset Camera", ImVec2(-1, 0))) {
            camera->resetToInitialState();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset camera to its initial position and orientation");
        }
    }
}

void CameraEditorUI::DrawFPSCamera(
    FPSCameraNode* camera,
    ModelNode* modelNode
) {
    // GLTF Cameras from model file (if model node provided and has cameras)
    if (modelNode && !modelNode->gltfCameras.empty()) {
        // Auto-apply GLTF camera on first render after model load
        if (modelNode->needsCameraApply && modelNode->selectedCameraIndex >= 0) {
            camera->applyGLTFCamera(
                modelNode->gltfCameras[modelNode->selectedCameraIndex]
            );
            modelNode->needsCameraApply = false;
        }

        if (ImGui::CollapsingHeader(
                "GLTF Cameras", ImGuiTreeNodeFlags_DefaultOpen
            )) {
            // Build camera names for combo box
            std::vector<const char*> cameraNames;
            cameraNames.push_back("Default Camera");
            for (const auto& cam : modelNode->gltfCameras) {
                cameraNames.push_back(cam.name.c_str());
            }

            // selectedCameraIndex: -1 = default, 0+ = GLTF camera index
            int comboIndex = modelNode->selectedCameraIndex + 1;
            if (ImGui::Combo(
                    "Active Camera", &comboIndex, cameraNames.data(),
                    static_cast<int>(cameraNames.size())
                )) {
                modelNode->selectedCameraIndex = comboIndex - 1;
                modelNode->updateCameraFromSelection();

                // Auto-apply when selection changes
                if (modelNode->selectedCameraIndex >= 0) {
                    camera->applyGLTFCamera(
                        modelNode->gltfCameras[modelNode->selectedCameraIndex]
                    );
                }
            }

            // Show selected camera info from GLTF file
            if (modelNode->selectedCameraIndex >= 0) {
                const auto& cam =
                    modelNode->gltfCameras[modelNode->selectedCameraIndex];
                ImGui::TextColored(
                    ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "GLTF Camera: %s",
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
                }
                ImGui::Text(
                    "Near: %.3f, Far: %.1f", cam.nearPlane, cam.farPlane
                );

                ImGui::Spacing();
                if (ImGui::Button("Re-apply Camera")) {
                    camera->applyGLTFCamera(cam);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Re-applies the selected GLTF camera's settings\n"
                        "(position, FOV, near/far planes) to reset any manual "
                        "changes"
                    );
                }
            }
        }
    }

    bool changed = false;

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |=
            ImGui::DragFloat3("Position", &camera->position.x, 0.1f);
        changed |= ImGui::DragFloat3(
            "Look At Target", &camera->target.x, 0.1f
        );
        changed |= ImGui::DragFloat3("Up Vector", &camera->up.x, 0.01f);

        if (ImGui::Button("Normalize Up")) {
            camera->up = glm::normalize(camera->up);
            changed = true;
        }

        // Recalculate yaw/pitch if position/target changed
        if (changed) {
            glm::vec3 direction =
                glm::normalize(camera->target - camera->position);
            camera->yaw = atan2(direction.x, direction.z);
            camera->pitch = asin(direction.y);
        }
    }

    // Projection
    changed |= DrawProjectionSettings(camera);

    // Update matrices if anything changed
    if (changed) {
        camera->updateMatrices();
    }

    // Control Speeds
    if (ImGui::CollapsingHeader("Control Speeds")) {
        ImGui::SliderFloat("Move Speed", &camera->moveSpeed, 0.1f, 20.0f);
        ImGui::SliderFloat(
            "Rotate Speed", &camera->rotateSpeed, 0.001f, 0.02f
        );
    }

    // Live Controls Info
    if (ImGui::CollapsingHeader(
            "Live Controls", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        ImGui::TextColored(
            ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "In Live View window:"
        );
        ImGui::BulletText("WASD: Move camera");
        ImGui::BulletText("Q/E: Move up/down");
        ImGui::BulletText("Right-click + Drag: Look around");
    }

    // Reset Button
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (camera->hasInitialState()) {
        if (ImGui::Button("Reset Camera", ImVec2(-1, 0))) {
            camera->resetToInitialState();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset camera to its initial position and orientation");
        }
    }
}

void CameraEditorUI::DrawFixedCamera(FixedCameraNode* camera) {
    bool changed = false;

    // Position and Target
    if (ImGui::CollapsingHeader(
            "Transform", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        changed |=
            ImGui::DragFloat3("Position", &camera->position.x, 0.1f);
        changed |= ImGui::DragFloat3(
            "Look At Target", &camera->target.x, 0.1f
        );
        changed |= ImGui::DragFloat3("Up Vector", &camera->up.x, 0.01f);

        if (ImGui::Button("Normalize Up")) {
            camera->up = glm::normalize(camera->up);
            changed = true;
        }
    }

    // Projection
    changed |= DrawProjectionSettings(camera);

    // Update matrices if anything changed
    if (changed) {
        camera->updateMatrices();
    }

    // Info
    if (ImGui::CollapsingHeader("Info")) {
        ImGui::TextColored(
            ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Fixed Camera"
        );
        ImGui::TextWrapped(
            "This camera has a fixed position and target. "
            "Use the transform controls above to position it manually."
        );
    }
}

bool CameraEditorUI::DrawProjectionSettings(CameraNodeBase* camera) {
    bool changed = false;

    if (ImGui::CollapsingHeader(
            "Projection", ImGuiTreeNodeFlags_DefaultOpen
        )) {
        changed |= ImGui::SliderFloat(
            "FOV", &camera->fov, 1.0f, 120.0f, "%.1f"
        );
        changed |= ImGui::DragFloat(
            "Near Plane", &camera->nearPlane, 0.01f, 0.001f, 100.0f
        );
        changed |= ImGui::DragFloat(
            "Far Plane", &camera->farPlane, 1.0f, 1.0f, 10000.0f
        );
    }

    return changed;
}

void CameraEditorUI::DrawDebugInfo(CameraNodeBase* camera) {
    ImGui::Text(
        "Position: (%.2f, %.2f, %.2f)", camera->position.x,
        camera->position.y, camera->position.z
    );
    ImGui::Text(
        "Target: (%.2f, %.2f, %.2f)", camera->target.x,
        camera->target.y, camera->target.z
    );

    ImGui::Separator();
    ImGui::Text("View Matrix:");
    for (int i = 0; i < 4; i++) {
        ImGui::Text(
            "  %.2f %.2f %.2f %.2f", camera->cameraData.view[i][0],
            camera->cameraData.view[i][1],
            camera->cameraData.view[i][2], camera->cameraData.view[i][3]
        );
    }
}
