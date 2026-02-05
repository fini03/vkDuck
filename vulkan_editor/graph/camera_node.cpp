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
    // Initialize controller with default Fixed camera type
    controller.init(CameraType::Fixed,
        glm::vec3(0.0f, 0.0f, 5.0f),  // position
        glm::vec3(0.0f, 0.0f, 0.0f),  // target
        glm::vec3(0.0f, 1.0f, 0.0f),  // up
        0.0f, 0.0f, 5.0f,              // yaw, pitch, distance
        5.0f, 0.005f, 0.5f,            // moveSpeed, rotateSpeed, zoomSpeed
        45.0f, 0.1f, 1000.0f           // fov, near, far
    );
    createDefaultPins();
}

CameraNodeBase::CameraNodeBase(int id)
    : Node(id) {
    name = "Camera";
    // Initialize controller with default Fixed camera type
    controller.init(CameraType::Fixed,
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 5.0f,
        5.0f, 0.005f, 0.5f,
        45.0f, 0.1f, 1000.0f
    );
    createDefaultPins();
}

CameraNodeBase::~CameraNodeBase() {}

void CameraNodeBase::createDefaultPins() {
    cameraPin.id = ax::NodeEditor::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";
}

void CameraNodeBase::updateMatrices() {
    // Use library's CameraController to compute matrices
    cameraData = controller.getCameraData();
}

void CameraNodeBase::processKeyboard(
    float deltaTime,
    bool forward, bool backward,
    bool left, bool right,
    bool upKey, bool downKey
) {
    // Delegate to library's CameraController
    controller.processKeyboard(deltaTime, forward, backward, left, right, upKey, downKey);
    updateMatrices();
}

void CameraNodeBase::processMouseDrag(float deltaX, float deltaY) {
    // Delegate to library's CameraController
    controller.processMouseDrag(deltaX, deltaY);
    updateMatrices();
}

void CameraNodeBase::processScroll(float delta) {
    // Delegate to library's CameraController
    controller.processScroll(delta);
    updateMatrices();
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
        sizeof(CameraData)
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

    // Copy camera parameters for code generation (from controller)
    cameraPrimitive->position = controller.position;
    cameraPrimitive->target = controller.target;
    cameraPrimitive->up = controller.up;
    cameraPrimitive->fov = controller.fov;
    cameraPrimitive->nearPlane = controller.nearPlane;
    cameraPrimitive->farPlane = controller.farPlane;

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

    // Camera base parameters (from controller)
    j["fov"] = controller.fov;
    j["nearPlane"] = controller.nearPlane;
    j["farPlane"] = controller.farPlane;
    j["aspectRatio"] = controller.aspectRatio;

    // Camera 3D position/orientation (from controller)
    j["cameraPosition"] = {controller.position.x, controller.position.y, controller.position.z};
    j["target"] = {controller.target.x, controller.target.y, controller.target.z};
    j["up"] = {controller.up.x, controller.up.y, controller.up.z};

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

    // Camera base parameters (to controller)
    controller.fov = j.value("fov", 45.0f);
    controller.nearPlane = j.value("nearPlane", 0.1f);
    controller.farPlane = j.value("farPlane", 1000.0f);
    controller.aspectRatio = j.value("aspectRatio", 16.0f / 9.0f);

    // Camera 3D position/orientation (to controller)
    if (j.contains("cameraPosition") && j["cameraPosition"].is_array()) {
        controller.position = glm::vec3(
            j["cameraPosition"][0].get<float>(),
            j["cameraPosition"][1].get<float>(),
            j["cameraPosition"][2].get<float>()
        );
    }
    if (j.contains("target") && j["target"].is_array()) {
        controller.target = glm::vec3(
            j["target"][0].get<float>(), j["target"][1].get<float>(),
            j["target"][2].get<float>()
        );
    }
    if (j.contains("up") && j["up"].is_array()) {
        controller.up = glm::vec3(
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

void CameraNodeBase::saveInitialState() {
    initialPosition = controller.position;
    initialTarget = controller.target;
    initialUp = controller.up;
    initialStateSaved = true;
}

void CameraNodeBase::resetToInitialState() {
    if (!initialStateSaved) return;
    controller.position = initialPosition;
    controller.target = initialTarget;
    controller.up = initialUp;
    updateMatrices();
}

// ============================================================================
// OrbitalCameraNode Implementation
// ============================================================================

OrbitalCameraNode::OrbitalCameraNode()
    : CameraNodeBase() {
    name = "Orbital Camera";
    cameraType = primitives::CameraType::Orbital;
    // Re-initialize controller with Orbital type
    controller.init(CameraType::Orbital,
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 5.0f,
        5.0f, 0.005f, 0.5f,
        45.0f, 0.1f, 1000.0f
    );
    saveInitialState();
}

OrbitalCameraNode::OrbitalCameraNode(int id)
    : CameraNodeBase(id) {
    name = "Orbital Camera";
    cameraType = primitives::CameraType::Orbital;
    // Re-initialize controller with Orbital type
    controller.init(CameraType::Orbital,
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 5.0f,
        5.0f, 0.005f, 0.5f,
        45.0f, 0.1f, 1000.0f
    );
    saveInitialState();
}

OrbitalCameraNode::~OrbitalCameraNode() {}

void OrbitalCameraNode::createPrimitives(primitives::Store& store) {
    // Call base class to create UBO and Camera primitive
    CameraNodeBase::createPrimitives(store);

    // Copy Orbital-specific parameters for code generation (from controller)
    if (cameraPrimitive) {
        cameraPrimitive->distance = controller.distance;
        cameraPrimitive->yaw = controller.yaw;
        cameraPrimitive->pitch = controller.pitch;
        cameraPrimitive->moveSpeed = controller.moveSpeed;
        cameraPrimitive->rotateSpeed = controller.rotateSpeed;
        cameraPrimitive->zoomSpeed = controller.zoomSpeed;
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

    // Add orbital-specific parameters (from controller)
    j["distance"] = controller.distance;
    j["yaw"] = controller.yaw;
    j["pitch"] = controller.pitch;
    j["moveSpeed"] = controller.moveSpeed;
    j["rotateSpeed"] = controller.rotateSpeed;
    j["zoomSpeed"] = controller.zoomSpeed;

    return j;
}

void OrbitalCameraNode::fromJson(const nlohmann::json& j) {
    // Call base class first
    CameraNodeBase::fromJson(j);

    // Restore orbital-specific parameters (to controller)
    controller.distance = j.value("distance", 5.0f);
    controller.yaw = j.value("yaw", 0.0f);
    controller.pitch = j.value("pitch", 0.0f);
    controller.moveSpeed = j.value("moveSpeed", 5.0f);
    controller.rotateSpeed = j.value("rotateSpeed", 0.005f);
    controller.zoomSpeed = j.value("zoomSpeed", 0.5f);

    // Make sure controller type is set correctly
    controller.type = CameraType::Orbital;

    updateMatrices();
}

void OrbitalCameraNode::applyGLTFCamera(const GLTFCamera& gltfCamera) {
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
    controller.target = controller.position + forward * controller.distance;

    // Extract up vector from transform
    controller.up = glm::normalize(glm::vec3(gltfCamera.transform[1]));

    // Recalculate orbit parameters from position
    glm::vec3 offset = controller.position - controller.target;
    controller.distance = glm::length(offset);
    if (controller.distance > 0.001f) {
        controller.pitch = asin(offset.y / controller.distance);
        controller.yaw = atan2(offset.x, offset.z);
    }

    updateMatrices();

    Log::debug(
        "OrbitalCameraNode",
        "Applied GLTF camera '{}' - FOV: {}, Pos: ({}, {}, {})",
        gltfCamera.name, controller.fov, controller.position.x,
        controller.position.y, controller.position.z
    );

    // Save as new initial state for reset
    saveInitialState();
}

void OrbitalCameraNode::saveInitialState() {
    CameraNodeBase::saveInitialState();
    initialDistance = controller.distance;
    initialYaw = controller.yaw;
    initialPitch = controller.pitch;
}

void OrbitalCameraNode::resetToInitialState() {
    if (!initialStateSaved) return;
    CameraNodeBase::resetToInitialState();
    controller.distance = initialDistance;
    controller.yaw = initialYaw;
    controller.pitch = initialPitch;
    // Let the controller update position from orbit params
    controller.processScroll(0.0f);  // Triggers updatePositionFromOrbit internally
    updateMatrices();
}
