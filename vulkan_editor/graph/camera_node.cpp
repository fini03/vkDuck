#include "camera_node.h"
#include "node_graph.h"
#include "../util/logger.h"
#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include "model_node.h" // For GLTFCamera
#include <imgui.h>

namespace {
constexpr float PADDING_X = 10.0f;
}

// ============================================================================
// CameraNodeBase Implementation
// ============================================================================

CameraNodeBase::CameraNodeBase()
    : Node() {
    name = "Camera";
    createDefaultPins();
}

CameraNodeBase::CameraNodeBase(int id)
    : Node(id) {
    name = "Camera";
    createDefaultPins();
}

CameraNodeBase::~CameraNodeBase() {}

void CameraNodeBase::createDefaultPins() {
    cameraPin.id = ax::NodeEditor::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";
}

void CameraNodeBase::updateMatrices() {
    // View matrix (LookAt)
    cameraData.view = glm::lookAt(position, target, up);

    // Projection matrix (Perspective)
    cameraData.proj = glm::perspective(
        glm::radians(fov), aspectRatio, nearPlane, farPlane
    );

    // Flip Y for Vulkan
    cameraData.proj[1][1] *= -1;

    // Inverse view
    cameraData.invView = glm::inverse(cameraData.view);

    // Note: UBO update is handled by Camera::recordCommands() for non-fixed cameras
}

void CameraNodeBase::clearPrimitives() {
    cameraUbo = nullptr;
    cameraPrimitive = nullptr;
    cameraUboArray = {};
}

void CameraNodeBase::createPrimitives(primitives::Store& store) {
    assert(!name.empty() && "Camera node must have a name");

    // Create UniformBuffer primitive for camera matrices
    primitives::StoreHandle hUbo = store.newUniformBuffer();
    assert(hUbo.isValid() && "Failed to create camera UBO");

    cameraUbo = &store.uniformBuffers[hUbo.handle];
    assert(cameraUbo != nullptr);
    assert(!cameraUbo->name.empty() && "UBO should have auto-generated name");

    // Point to our persistent camera data
    cameraUbo->dataType = primitives::UniformDataType::Camera;
    cameraUbo->data = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(&cameraData),
        sizeof(primitives::CameraData)
    );
    cameraUbo->extraData = &cameraType;

    // Create Camera primitive for code generation
    primitives::StoreHandle hCamera = store.newCamera();
    assert(hCamera.isValid() && "Failed to create Camera primitive");

    cameraPrimitive = &store.cameras[hCamera.handle];
    assert(cameraPrimitive != nullptr);

    cameraPrimitive->name = name;  // Use node name for camera controller
    cameraPrimitive->cameraType = cameraType;
    cameraPrimitive->ubo = hUbo;

    // Copy camera parameters for code generation
    cameraPrimitive->position = position;
    cameraPrimitive->target = target;
    cameraPrimitive->up = up;
    cameraPrimitive->fov = fov;
    cameraPrimitive->nearPlane = nearPlane;
    cameraPrimitive->farPlane = farPlane;

    // Create array with single Camera for descriptor set binding
    cameraUboArray = store.newArray();
    assert(cameraUboArray.isValid() && "Failed to create camera UBO array");

    auto& array = store.arrays[cameraUboArray.handle];
    array.type = primitives::Type::UniformBuffer;
    array.handles = {hUbo.handle};

    // Update matrices (camera data will be used by UBO)
    updateMatrices();

    Log::debug("CameraNodeBase", "Created camera '{}' with UBO '{}'",
               name, cameraUbo->name);
}

void CameraNodeBase::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::StoreHandle>>& outputs
) const {
    if (cameraUboArray.isValid()) {
        outputs.push_back({cameraPin.id, cameraUboArray});
    }
}

nlohmann::json CameraNodeBase::toJson() const {
    nlohmann::json j;
    // Node base info
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y}; // UI position

    // Camera base parameters
    j["fov"] = fov;
    j["nearPlane"] = nearPlane;
    j["farPlane"] = farPlane;
    j["aspectRatio"] = aspectRatio;

    // Camera 3D position/orientation
    j["cameraPosition"] = {position.x, position.y, position.z};
    j["target"] = {target.x, target.y, target.z};
    j["up"] = {up.x, up.y, up.z};

    // Pin info
    j["outputPins"] = {
        {{"id", cameraPin.id.Get()},
         {"type", static_cast<int>(cameraPin.type)},
         {"label", cameraPin.label}}
    };

    return j;
}

void CameraNodeBase::fromJson(const nlohmann::json& j) {
    // Node base info
    name = j.value("name", "Camera");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }

    // Camera base parameters
    fov = j.value("fov", 45.0f);
    nearPlane = j.value("nearPlane", 0.1f);
    farPlane = j.value("farPlane", 1000.0f);
    aspectRatio = j.value("aspectRatio", 16.0f / 9.0f);

    // Camera 3D position/orientation
    if (j.contains("cameraPosition") &&
        j["cameraPosition"].is_array()) {
        position = glm::vec3(
            j["cameraPosition"][0].get<float>(),
            j["cameraPosition"][1].get<float>(),
            j["cameraPosition"][2].get<float>()
        );
    }
    if (j.contains("target") && j["target"].is_array()) {
        target = glm::vec3(
            j["target"][0].get<float>(), j["target"][1].get<float>(),
            j["target"][2].get<float>()
        );
    }
    if (j.contains("up") && j["up"].is_array()) {
        up = glm::vec3(
            j["up"][0].get<float>(), j["up"][1].get<float>(),
            j["up"][2].get<float>()
        );
    }

    // Restore pin ID
    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        for (const auto& pinJson : j["outputPins"]) {
            if (pinJson.value("label", "") == "Camera") {
                cameraPin.id =
                    ax::NodeEditor::PinId(pinJson["id"].get<int>());
            }
        }
    }

    updateMatrices();
}

void CameraNodeBase::renderCameraNode(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    ImColor headerColor,
    const NodeGraph& graph
) const {
    namespace ed = ax::NodeEditor;
    std::vector<std::string> pinLabels = {cameraPin.label};
    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(30, 30, 30, 80));

    builder.Begin(id);
    builder.Header(headerColor);

    float availWidth = nodeWidth - PADDING_X * 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    if (!isRenaming) {
        if (textSize.x < availWidth) {
            float centerOffset = (availWidth - textSize.x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            const_cast<CameraNodeBase*>(this)->isRenaming = true;
        }
    } else {
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText(
            "##NodeName", nameBuffer, sizeof(nameBuffer),
            ImGuiInputTextFlags_AutoSelectAll
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<CameraNodeBase*>(this)->name = nameBuffer;
            const_cast<CameraNodeBase*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    DrawOutputPin(
        cameraPin.id, cameraPin.label, static_cast<int>(cameraPin.type),
        graph.isPinLinked(cameraPin.id), nodeWidth, builder
    );

    builder.End();
    ed::PopStyleColor();
}

// ============================================================================
// OrbitalCameraNode Implementation
// ============================================================================

OrbitalCameraNode::OrbitalCameraNode()
    : CameraNodeBase() {
    name = "Orbital Camera";
    cameraType = primitives::CameraType::Orbital;
    // Save initial state for reset functionality
    saveInitialState();
}

OrbitalCameraNode::OrbitalCameraNode(int id)
    : CameraNodeBase(id) {
    name = "Orbital Camera";
    cameraType = primitives::CameraType::Orbital;
    // Save initial state for reset functionality
    saveInitialState();
}

OrbitalCameraNode::~OrbitalCameraNode() {}

void OrbitalCameraNode::createPrimitives(primitives::Store& store) {
    // Call base class to create UBO and Camera primitive
    CameraNodeBase::createPrimitives(store);

    // Copy Orbital-specific parameters for code generation
    if (cameraPrimitive) {
        cameraPrimitive->distance = distance;
        cameraPrimitive->yaw = yaw;
        cameraPrimitive->pitch = pitch;
        cameraPrimitive->moveSpeed = moveSpeed;
        cameraPrimitive->rotateSpeed = rotateSpeed;
        cameraPrimitive->zoomSpeed = zoomSpeed;
    }
}

void OrbitalCameraNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Black for camera nodes
    renderCameraNode(builder, ImColor(0, 0, 0), graph);
}

nlohmann::json OrbitalCameraNode::toJson() const {
    // Start with base class serialization
    nlohmann::json j = CameraNodeBase::toJson();
    j["type"] = "orbital_camera";

    // Add orbital-specific parameters
    j["distance"] = distance;
    j["yaw"] = yaw;
    j["pitch"] = pitch;
    j["moveSpeed"] = moveSpeed;
    j["rotateSpeed"] = rotateSpeed;
    j["zoomSpeed"] = zoomSpeed;

    return j;
}

void OrbitalCameraNode::fromJson(const nlohmann::json& j) {
    // Call base class first
    CameraNodeBase::fromJson(j);

    // Restore orbital-specific parameters
    distance = j.value("distance", 5.0f);
    yaw = j.value("yaw", 0.0f);
    pitch = j.value("pitch", 0.0f);
    moveSpeed = j.value("moveSpeed", 5.0f);
    rotateSpeed = j.value("rotateSpeed", 0.005f);
    zoomSpeed = j.value("zoomSpeed", 0.5f);

    updateMatrices();
}

void OrbitalCameraNode::processKeyboard(
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

    // Move target (camera follows in orbit mode)
    if (forward) {
        target += front * velocity;
    }
    if (backward) {
        target -= front * velocity;
    }
    if (left) {
        target -= right_dir * velocity;
    }
    if (right) {
        target += right_dir * velocity;
    }
    if (upKey) {
        target += up * velocity;
    }
    if (downKey) {
        target -= up * velocity;
    }

    // Update position based on orbit
    updatePositionFromOrbit();
    updateMatrices();
}

void OrbitalCameraNode::processMouseDrag(
    float deltaX,
    float deltaY
) {
    yaw -= deltaX * rotateSpeed;
    pitch -= deltaY * rotateSpeed;

    // Clamp pitch to avoid flipping
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    updatePositionFromOrbit();
    updateMatrices();
}

void OrbitalCameraNode::processScroll(float delta) {
    distance -= delta * zoomSpeed;
    distance = glm::clamp(distance, 0.5f, 100.0f);

    updatePositionFromOrbit();
    updateMatrices();
}

void OrbitalCameraNode::updatePositionFromOrbit() {
    // Calculate position from spherical coordinates around target
    position.x = target.x + distance * cos(pitch) * sin(yaw);
    position.y = target.y + distance * sin(pitch);
    position.z = target.z + distance * cos(pitch) * cos(yaw);
}

void CameraNodeBase::saveInitialState() {
    initialPosition = position;
    initialTarget = target;
    initialUp = up;
    initialStateSaved = true;
}

void CameraNodeBase::resetToInitialState() {
    if (!initialStateSaved) return;
    position = initialPosition;
    target = initialTarget;
    up = initialUp;
    updateMatrices();
}

void OrbitalCameraNode::applyGLTFCamera(const GLTFCamera& gltfCamera) {
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
    glm::vec3 forward =
        -glm::normalize(glm::vec3(gltfCamera.transform[2]));
    target = position + forward * distance;

    // Extract up vector from transform
    up = glm::normalize(glm::vec3(gltfCamera.transform[1]));

    // Recalculate orbit parameters from position
    glm::vec3 offset = position - target;
    distance = glm::length(offset);
    if (distance > 0.001f) {
        pitch = asin(offset.y / distance);
        yaw = atan2(offset.x, offset.z);
    }

    updateMatrices();

    Log::debug(
        "OrbitalCameraNode",
        "Applied GLTF camera '{}' - FOV: {}, Pos: ({}, {}, {})",
        gltfCamera.name, fov, position.x, position.y, position.z
    );

    // Save as new initial state for reset
    saveInitialState();
}

void OrbitalCameraNode::saveInitialState() {
    CameraNodeBase::saveInitialState();
    initialDistance = distance;
    initialYaw = yaw;
    initialPitch = pitch;
}

void OrbitalCameraNode::resetToInitialState() {
    if (!initialStateSaved) return;
    CameraNodeBase::resetToInitialState();
    distance = initialDistance;
    yaw = initialYaw;
    pitch = initialPitch;
    updatePositionFromOrbit();
    updateMatrices();
}
