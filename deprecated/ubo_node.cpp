#include "ubo_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_node_editor.h>

#include <vkDuck/model_loader.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr const char* LOG_CATEGORY = "UBONode";
}

namespace ed = ax::NodeEditor;

UBONode::UBONode()
    : ModelNodeBase() {
    name = "UBO";
    createDefaultPins();
}

UBONode::UBONode(int id)
    : ModelNodeBase(id) {
    name = "UBO";
    createDefaultPins();
}

UBONode::~UBONode() = default;

void UBONode::createDefaultPins() {
    // Model Matrix output
    modelMatrixPin.id = ed::PinId(GetNextGlobalId());
    modelMatrixPin.type = PinType::UniformBuffer;
    modelMatrixPin.label = "Model matrix";

    // Camera output (selected GLTF camera as UniformBuffer)
    cameraPin.id = ed::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";

    // Light output (selected GLTF light as UniformBuffer)
    lightPin.id = ed::PinId(GetNextGlobalId());
    lightPin.type = PinType::UniformBuffer;
    lightPin.label = "Light";
}

void UBONode::registerPins(PinRegistry& registry) {
    modelMatrixPinHandle = registry.registerPinWithId(
        id,
        modelMatrixPin.id,
        modelMatrixPin.type,
        PinKind::Output,
        modelMatrixPin.label
    );

    cameraPinHandle = registry.registerPinWithId(
        id,
        cameraPin.id,
        cameraPin.type,
        PinKind::Output,
        cameraPin.label
    );

    lightPinHandle = registry.registerPinWithId(
        id,
        lightPin.id,
        lightPin.type,
        PinKind::Output,
        lightPin.label
    );

    usesRegistry_ = true;
}

void UBONode::onModelSet() {
    // Initialize camera/light selection when model is set
    const CachedModel* cached = getCachedModel();
    if (!cached) return;

    if (!cached->cameras.empty()) {
        selectedCameraIndex = 0;
    } else {
        selectedCameraIndex = -1;
    }

    if (!cached->lights.empty()) {
        selectedLightIndex = 0;
    } else {
        selectedLightIndex = -1;
    }
}

std::vector<ed::PinId> UBONode::getPinsToUnlink() const {
    std::vector<ed::PinId> pins;
    const CachedModel* cached = getCachedModel();

    // If no model or model has no cameras, the camera pin should be unlinked
    if (!cached || cached->cameras.empty()) {
        pins.push_back(cameraPin.id);
    }

    // If no model or model has no lights, the light pin should be unlinked
    if (!cached || cached->lights.empty()) {
        pins.push_back(lightPin.id);
    }

    return pins;
}

nlohmann::json UBONode::toJson() const {
    nlohmann::json j = ModelNodeBase::toJson();
    j["type"] = "ubo";
    j["selectedCameraIndex"] = selectedCameraIndex;
    j["selectedLightIndex"] = selectedLightIndex;

    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back({
        {"id", modelMatrixPin.id.Get()},
        {"type", static_cast<int>(modelMatrixPin.type)},
        {"label", modelMatrixPin.label}
    });
    j["outputPins"].push_back({
        {"id", cameraPin.id.Get()},
        {"type", static_cast<int>(cameraPin.type)},
        {"label", cameraPin.label}
    });
    j["outputPins"].push_back({
        {"id", lightPin.id.Get()},
        {"type", static_cast<int>(lightPin.type)},
        {"label", lightPin.label}
    });

    return j;
}

void UBONode::fromJson(const nlohmann::json& j) {
    ModelNodeBase::fromJson(j);

    selectedCameraIndex = j.value("selectedCameraIndex", -1);
    selectedLightIndex = j.value("selectedLightIndex", -1);

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

void UBONode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    const CachedModel* cached = getCachedModel();

    // Build pin labels based on what's available
    std::vector<std::string> pinLabels = {modelMatrixPin.label};
    if (cached && !cached->cameras.empty()) {
        pinLabels.push_back(cameraPin.label);
    }
    if (cached && !cached->lights.empty()) {
        pinLabels.push_back(lightPin.label);
    }

    float nodeWidth = calculateModelNodeWidth(name, pinLabels);
    renderModelNodeHeader(builder, nodeWidth);

    // Model matrix pin (always shown)
    DrawOutputPin(
        modelMatrixPin.id,
        modelMatrixPin.label,
        static_cast<int>(modelMatrixPin.type),
        nodeGraph.isPinLinked(modelMatrixPin.id),
        nodeWidth,
        builder
    );

    // Camera pin (only if model has cameras)
    if (cached && !cached->cameras.empty()) {
        DrawOutputPin(
            cameraPin.id,
            cameraPin.label,
            static_cast<int>(cameraPin.type),
            nodeGraph.isPinLinked(cameraPin.id),
            nodeWidth,
            builder
        );
    }

    // Light pin (only if model has lights)
    if (cached && !cached->lights.empty()) {
        DrawOutputPin(
            lightPin.id,
            lightPin.label,
            static_cast<int>(lightPin.type),
            nodeGraph.isPinLinked(lightPin.id),
            nodeWidth,
            builder
        );
    }

    builder.End();
    ed::PopStyleColor();
}

void UBONode::clearPrimitives() {
    modelMatrixArray_ = {};
    cameraUboArray_ = {};
    lightUboArray_ = {};
    cameraUbo_ = nullptr;
    lightUbo_ = nullptr;
    lightPrimitive_ = nullptr;
    modelMatricesData_.clear();
}

void UBONode::createPrimitives(primitives::Store& store) {
    const CachedModel* cached = getCachedModel();
    if (!cached) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: no model loaded");
        return;
    }

    const auto& modelData = cached->modelData;

    // Create model matrix array
    modelMatrixArray_ = store.newArray();
    auto& uboArray = store.arrays[modelMatrixArray_.handle];
    uboArray.type = primitives::Type::UniformBuffer;
    uboArray.handles.resize(modelData.ranges.size());

    glm::mat4 modelMatrix{1.0f};
    glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    glm::mat4 normalMatrix = glm::mat4(normalMat3);

    // Store matrices permanently
    modelMatricesData_.clear();
    modelMatricesData_.resize(modelData.ranges.size());
    for (auto& matrices : modelMatricesData_) {
        matrices.model = modelMatrix;
        matrices.normalMatrix = normalMatrix;
    }

    // Create UBO primitives pointing to persistent storage
    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        primitives::StoreHandle hUBO = store.newUniformBuffer();
        primitives::UniformBuffer& ubo = store.uniformBuffers[hUBO.handle];

        ubo.data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&modelMatricesData_[i]),
            sizeof(ModelMatrices)
        );

        uboArray.handles[i] = hUBO.handle;

        Log::debug(
            LOG_CATEGORY,
            "Created UniformBuffer primitive for range {} with model and "
            "normal matrix",
            i
        );
    }

    // Create camera UBO if model has cameras
    if (!cached->cameras.empty()) {
        primitives::StoreHandle hCameraUbo = store.newUniformBuffer();
        cameraUbo_ = &store.uniformBuffers[hCameraUbo.handle];

        cameraUbo_->dataType = primitives::UniformDataType::Camera;
        cameraUbo_->data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&cameraData),
            sizeof(ModelCameraData)
        );
        cameraUbo_->extraData = &cameraType_;

        cameraUboArray_ = store.newArray();
        auto& camArray = store.arrays[cameraUboArray_.handle];
        camArray.type = primitives::Type::UniformBuffer;
        camArray.handles = {hCameraUbo.handle};

        updateCameraFromSelection();

        Log::debug(LOG_CATEGORY, "Created camera UBO primitive");
    }

    // Create light UBO if model has lights
    if (!cached->lights.empty()) {
        updateLightsFromGLTF();

        primitives::StoreHandle hLightUbo = store.newUniformBuffer();
        lightUbo_ = &store.uniformBuffers[hLightUbo.handle];

        lightUbo_->dataType = primitives::UniformDataType::Light;
        lightUbo_->data = lightsBuffer_.getSpan();

        // Create Light primitive for code generation (links to the UBO)
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

        Log::debug(
            LOG_CATEGORY,
            "Created light UBO with {} lights ({} bytes)",
            lightsBuffer_.header.numLights,
            lightUbo_->data.size()
        );
    }
}

void UBONode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>&
        outputs
) const {
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

void UBONode::updateCameraFromSelection() {
    const CachedModel* cached = getCachedModel();

    // Check if we have a valid camera selected
    if (!cached || selectedCameraIndex < 0 ||
        selectedCameraIndex >= static_cast<int>(cached->cameras.size())) {
        // No camera selected - use default identity matrices
        cameraData.view = glm::mat4(1.0f);
        cameraData.invView = glm::mat4(1.0f);
        cameraData.proj = glm::mat4(1.0f);
        cameraData.invProj = glm::mat4(1.0f);
        return;
    }

    const GLTFCamera& gltfCam = cached->cameras[selectedCameraIndex];

    // Compute view matrix from GLTF camera transform
    // GLTF cameras look down -Z in their local space
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
        cameraData.proj = glm::perspective(
            fovRadians, aspect, gltfCam.nearPlane, gltfCam.farPlane
        );
    } else {
        // Orthographic projection
        cameraData.proj = glm::ortho(
            -gltfCam.xmag,
            gltfCam.xmag,
            -gltfCam.ymag,
            gltfCam.ymag,
            gltfCam.nearPlane,
            gltfCam.farPlane
        );
    }

    // Flip Y for Vulkan
    cameraData.proj[1][1] *= -1;
    cameraData.invProj = glm::inverse(cameraData.proj);

    Log::debug(
        LOG_CATEGORY,
        "Updated camera from GLTF '{}' - Pos: ({:.2f}, {:.2f}, {:.2f})",
        gltfCam.name,
        position.x,
        position.y,
        position.z
    );
}

void UBONode::updateLightsFromGLTF() {
    const CachedModel* cached = getCachedModel();
    if (!cached) return;

    const auto& gltfLights = cached->lights;

    // Resize to match GLTF lights
    lightsBuffer_.lights.resize(gltfLights.size());

    for (size_t i = 0; i < gltfLights.size(); ++i) {
        const auto& gltfLight = gltfLights[i];
        auto& light = lightsBuffer_.lights[i];

        light.position = gltfLight.position;
        light.radius = gltfLight.range > 0.0f ? gltfLight.range : 10.0f;
        light.color = gltfLight.color;
        light.intensity = gltfLight.intensity;

        Log::debug(
            LOG_CATEGORY,
            "Light '{}' - Pos: ({:.2f}, {:.2f}, {:.2f}), Intensity: {:.2f}",
            gltfLight.name,
            light.position.x,
            light.position.y,
            light.position.z,
            light.intensity
        );
    }

    lightsBuffer_.updateGpuBuffer();
}
