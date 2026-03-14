#include "multi_ubo_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_node_editor.h>

#include <vkDuck/model_loader.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr const char* LOG_CATEGORY = "MultiUBONode";
}

namespace ed = ax::NodeEditor;

MultiUBONode::MultiUBONode()
    : MultiModelConsumerBase() {
    name = "Multi UBO";
    createDefaultPins();
}

MultiUBONode::MultiUBONode(int id)
    : MultiModelConsumerBase(id) {
    name = "Multi UBO";
    createDefaultPins();
}

MultiUBONode::~MultiUBONode() = default;

void MultiUBONode::createDefaultPins() {
    // Model Matrix output
    modelMatrixPin.id = ed::PinId(GetNextGlobalId());
    modelMatrixPin.type = PinType::UniformBuffer;
    modelMatrixPin.label = "Model matrix";

    // Camera output (selected GLTF camera as UniformBuffer)
    cameraPin.id = ed::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";

    // Light output (combined GLTF lights as UniformBuffer)
    lightPin.id = ed::PinId(GetNextGlobalId());
    lightPin.type = PinType::UniformBuffer;
    lightPin.label = "Light";
}

void MultiUBONode::registerPins(PinRegistry& registry) {
    // Register source input pin from base class
    registerSourceInputPin(registry);

    // Register output pins
    modelMatrixPinHandle = registry.registerPinWithId(
        id, modelMatrixPin.id, modelMatrixPin.type, PinKind::Output,
        modelMatrixPin.label);

    cameraPinHandle = registry.registerPinWithId(
        id, cameraPin.id, cameraPin.type, PinKind::Output, cameraPin.label);

    lightPinHandle = registry.registerPinWithId(
        id, lightPin.id, lightPin.type, PinKind::Output, lightPin.label);
}

bool MultiUBONode::sourceHasCameras() const {
    if (!graph_) return false;
    MultiModelSourceNode* source = findSourceNode(*graph_);
    return source && !source->getMergedCameras().empty();
}

bool MultiUBONode::sourceHasLights() const {
    if (!graph_) return false;
    MultiModelSourceNode* source = findSourceNode(*graph_);
    return source && !source->getMergedLights().empty();
}

std::vector<ed::PinId> MultiUBONode::getPinsToUnlink() const {
    std::vector<ed::PinId> pins;

    if (!sourceHasCameras()) {
        pins.push_back(cameraPin.id);
    }

    if (!sourceHasLights()) {
        pins.push_back(lightPin.id);
    }

    return pins;
}

nlohmann::json MultiUBONode::toJson() const {
    nlohmann::json j;
    j["type"] = "multi_ubo";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};
    j["selectedCameraIndex"] = selectedCameraIndex;

    // Serialize input pin (source connection)
    sourceInputPinToJson(j);

    // Serialize output pins
    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back({{"id", modelMatrixPin.id.Get()},
                               {"type", static_cast<int>(modelMatrixPin.type)},
                               {"label", modelMatrixPin.label}});
    j["outputPins"].push_back({{"id", cameraPin.id.Get()},
                               {"type", static_cast<int>(cameraPin.type)},
                               {"label", cameraPin.label}});
    j["outputPins"].push_back({{"id", lightPin.id.Get()},
                               {"type", static_cast<int>(lightPin.type)},
                               {"label", lightPin.label}});

    return j;
}

void MultiUBONode::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Multi UBO");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position =
            ImVec2(j["position"][0].get<float>(), j["position"][1].get<float>());
    }

    selectedCameraIndex = j.value("selectedCameraIndex", -1);

    // Restore input pin
    sourceInputPinFromJson(j);

    // Restore output pins
    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0)
            modelMatrixPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1)
            cameraPin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2)
            lightPin.id = ed::PinId(pins[2]["id"].get<int>());
    }
}

void MultiUBONode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph) const {
    bool hasCameras = sourceHasCameras();
    bool hasLights = sourceHasLights();

    // Build pin labels based on what's available
    std::vector<std::string> pinLabels = {sourceInputPin.label, modelMatrixPin.label};
    if (hasCameras) {
        pinLabels.push_back(cameraPin.label);
    }
    if (hasLights) {
        pinLabels.push_back(lightPin.label);
    }

    float nodeWidth = calculateConsumerNodeWidth(name, pinLabels);
    renderConsumerNodeHeader(builder, nodeWidth);

    // Draw input pin (source connection)
    DrawInputPin(
        sourceInputPin.id,
        sourceInputPin.label,
        static_cast<int>(sourceInputPin.type),
        nodeGraph.isPinLinked(sourceInputPin.id),
        nodeWidth,
        builder
    );

    // Model matrix pin (always shown)
    DrawOutputPin(modelMatrixPin.id, modelMatrixPin.label,
                  static_cast<int>(modelMatrixPin.type),
                  nodeGraph.isPinLinked(modelMatrixPin.id), nodeWidth, builder);

    // Camera pin (only if source has cameras)
    if (hasCameras) {
        DrawOutputPin(cameraPin.id, cameraPin.label,
                      static_cast<int>(cameraPin.type),
                      nodeGraph.isPinLinked(cameraPin.id), nodeWidth, builder);
    }

    // Light pin (only if source has lights)
    if (hasLights) {
        DrawOutputPin(lightPin.id, lightPin.label,
                      static_cast<int>(lightPin.type),
                      nodeGraph.isPinLinked(lightPin.id), nodeWidth, builder);
    }

    builder.End();
    ed::PopStyleColor();
}

void MultiUBONode::clearPrimitives() {
    modelMatrixArray_ = {};
    cameraUboArray_ = {};
    lightUboArray_ = {};
    cameraUbo_ = nullptr;
    lightUbo_ = nullptr;
    lightPrimitive_ = nullptr;
    modelMatricesData_.clear();
}

void MultiUBONode::createPrimitives(primitives::Store& store) {
    if (!graph_) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: no graph reference");
        return;
    }

    MultiModelSourceNode* source = findSourceNode(*graph_);
    if (!source) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: not connected to source");
        return;
    }

    const auto& ranges = source->getConsolidatedRanges();
    const auto& cameras = source->getMergedCameras();
    const auto& lights = source->getMergedLights();

    if (ranges.empty()) {
        Log::warning(LOG_CATEGORY,
                     "Cannot create primitives: no models loaded in source");
        return;
    }

    // Auto-select first camera if not yet selected
    if (selectedCameraIndex < 0 && !cameras.empty()) {
        const_cast<MultiUBONode*>(this)->selectedCameraIndex = 0;
    }

    // Create model matrix array
    modelMatrixArray_ = store.newArray();
    auto& uboArray = store.arrays[modelMatrixArray_.handle];
    uboArray.type = primitives::Type::UniformBuffer;
    uboArray.handles.resize(ranges.size());

    glm::mat4 modelMatrix{1.0f};
    glm::mat3 normalMat3 =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    glm::mat4 normalMatrix = glm::mat4(normalMat3);

    // Store matrices permanently
    modelMatricesData_.clear();
    modelMatricesData_.resize(ranges.size());
    for (auto& matrices : modelMatricesData_) {
        matrices.model = modelMatrix;
        matrices.normalMatrix = normalMatrix;
    }

    // Create UBO primitives pointing to persistent storage
    for (size_t i = 0; i < ranges.size(); ++i) {
        primitives::StoreHandle hUBO = store.newUniformBuffer();
        primitives::UniformBuffer& ubo = store.uniformBuffers[hUBO.handle];

        ubo.data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&modelMatricesData_[i]),
            sizeof(ModelMatrices));

        uboArray.handles[i] = hUBO.handle;

        Log::debug(LOG_CATEGORY,
                   "Created UniformBuffer primitive for range {} with model "
                   "and normal matrix",
                   i);
    }

    // Create camera UBO if source has cameras
    if (!cameras.empty()) {
        primitives::StoreHandle hCameraUbo = store.newUniformBuffer();
        cameraUbo_ = &store.uniformBuffers[hCameraUbo.handle];

        cameraUbo_->dataType = primitives::UniformDataType::Camera;
        cameraUbo_->data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&cameraData), sizeof(ModelCameraData));
        cameraUbo_->extraData = &cameraType_;

        cameraUboArray_ = store.newArray();
        auto& camArray = store.arrays[cameraUboArray_.handle];
        camArray.type = primitives::Type::UniformBuffer;
        camArray.handles = {hCameraUbo.handle};

        updateCameraFromSelection();

        Log::debug(LOG_CATEGORY, "Created camera UBO primitive from {} cameras",
                   cameras.size());
    }

    // Create light UBO if source has lights
    if (!lights.empty()) {
        updateLightsFromMerged();

        primitives::StoreHandle hLightUbo = store.newUniformBuffer();
        lightUbo_ = &store.uniformBuffers[hLightUbo.handle];

        lightUbo_->dataType = primitives::UniformDataType::Light;
        lightUbo_->data = lightsBuffer_.getSpan();

        // Create Light primitive for code generation
        primitives::StoreHandle hLight = store.newLight();
        lightPrimitive_ = &store.lights[hLight.handle];
        lightPrimitive_->name = name + "_lights";
        lightPrimitive_->ubo = hLightUbo;
        lightPrimitive_->numLights =
            static_cast<int>(lightsBuffer_.lights.size());

        // Copy light data for code generation
        lightPrimitive_->lights.resize(lightPrimitive_->numLights);
        for (int i = 0; i < lightPrimitive_->numLights; ++i) {
            lightPrimitive_->lights[i].position =
                lightsBuffer_.lights[i].position;
            lightPrimitive_->lights[i].color = lightsBuffer_.lights[i].color;
            lightPrimitive_->lights[i].radius = lightsBuffer_.lights[i].radius;
            lightPrimitive_->lights[i].intensity =
                lightsBuffer_.lights[i].intensity;
        }

        lightUboArray_ = store.newArray();
        auto& lgtArray = store.arrays[lightUboArray_.handle];
        lgtArray.type = primitives::Type::UniformBuffer;
        lgtArray.handles = {hLightUbo.handle};

        Log::debug(LOG_CATEGORY, "Created light UBO with {} lights ({} bytes)",
                   lightsBuffer_.header.numLights, lightUbo_->data.size());
    }

    Log::info(LOG_CATEGORY, "Created UBOs for {} ranges from source",
              ranges.size());
}

void MultiUBONode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>&
        outputs) const {
    if (modelMatrixArray_.isValid()) {
        outputs.push_back({modelMatrixPin.id, modelMatrixArray_});
    }
    if (cameraUboArray_.isValid()) {
        outputs.push_back({cameraPin.id, cameraUboArray_});
    }
    if (lightUboArray_.isValid()) {
        outputs.push_back({lightPin.id, lightUboArray_});
    }
}

void MultiUBONode::updateCameraFromSelection() {
    if (!graph_) return;

    MultiModelSourceNode* source = findSourceNode(*graph_);
    if (!source) return;

    const auto& cameras = source->getMergedCameras();

    // Check if we have a valid camera selected
    if (selectedCameraIndex < 0 ||
        selectedCameraIndex >= static_cast<int>(cameras.size())) {
        // No camera selected - use default identity matrices
        cameraData.view = glm::mat4(1.0f);
        cameraData.invView = glm::mat4(1.0f);
        cameraData.proj = glm::mat4(1.0f);
        cameraData.invProj = glm::mat4(1.0f);
        return;
    }

    const GLTFCamera& gltfCam = cameras[selectedCameraIndex];

    // Compute view matrix from GLTF camera transform
    glm::vec3 position = gltfCam.position;
    glm::vec3 forward = -glm::normalize(glm::vec3(gltfCam.transform[2]));
    glm::vec3 up = glm::normalize(glm::vec3(gltfCam.transform[1]));
    glm::vec3 target = position + forward;

    cameraData.view = glm::lookAt(position, target, up);
    cameraData.invView = glm::inverse(cameraData.view);

    // Compute projection matrix
    if (gltfCam.isPerspective) {
        float fovRadians = glm::radians(gltfCam.fov);
        float aspect =
            gltfCam.aspectRatio > 0.0f ? gltfCam.aspectRatio : aspectRatio;
        cameraData.proj =
            glm::perspective(fovRadians, aspect, gltfCam.nearPlane,
                             gltfCam.farPlane);
    } else {
        // Orthographic projection
        cameraData.proj =
            glm::ortho(-gltfCam.xmag, gltfCam.xmag, -gltfCam.ymag, gltfCam.ymag,
                       gltfCam.nearPlane, gltfCam.farPlane);
    }

    // Flip Y for Vulkan
    cameraData.proj[1][1] *= -1;
    cameraData.invProj = glm::inverse(cameraData.proj);

    Log::debug(LOG_CATEGORY,
               "Updated camera from merged '{}' - Pos: ({:.2f}, {:.2f}, "
               "{:.2f})",
               gltfCam.name, position.x, position.y, position.z);
}

void MultiUBONode::updateLightsFromMerged() {
    if (!graph_) return;

    MultiModelSourceNode* source = findSourceNode(*graph_);
    if (!source) return;

    const auto& lights = source->getMergedLights();

    // Resize to match merged lights
    lightsBuffer_.lights.resize(lights.size());

    for (size_t i = 0; i < lights.size(); ++i) {
        const auto& gltfLight = lights[i];
        auto& light = lightsBuffer_.lights[i];

        light.position = gltfLight.position;
        light.radius = gltfLight.range > 0.0f ? gltfLight.range : 10.0f;
        light.color = gltfLight.color;
        light.intensity = gltfLight.intensity;

        Log::debug(LOG_CATEGORY,
                   "Light '{}' - Pos: ({:.2f}, {:.2f}, {:.2f}), Intensity: "
                   "{:.2f}",
                   gltfLight.name, light.position.x, light.position.y,
                   light.position.z, light.intensity);
    }

    lightsBuffer_.updateGpuBuffer();
}
