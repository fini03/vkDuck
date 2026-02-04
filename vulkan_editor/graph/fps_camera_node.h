#pragma once
#include "camera_node.h"

// ============================================================================
// FPSCameraNode - First-person fly camera (like Blender fly mode)
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

    // FPS camera uses yaw/pitch for orientation
    float yaw{0.0f};   // Horizontal angle (radians)
    float pitch{0.0f}; // Vertical angle (radians)

    // Control speeds
    float moveSpeed{5.0f};     // Movement speed
    float rotateSpeed{0.005f}; // Mouse rotation sensitivity

    // Input handling - override base class virtuals
    void processKeyboard(
        float deltaTime,
        bool forward,
        bool backward,
        bool left,
        bool right,
        bool up,
        bool down
    ) override;
    void processMouseDrag(float deltaX, float deltaY) override;

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
    void updateTargetFromOrientation();
    void initializeOrientationFromTarget();

    // Initial FPS parameters for reset
    float initialYaw{0.0f};
    float initialPitch{0.0f};
};
