#pragma once
#include "camera_node.h"

// ============================================================================
// FixedCameraNode - Direct position/target control without orbit
// mechanics
// ============================================================================
class FixedCameraNode : public CameraNodeBase {
public:
    FixedCameraNode();
    FixedCameraNode(int id);
    ~FixedCameraNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    // Serialization override - sets correct type
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Fixed camera has no orbit controls - position and target are set
    // directly The base class position, target, up are used directly

    // Camera type for code generation (Fixed is the default, but explicit)
    primitives::CameraType getCameraType() const override {
        return primitives::CameraType::Fixed;
    }
};
