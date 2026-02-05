#include "fps_camera_node.h"
#include "node_graph.h"
#include "../util/logger.h"
#include "external/utilities/builders.h"
#include "model_node.h" // For GLTFCamera
#include <imgui.h>

FPSCameraNode::FPSCameraNode()
    : CameraNodeBase() {
    name = "FPS Camera";
    cameraType = primitives::CameraType::FPS;
    // Re-initialize controller with FPS type
    controller.init(CameraType::FPS,
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 5.0f,
        5.0f, 0.005f, 0.5f,
        45.0f, 0.1f, 1000.0f
    );
    saveInitialState();
}

FPSCameraNode::FPSCameraNode(int id)
    : CameraNodeBase(id) {
    name = "FPS Camera";
    cameraType = primitives::CameraType::FPS;
    // Re-initialize controller with FPS type
    controller.init(CameraType::FPS,
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 5.0f,
        5.0f, 0.005f, 0.5f,
        45.0f, 0.1f, 1000.0f
    );
    saveInitialState();
}

FPSCameraNode::~FPSCameraNode() {}

void FPSCameraNode::createPrimitives(primitives::Store& store) {
    // Call base class to create UBO and Camera primitive
    CameraNodeBase::createPrimitives(store);

    // Copy FPS-specific parameters for code generation (from controller)
    if (cameraPrimitive) {
        cameraPrimitive->yaw = controller.yaw;
        cameraPrimitive->pitch = controller.pitch;
        cameraPrimitive->moveSpeed = controller.moveSpeed;
        cameraPrimitive->rotateSpeed = controller.rotateSpeed;
    }
}

void FPSCameraNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Dark blue for FPS camera nodes
    renderCameraNode(builder, ImColor(0, 50, 100), graph);
}

nlohmann::json FPSCameraNode::toJson() const {
    // Start with base class serialization
    nlohmann::json j = CameraNodeBase::toJson();
    j["type"] = "fps_camera";

    // Add FPS-specific parameters (from controller)
    j["yaw"] = controller.yaw;
    j["pitch"] = controller.pitch;
    j["moveSpeed"] = controller.moveSpeed;
    j["rotateSpeed"] = controller.rotateSpeed;

    return j;
}

void FPSCameraNode::fromJson(const nlohmann::json& j) {
    // Call base class first
    CameraNodeBase::fromJson(j);

    // Restore FPS-specific parameters (to controller)
    controller.yaw = j.value("yaw", 0.0f);
    controller.pitch = j.value("pitch", 0.0f);
    controller.moveSpeed = j.value("moveSpeed", 5.0f);
    controller.rotateSpeed = j.value("rotateSpeed", 0.005f);

    // Make sure controller type is set correctly
    controller.type = CameraType::FPS;

    updateMatrices();
}

void FPSCameraNode::saveInitialState() {
    CameraNodeBase::saveInitialState();
    initialYaw = controller.yaw;
    initialPitch = controller.pitch;
}

void FPSCameraNode::resetToInitialState() {
    if (!initialStateSaved) return;
    CameraNodeBase::resetToInitialState();
    controller.yaw = initialYaw;
    controller.pitch = initialPitch;
    // Let the controller recompute target from orientation
    controller.processMouseDrag(0.0f, 0.0f);
    updateMatrices();
}

void FPSCameraNode::applyGLTFCamera(const GLTFCamera& gltfCamera) {
    // Apply projection settings to controller
    if (gltfCamera.isPerspective) {
        controller.fov = gltfCamera.fov;
        if (gltfCamera.aspectRatio > 0.0f) {
            controller.aspectRatio = gltfCamera.aspectRatio;
        }
    }
    controller.nearPlane = gltfCamera.nearPlane;
    controller.farPlane = gltfCamera.farPlane;

    // Apply position from GLTF transform
    controller.position = gltfCamera.position;

    // Extract forward direction from transform matrix
    // GLTF cameras look down -Z in their local space
    glm::vec3 forward = -glm::normalize(glm::vec3(gltfCamera.transform[2]));
    controller.target = controller.position + forward * 5.0f;

    // Extract up vector from transform
    controller.up = glm::normalize(glm::vec3(gltfCamera.transform[1]));

    // Calculate yaw/pitch from direction
    controller.yaw = atan2(forward.x, forward.z);
    controller.pitch = asin(forward.y);

    updateMatrices();

    Log::debug(
        "FPSCameraNode",
        "Applied GLTF camera '{}' - FOV: {}, Pos: ({}, {}, {})",
        gltfCamera.name, controller.fov, controller.position.x,
        controller.position.y, controller.position.z
    );

    // Save as new initial state for reset
    saveInitialState();
}
