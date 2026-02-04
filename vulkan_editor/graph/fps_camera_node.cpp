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
    // Initialize yaw/pitch from default position/target
    initializeOrientationFromTarget();
    // Save initial state for reset functionality
    saveInitialState();
}

FPSCameraNode::FPSCameraNode(int id)
    : CameraNodeBase(id) {
    name = "FPS Camera";
    cameraType = primitives::CameraType::FPS;
    // Initialize yaw/pitch from default position/target
    initializeOrientationFromTarget();
    // Save initial state for reset functionality
    saveInitialState();
}

FPSCameraNode::~FPSCameraNode() {}

void FPSCameraNode::createPrimitives(primitives::Store& store) {
    // Call base class to create UBO and Camera primitive
    CameraNodeBase::createPrimitives(store);

    // Copy FPS-specific parameters for code generation
    if (cameraPrimitive) {
        cameraPrimitive->yaw = yaw;
        cameraPrimitive->pitch = pitch;
        cameraPrimitive->moveSpeed = moveSpeed;
        cameraPrimitive->rotateSpeed = rotateSpeed;
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

    // Add FPS-specific parameters
    j["yaw"] = yaw;
    j["pitch"] = pitch;
    j["moveSpeed"] = moveSpeed;
    j["rotateSpeed"] = rotateSpeed;

    return j;
}

void FPSCameraNode::fromJson(const nlohmann::json& j) {
    // Call base class first
    CameraNodeBase::fromJson(j);

    // Restore FPS-specific parameters
    yaw = j.value("yaw", 0.0f);
    pitch = j.value("pitch", 0.0f);
    moveSpeed = j.value("moveSpeed", 5.0f);
    rotateSpeed = j.value("rotateSpeed", 0.005f);

    updateMatrices();
}

void FPSCameraNode::processKeyboard(
    float deltaTime,
    bool forward,
    bool backward,
    bool left,
    bool right,
    bool upKey,
    bool downKey
) {
    // Calculate camera direction vectors
    glm::vec3 front = glm::normalize(target - position);
    glm::vec3 right_dir = glm::normalize(glm::cross(front, up));

    float velocity = moveSpeed * deltaTime;

    // Move camera position directly (FPS style)
    if (forward) {
        position += front * velocity;
        target += front * velocity;
    }
    if (backward) {
        position -= front * velocity;
        target -= front * velocity;
    }
    if (left) {
        position -= right_dir * velocity;
        target -= right_dir * velocity;
    }
    if (right) {
        position += right_dir * velocity;
        target += right_dir * velocity;
    }
    if (upKey) {
        position += up * velocity;
        target += up * velocity;
    }
    if (downKey) {
        position -= up * velocity;
        target -= up * velocity;
    }

    updateMatrices();
}

void FPSCameraNode::processMouseDrag(float deltaX, float deltaY) {
    // Rotate yaw and pitch
    yaw -= deltaX * rotateSpeed;
    pitch -= deltaY * rotateSpeed;

    // Clamp pitch to avoid flipping
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    updateTargetFromOrientation();
    updateMatrices();
}

void FPSCameraNode::updateTargetFromOrientation() {
    // Calculate front direction from yaw/pitch
    glm::vec3 front;
    front.x = cos(pitch) * sin(yaw);
    front.y = sin(pitch);
    front.z = cos(pitch) * cos(yaw);
    front = glm::normalize(front);

    // Target is in front of the camera position (fixed distance)
    target = position + front * 5.0f;
}

void FPSCameraNode::initializeOrientationFromTarget() {
    // Calculate yaw/pitch from current position and target
    glm::vec3 diff = target - position;
    float length = glm::length(diff);

    // Guard against zero-length direction (position == target)
    if (length < 0.0001f) {
        // Default to looking along -Z axis
        yaw = glm::pi<float>();
        pitch = 0.0f;
        return;
    }

    glm::vec3 direction = diff / length;
    yaw = atan2(direction.x, direction.z);
    pitch = asin(glm::clamp(direction.y, -1.0f, 1.0f));
}

void FPSCameraNode::saveInitialState() {
    CameraNodeBase::saveInitialState();
    initialYaw = yaw;
    initialPitch = pitch;
}

void FPSCameraNode::resetToInitialState() {
    if (!initialStateSaved) return;
    CameraNodeBase::resetToInitialState();
    yaw = initialYaw;
    pitch = initialPitch;
    updateMatrices();
}

void FPSCameraNode::applyGLTFCamera(const GLTFCamera& gltfCamera) {
    // Apply projection settings
    if (gltfCamera.isPerspective) {
        fov = gltfCamera.fov;
        if (gltfCamera.aspectRatio > 0.0f) {
            aspectRatio = gltfCamera.aspectRatio;
        }
    }
    nearPlane = gltfCamera.nearPlane;
    farPlane = gltfCamera.farPlane;

    // Apply position from GLTF transform
    position = gltfCamera.position;

    // Extract forward direction from transform matrix
    // GLTF cameras look down -Z in their local space
    glm::vec3 forward = -glm::normalize(glm::vec3(gltfCamera.transform[2]));
    target = position + forward * 5.0f;

    // Extract up vector from transform
    up = glm::normalize(glm::vec3(gltfCamera.transform[1]));

    // Calculate yaw/pitch from direction
    yaw = atan2(forward.x, forward.z);
    pitch = asin(forward.y);

    updateMatrices();

    Log::debug(
        "FPSCameraNode",
        "Applied GLTF camera '{}' - FOV: {}, Pos: ({}, {}, {})",
        gltfCamera.name, fov, position.x, position.y, position.z
    );

    // Save as new initial state for reset
    saveInitialState();
}
