#pragma once
#include "camera_node.h"

// ============================================================================
// FPSCameraNode - First-person fly camera (like Blender fly mode)
// Uses CameraController from vkDuck library for camera math
// ============================================================================
class FPSCameraNode : public CameraNodeBase {
public:
    FPSCameraNode();
    FPSCameraNode(int id);
    ~FPSCameraNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    // Serialization override - includes FPS-specific fields
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Override to copy FPS-specific parameters
    void createPrimitives(primitives::Store& store) override;

    // Convenience accessors for FPS-specific params (delegate to controller)
    float getYaw() const { return controller.yaw; }
    void setYaw(float y) { controller.yaw = y; }
    float getPitch() const { return controller.pitch; }
    void setPitch(float p) { controller.pitch = p; }
    float getMoveSpeed() const { return controller.moveSpeed; }
    void setMoveSpeed(float s) { controller.moveSpeed = s; }
    float getRotateSpeed() const { return controller.rotateSpeed; }
    void setRotateSpeed(float s) { controller.rotateSpeed = s; }

    // Apply settings from a GLTF camera
    void applyGLTFCamera(const GLTFCamera& gltfCamera);

    // Reset to initial state (also resets yaw/pitch)
    void saveInitialState() override;
    void resetToInitialState() override;

    // Camera type for code generation
    primitives::CameraType getCameraType() const override {
        return primitives::CameraType::FPS;
    }

private:
    // Initial FPS parameters for reset
    float initialYaw{0.0f};
    float initialPitch{0.0f};
};
