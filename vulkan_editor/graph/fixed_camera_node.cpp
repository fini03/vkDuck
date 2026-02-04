#include "fixed_camera_node.h"
#include "node_graph.h"
#include "external/utilities/builders.h"
#include <imgui.h>

FixedCameraNode::FixedCameraNode()
    : CameraNodeBase() {
    name = "Fixed Camera";
}

FixedCameraNode::FixedCameraNode(int id)
    : CameraNodeBase(id) {
    name = "Fixed Camera";
}

FixedCameraNode::~FixedCameraNode() {}

void FixedCameraNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Black for camera nodes
    renderCameraNode(builder, ImColor(0, 0, 0), graph);
}

nlohmann::json FixedCameraNode::toJson() const {
    // Start with base class serialization
    nlohmann::json j = CameraNodeBase::toJson();
    j["type"] = "fixed_camera";
    return j;
}

void FixedCameraNode::fromJson(const nlohmann::json& j) {
    // Fixed camera has no extra fields, just use base class
    CameraNodeBase::fromJson(j);
}
