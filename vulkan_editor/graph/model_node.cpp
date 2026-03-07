#include "model_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <set>
#include <vulkan/vk_enum_string_helper.h>

// Use vkDuck's shared implementations
#include <vkDuck/image_loader.h>
#include <vkDuck/model_loader.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr float PADDING_X = 10.0f;
constexpr const char* LOG_CATEGORY = "ModelNode";
}
namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

template <
    typename T,
    std::size_t N>
std::vector<const char*> createEnumStringList(
    const std::array<
        T,
        N>& enumValues,
    const char* (*stringFunc)(T)
) {
    std::vector<const char*> strings;
    strings.reserve(N);
    for (const auto& value : enumValues) {
        strings.push_back(stringFunc(value));
    }
    return strings;
}
const std::vector<const char*> ModelNode::topologyOptions =
    createEnumStringList(
        topologyOptionsEnum, string_VkPrimitiveTopology
    );

// EditorImage destructor is now in model_manager.cpp

ModelNode::ModelNode()
    : Node() {
    name = "Model";
    createDefaultPins();
}

ModelNode::ModelNode(int id)
    : Node(id) {
    name = "Model";
    createDefaultPins();
}

ModelNode::~ModelNode() {
    // Release reference to cached model (check for null during shutdown)
    if (modelHandle_.isValid() && g_modelManager) {
        g_modelManager->removeReference(modelHandle_);
    }
}

nlohmann::json ModelNode::toJson() const {
    nlohmann::json j;
    j["type"] = "model";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};
    j["settings"] = settings.toJson();
    j["selectedCameraIndex"] = selectedCameraIndex;

    // Store all output pins by index
    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back(
        {{"id", modelMatrixPin.id.Get()},
         {"type", static_cast<int>(modelMatrixPin.type)},
         {"label", modelMatrixPin.label}}
    );
    j["outputPins"].push_back(
        {{"id", texturePin.id.Get()},
         {"type", static_cast<int>(texturePin.type)},
         {"label", texturePin.label}}
    );
    j["outputPins"].push_back(
        {{"id", vertexDataPin.id.Get()},
         {"type", static_cast<int>(vertexDataPin.type)},
         {"label", vertexDataPin.label}}
    );
    j["outputPins"].push_back(
        {{"id", cameraPin.id.Get()},
         {"type", static_cast<int>(cameraPin.type)},
         {"label", cameraPin.label}}
    );
    j["outputPins"].push_back(
        {{"id", lightPin.id.Get()},
         {"type", static_cast<int>(lightPin.type)},
         {"label", lightPin.label}}
    );

    j["selectedLightIndex"] = selectedLightIndex;

    return j;
}

void ModelNode::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Model");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }

    if (j.contains("settings")) {
        settings.fromJson(j["settings"]);
    }

    // Note: Model loading is handled separately in pipeline_state.cpp

    // Store camera/light selection to restore after model is loaded
    selectedCameraIndex = j.value("selectedCameraIndex", -1);
    selectedLightIndex = j.value("selectedLightIndex", -1);

    // Restore output pins by index (order: modelMatrix, texture,
    // vertexData, camera, light)
    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0)
            modelMatrixPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1)
            texturePin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2)
            vertexDataPin.id = ed::PinId(pins[2]["id"].get<int>());
        if (pins.size() > 3)
            cameraPin.id = ed::PinId(pins[3]["id"].get<int>());
        if (pins.size() > 4)
            lightPin.id = ed::PinId(pins[4]["id"].get<int>());
    }
}

void ModelNode::createDefaultPins() {
    // Model Matrix output
    modelMatrixPin.id = ed::PinId(GetNextGlobalId());
    modelMatrixPin.type = PinType::UniformBuffer;
    modelMatrixPin.label = "Model matrix";

    // Texture output
    texturePin.id = ed::PinId(GetNextGlobalId());
    texturePin.type = PinType::Image;
    texturePin.label = "Image";

    vertexDataPin.id = ed::PinId(GetNextGlobalId());
    vertexDataPin.type = PinType::VertexData;
    vertexDataPin.label = "Vertex data";

    // Camera output (selected GLTF camera as UniformBuffer)
    cameraPin.id = ed::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";

    // Light output (selected GLTF light as UniformBuffer)
    lightPin.id = ed::PinId(GetNextGlobalId());
    lightPin.type = PinType::UniformBuffer;
    lightPin.label = "Light";
}

void ModelNode::registerPins(PinRegistry& registry) {
    // Register all output pins
    modelMatrixPinHandle = registry.registerPinWithId(
        id,
        modelMatrixPin.id,
        modelMatrixPin.type,
        PinKind::Output,
        modelMatrixPin.label
    );

    texturePinHandle = registry.registerPinWithId(
        id,
        texturePin.id,
        texturePin.type,
        PinKind::Output,
        texturePin.label
    );

    vertexDataPinHandle = registry.registerPinWithId(
        id,
        vertexDataPin.id,
        vertexDataPin.type,
        PinKind::Output,
        vertexDataPin.label
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

    usesRegistry = true;
}

void ModelNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    // Get cached model to check for cameras/lights
    const CachedModel* cached = getCachedModel();

    // Calculate node width - only include camera/light pins if model has them
    std::vector<std::string> pinLabels = {
        vertexDataPin.label, modelMatrixPin.label, texturePin.label
    };
    if (cached && !cached->cameras.empty()) {
        pinLabels.push_back(cameraPin.label);
    }
    if (cached && !cached->lights.empty()) {
        pinLabels.push_back(lightPin.label);
    }
    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(180, 115, 0, 80));

    builder.Begin(id);

    // Draw header - orange for model nodes
    builder.Header(ImColor(255, 165, 0));

    float availWidth = nodeWidth - PADDING_X * 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    if (!isRenaming) {
        // Center text if it fits
        if (textSize.x < availWidth) {
            float centerOffset = (availWidth - textSize.x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            const_cast<ModelNode*>(this)->isRenaming = true;
        }
    } else {
        // Editable name
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText(
            "##NodeName", nameBuffer, sizeof(nameBuffer),
            ImGuiInputTextFlags_AutoSelectAll
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<ModelNode*>(this)->name = nameBuffer;
            const_cast<ModelNode*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    // Draw output pins
    DrawOutputPin(
        vertexDataPin.id, vertexDataPin.label,
        static_cast<int>(vertexDataPin.type),
        nodeGraph.isPinLinked(vertexDataPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        modelMatrixPin.id, modelMatrixPin.label,
        static_cast<int>(modelMatrixPin.type),
        nodeGraph.isPinLinked(modelMatrixPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        texturePin.id, texturePin.label,
        static_cast<int>(texturePin.type), nodeGraph.isPinLinked(texturePin.id),
        nodeWidth, builder
    );

    // Only show camera pin if model has GLTF cameras
    if (cached && !cached->cameras.empty()) {
        DrawOutputPin(
            cameraPin.id, cameraPin.label,
            static_cast<int>(cameraPin.type), nodeGraph.isPinLinked(cameraPin.id),
            nodeWidth, builder
        );
    }

    // Only show light pin if model has GLTF lights
    if (cached && !cached->lights.empty()) {
        DrawOutputPin(
            lightPin.id, lightPin.label,
            static_cast<int>(lightPin.type), nodeGraph.isPinLinked(lightPin.id),
            nodeWidth, builder
        );
    }

    builder.End();
    ed::PopStyleColor();
}

void ModelNode::loadModel(const std::filesystem::path& relativePath) {
    // Release previous model reference if any
    if (modelHandle_.isValid()) {
        g_modelManager->removeReference(modelHandle_);
        modelHandle_ = {};
    }

    // Reset per-node state
    selectedCameraIndex = -1;
    selectedLightIndex = -1;
    needsCameraApply = false;

    // Load via ModelManager
    ModelHandle newHandle = g_modelManager->loadModel(relativePath);

    if (!g_modelManager->isLoaded(newHandle)) {
        Log::error(LOG_CATEGORY, "Failed to load model: {}", relativePath.string());
        return;
    }

    // Store handle and add reference
    modelHandle_ = newHandle;
    g_modelManager->addReference(modelHandle_);

    // Update settings path
    std::strncpy(settings.modelPath, relativePath.string().c_str(), sizeof(settings.modelPath) - 1);
    settings.modelPath[sizeof(settings.modelPath) - 1] = '\0';

    // Initialize per-node state from cached model
    const CachedModel* cached = g_modelManager->getModel(modelHandle_);
    if (cached) {
        if (!cached->cameras.empty()) {
            selectedCameraIndex = 0;
            needsCameraApply = true;
        }
        if (!cached->lights.empty()) {
            selectedLightIndex = 0;
        }

        Log::info(
            LOG_CATEGORY,
            "Model '{}' loaded: {} vertices, {} indices, {} geometries",
            cached->displayName,
            cached->modelData.getTotalVertexCount(),
            cached->modelData.getTotalIndexCount(),
            cached->modelData.getGeometryCount()
        );
    }
}

void ModelNode::setModel(ModelHandle handle) {
    if (!g_modelManager->isLoaded(handle)) {
        Log::warning(LOG_CATEGORY, "Cannot set model: handle not loaded");
        return;
    }

    // Release previous model reference
    if (modelHandle_.isValid()) {
        g_modelManager->removeReference(modelHandle_);
    }

    // Set new handle and add reference
    modelHandle_ = handle;
    g_modelManager->addReference(modelHandle_);

    // Reset per-node state
    selectedCameraIndex = -1;
    selectedLightIndex = -1;
    needsCameraApply = false;

    // Initialize from cached model
    const CachedModel* cached = g_modelManager->getModel(modelHandle_);
    if (cached) {
        std::strncpy(settings.modelPath, cached->path.string().c_str(), sizeof(settings.modelPath) - 1);
        settings.modelPath[sizeof(settings.modelPath) - 1] = '\0';

        if (!cached->cameras.empty()) {
            selectedCameraIndex = 0;
            needsCameraApply = true;
        }
        if (!cached->lights.empty()) {
            selectedLightIndex = 0;
        }

        Log::info(LOG_CATEGORY, "Model set to '{}'", cached->displayName);
    }
}

bool ModelNode::hasModel() const {
    return modelHandle_.isValid() && g_modelManager->isLoaded(modelHandle_);
}

const CachedModel* ModelNode::getCachedModel() const {
    if (!modelHandle_.isValid()) {
        return nullptr;
    }
    return g_modelManager->getModel(modelHandle_);
}

void ModelNode::onModelReloaded() {
    // Called when ModelManager reloads our model
    // Preserve camera/light selections if still valid
    const CachedModel* cached = getCachedModel();
    if (!cached) return;

    if (selectedCameraIndex >= static_cast<int>(cached->cameras.size())) {
        selectedCameraIndex = cached->cameras.empty() ? -1 : 0;
    }
    if (selectedLightIndex >= static_cast<int>(cached->lights.size())) {
        selectedLightIndex = cached->lights.empty() ? -1 : 0;
    }

    Log::info(LOG_CATEGORY, "Model reloaded, selections preserved");
}

void ModelNode::clearPrimitives() {
    baseTextureArray = {};
    vertexDataArray = {};
    modelMatrixArray = {};
    cameraUboArray = {};
    cameraUbo = nullptr;
    lightUboArray = {};
    lightUbo = nullptr;
    modelMatricesData.clear();
}

void ModelNode::createPrimitives(primitives::Store& store) {
    const CachedModel* cached = getCachedModel();
    if (!cached) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: no model loaded");
        return;
    }

    const auto& modelData = cached->modelData;
    const auto& images = cached->images;
    const auto& materials = cached->materials;

    primitives::StoreHandle textureNotFound{};

    // Helper to create default texture primitive
    auto createNewDefaultTexture = [&cached, &store]() -> primitives::StoreHandle {
        if (!cached->defaultTexture.pixels) {
            return {};
        }
        auto texture = store.newImage();
        auto& storeImage = store.images[texture.handle];
        storeImage.imageData = const_cast<void*>(static_cast<const void*>(cached->defaultTexture.pixels));
        storeImage.imageSize =
            cached->defaultTexture.width * cached->defaultTexture.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = cached->defaultTexture.width;
        storeImage.imageInfo.extent.height = cached->defaultTexture.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        return texture;
    };

    // Create image primitives from cached images
    // We need to store handles for each image
    std::vector<primitives::StoreHandle> imageHandles(images.size());

    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];

        if (!image.toLoad)
            continue;

        // If image failed to load, use default texture
        if (image.pixels == nullptr) {
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();
            imageHandles[i] = textureNotFound;
            continue;
        }

        imageHandles[i] = store.newImage();
        auto& storeImage = store.images[imageHandles[i].handle];
        storeImage.imageData = const_cast<void*>(static_cast<const void*>(image.pixels));
        storeImage.imageSize = image.width * image.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = image.width;
        storeImage.imageInfo.extent.height = image.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        storeImage.originalImagePath = image.path.generic_string();
    }

    // Create texture array for geometry ranges
    baseTextureArray = store.newArray();
    auto& texArray = store.arrays[baseTextureArray.handle];
    texArray.type = primitives::Type::Image;
    texArray.handles.resize(modelData.ranges.size());

    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        const auto& range = modelData.ranges[i];
        assert(range.materialIndex >= 0);
        int imgIdx = materials[range.materialIndex].baseTextureIndex;

        if (imgIdx < 0 || !imageHandles[imgIdx].isValid()) {
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();
            texArray.handles[i] = textureNotFound.handle;
        } else {
            texArray.handles[i] = imageHandles[imgIdx].handle;
        }
    }

    // Create vertex data array
    vertexDataArray = store.newArray();
    auto& vertexArray = store.arrays[vertexDataArray.handle];
    vertexArray.type = primitives::Type::VertexData;
    vertexArray.handles.resize(modelData.ranges.size());

    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        const auto& range = modelData.ranges[i];

        primitives::StoreHandle hVertexData = store.newVertexData();
        primitives::VertexData& vertexData = store.vertexDatas[hVertexData.handle];

        size_t vertexSize = range.vertexCount * sizeof(Vertex);
        size_t indexSize = range.indexCount * sizeof(uint32_t);

        // Set up vertex data span (const_cast needed for span from const data)
        auto* vertexDataPtr = reinterpret_cast<uint8_t*>(
            const_cast<Vertex*>(modelData.vertices.data() + range.firstVertex)
        );
        vertexData.vertexData = std::span<uint8_t>(vertexDataPtr, vertexSize);
        vertexData.vertexDataSize = vertexSize;
        vertexData.vertexCount = range.vertexCount;

        // Set up index data span
        auto* indexDataPtr = const_cast<uint32_t*>(modelData.indices.data() + range.firstIndex);
        vertexData.indexData = std::span<uint32_t>(indexDataPtr, range.indexCount);
        vertexData.indexDataSize = indexSize;
        vertexData.indexCount = range.indexCount;

        vertexData.bindingDescription = Vertex::getBindingDescription();
        vertexData.attributeDescriptions = Vertex::getAttributeDescriptions();
        vertexData.modelFilePath = settings.modelPath;
        vertexData.geometryIndex = static_cast<uint32_t>(i);

        vertexArray.handles[i] = hVertexData.handle;

        Log::debug(
            LOG_CATEGORY,
            "Created VertexData for range {}: {} verts, {} indices",
            i, range.vertexCount, range.indexCount
        );
    }

    modelMatrixArray = store.newArray();
    auto& uboArray = store.arrays[modelMatrixArray.handle];
    uboArray.type = primitives::Type::UniformBuffer;
    uboArray.handles.resize(modelData.ranges.size());

    glm::mat4 modelMatrix{1.0};
    settings.modelMatrix = modelMatrix;

    // TODO
    glm::mat3 normalMat3 =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    glm::mat4 normalMatrix = glm::mat4(normalMat3);

    // Store matrices permanently
    modelMatricesData.clear();
    modelMatricesData.resize(modelData.ranges.size());
    for (auto& matrices : modelMatricesData) {
        matrices.model = modelMatrix;
        matrices.normalMatrix = normalMatrix;
    }

    // Create UBO primitives pointing to persistent storage
    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        primitives::StoreHandle hUBO = store.newUniformBuffer();
        primitives::UniformBuffer& ubo =
            store.uniformBuffers[hUBO.handle];

        // Point to persistent storage - now includes both model and
        // normal matrix
        ubo.data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&modelMatricesData[i]),
            sizeof(ModelMatrices)
        );

        uboArray.handles[i] = hUBO.handle;

        Log::debug(
            "Model",
            "Created UniformBuffer primitive for range {} with model "
            "and normal matrix",
            i
        );
    }

    // Create camera UBO if model has cameras
    if (!cached->cameras.empty()) {
        primitives::StoreHandle hCameraUbo = store.newUniformBuffer();
        cameraUbo = &store.uniformBuffers[hCameraUbo.handle];

        cameraUbo->dataType = primitives::UniformDataType::Camera;
        cameraUbo->data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&cameraData),
            sizeof(ModelCameraData)
        );
        cameraUbo->extraData = &cameraType;

        cameraUboArray = store.newArray();
        auto& camArray = store.arrays[cameraUboArray.handle];
        camArray.type = primitives::Type::UniformBuffer;
        camArray.handles = {hCameraUbo.handle};

        updateCameraFromSelection();

        Log::debug(LOG_CATEGORY, "Created camera UBO primitive");
    }

    // Create light UBO if model has lights
    if (!cached->lights.empty()) {
        updateLightsFromGLTF();

        primitives::StoreHandle hLightUbo = store.newUniformBuffer();
        lightUbo = &store.uniformBuffers[hLightUbo.handle];

        lightUbo->dataType = primitives::UniformDataType::Light;
        lightUbo->data = lightsBuffer.getSpan();

        lightUboArray = store.newArray();
        auto& lgtArray = store.arrays[lightUboArray.handle];
        lgtArray.type = primitives::Type::UniformBuffer;
        lgtArray.handles = {hLightUbo.handle};

        Log::debug(
            LOG_CATEGORY,
            "Created light UBO with {} lights ({} bytes)",
            lightsBuffer.header.numLights,
            lightUbo->data.size()
        );
    }
}

void ModelNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::StoreHandle>>& outputs
) const {
    outputs.push_back({texturePin.id, baseTextureArray});
    if (vertexDataArray.isValid()) {
        outputs.push_back({vertexDataPin.id, vertexDataArray});
    }
    if (modelMatrixArray.isValid()) {
        outputs.push_back({modelMatrixPin.id, modelMatrixArray});
    }
    if (cameraUboArray.isValid()) {
        outputs.push_back({cameraPin.id, cameraUboArray});
    }
    if (lightUboArray.isValid()) {
        outputs.push_back({lightPin.id, lightUboArray});
    }
}

void ModelNode::updateCameraFromSelection() {
    const CachedModel* cached = getCachedModel();

    // Check if we have a valid camera selected
    if (!cached || selectedCameraIndex < 0 ||
        selectedCameraIndex >= static_cast<int>(cached->cameras.size())) {
        // No camera selected - use default identity matrices
        cameraData.view = glm::mat4(1.0f);
        cameraData.invView = glm::mat4(1.0f);
        cameraData.proj = glm::mat4(1.0f);
        return;
    }

    const GLTFCamera& gltfCam = cached->cameras[selectedCameraIndex];

    // Compute view matrix from GLTF camera transform
    // GLTF cameras look down -Z in their local space
    glm::vec3 position = gltfCam.position;
    glm::vec3 forward =
        -glm::normalize(glm::vec3(gltfCam.transform[2]));
    glm::vec3 up = glm::normalize(glm::vec3(gltfCam.transform[1]));
    glm::vec3 target = position + forward;

    cameraData.view = glm::lookAt(position, target, up);
    cameraData.invView = glm::inverse(cameraData.view);

    // Compute projection matrix
    if (gltfCam.isPerspective) {
        float fovRadians = glm::radians(gltfCam.fov);
        float aspect = gltfCam.aspectRatio > 0.0f ? gltfCam.aspectRatio
                                                  : aspectRatio;
        cameraData.proj = glm::perspective(
            fovRadians, aspect, gltfCam.nearPlane, gltfCam.farPlane
        );
    } else {
        // Orthographic projection
        cameraData.proj = glm::ortho(
            -gltfCam.xmag, gltfCam.xmag, -gltfCam.ymag, gltfCam.ymag,
            gltfCam.nearPlane, gltfCam.farPlane
        );
    }

    // Flip Y for Vulkan
    cameraData.proj[1][1] *= -1;

    Log::debug(
        "ModelNode",
        "Updated camera from GLTF '{}' - Pos: ({:.2f}, {:.2f}, {:.2f})",
        gltfCam.name, position.x, position.y, position.z
    );
}

void ModelNode::updateLightsFromGLTF() {
    const CachedModel* cached = getCachedModel();
    if (!cached) return;

    const auto& gltfLights = cached->lights;

    // Resize to match GLTF lights
    lightsBuffer.lights.resize(gltfLights.size());

    for (size_t i = 0; i < gltfLights.size(); ++i) {
        const auto& gltfLight = gltfLights[i];
        auto& light = lightsBuffer.lights[i];

        light.position = gltfLight.position;
        light.radius = gltfLight.range > 0.0f ? gltfLight.range : 10.0f;
        light.color = gltfLight.color;
        light.intensity = gltfLight.intensity;

        Log::debug(
            LOG_CATEGORY,
            "Light '{}' - Pos: ({:.2f}, {:.2f}, {:.2f}), Intensity: {:.2f}",
            gltfLight.name,
            light.position.x, light.position.y, light.position.z,
            light.intensity
        );
    }

    lightsBuffer.updateGpuBuffer();
}

// File watching is now handled by ModelManager