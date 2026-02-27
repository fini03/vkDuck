#include "model_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <cmath>
#include <cstring>
#include <future>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <set>
#include <thread>
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
#include <imgui.h>

namespace {
constexpr float PADDING_X = 10.0f;
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

// Parallel image loading result (uses vkDuck's imageLoad)
struct DecodedImageResult {
    void* pixels{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    size_t index{0};
    bool success{false};
};

// Load a single image (for use in parallel)
static DecodedImageResult loadSingleImage(const fs::path& path, size_t index) {
    DecodedImageResult result;
    result.index = index;
    result.pixels = imageLoad(path, result.width, result.height);
    result.success = (result.pixels != nullptr);
    return result;
}

// Load multiple images in parallel using std::async
static std::vector<DecodedImageResult> loadImagesParallel(
    const std::vector<std::pair<size_t, fs::path>>& imagesToLoad
) {
    std::vector<std::future<DecodedImageResult>> futures;
    futures.reserve(imagesToLoad.size());

    for (const auto& [index, path] : imagesToLoad) {
        futures.push_back(std::async(std::launch::async, loadSingleImage, path, index));
    }

    std::vector<DecodedImageResult> results;
    results.reserve(futures.size());
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    return results;
}

EditorImage::~EditorImage() {
    if (pixels)
        imageFree(pixels);
}

ModelNode::ModelNode()
    : Node() {
    name = "Model";
    createDefaultPins();
    fileWatcher = std::make_unique<ModelFileWatcher>();
}

ModelNode::ModelNode(int id)
    : Node(id) {
    name = "Model";
    createDefaultPins();
    fileWatcher = std::make_unique<ModelFileWatcher>();
}

ModelNode::~ModelNode() {}

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

    // Store camera selection to restore after model is loaded
    selectedCameraIndex = j.value("selectedCameraIndex", -1);

    // Restore output pins by index (order: modelMatrix, texture,
    // vertexData, cameras)
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

    usesRegistry = true;
}

void ModelNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Calculate node width - only include camera pin if model has
    // cameras
    std::vector<std::string> pinLabels = {
        vertexDataPin.label, modelMatrixPin.label, texturePin.label
    };
    if (!gltfCameras.empty()) {
        pinLabels.push_back(cameraPin.label);
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
        graph.isPinLinked(vertexDataPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        modelMatrixPin.id, modelMatrixPin.label,
        static_cast<int>(modelMatrixPin.type),
        graph.isPinLinked(modelMatrixPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        texturePin.id, texturePin.label,
        static_cast<int>(texturePin.type), graph.isPinLinked(texturePin.id),
        nodeWidth, builder
    );

    // Only show camera pin if model has GLTF cameras
    if (!gltfCameras.empty()) {
        DrawOutputPin(
            cameraPin.id, cameraPin.label,
            static_cast<int>(cameraPin.type), graph.isPinLinked(cameraPin.id),
            nodeWidth, builder
        );
    }

    builder.End();
    ed::PopStyleColor();
}

void ModelNode::loadModel(
    const std::filesystem::path& path,
    const std::filesystem::path& projRoot
) {
    auto totalStart = std::chrono::high_resolution_clock::now();
    Log::info("Model", "Loading model from: {}", path.string());

    // Clear existing data
    materials.clear();
    images.clear();
    modelData.clear();
    gltfCameras.clear();
    selectedCameraIndex = -1;
    defaultTexture = {.path = projRoot / "data" / "images" / "default.png"};

    // Use vkDuck library's loadModel for all the heavy lifting
    // (GLTF parsing, SIMD index conversion, camera extraction, texture path resolution)
    ModelData libModelData = ::loadModel(path.string(), projRoot.string());

    if (libModelData.vertices.empty()) {
        Log::error("Model", "Failed to load model or model is empty");
        return;
    }

    // Copy consolidated geometry data directly to modelData
    modelData.vertices = std::move(libModelData.vertices);
    modelData.indices = std::move(libModelData.indices);

    // Convert GeometryRange to EditorGeometryRange (add topology field)
    VkPrimitiveTopology defaultTopology = topologyOptionsEnum[settings.topology];
    modelData.ranges.reserve(libModelData.ranges.size());
    for (const auto& range : libModelData.ranges) {
        EditorGeometryRange editorRange;
        editorRange.firstVertex = range.firstVertex;
        editorRange.vertexCount = range.vertexCount;
        editorRange.firstIndex = range.firstIndex;
        editorRange.indexCount = range.indexCount;
        editorRange.materialIndex = range.materialIndex;
        editorRange.topology = defaultTopology;
        modelData.ranges.push_back(editorRange);
    }

    // Copy cameras from library
    gltfCameras = std::move(libModelData.cameras);

    if (!gltfCameras.empty()) {
        Log::info(
            "Model", "Found {} camera(s) in GLTF file",
            gltfCameras.size()
        );
        selectedCameraIndex = 0; // Select first camera by default
        needsCameraApply = true; // Trigger auto-apply when camera UI is shown
    }

    // Set up images and materials based on texture paths from library
    // The library provides one texture path per material
    materials.resize(libModelData.texturePaths.size());
    images.resize(libModelData.texturePaths.size());

    for (size_t i = 0; i < libModelData.texturePaths.size(); ++i) {
        const auto& texPath = libModelData.texturePaths[i];
        if (!texPath.empty()) {
            images[i].path = texPath;
            images[i].toLoad = true;
            materials[i].baseTextureIndex = static_cast<int>(i);
        } else {
            materials[i].baseTextureIndex = -1;
        }
    }

    // Load default texture first so we can use it as a fallback
    Log::debug(
        "Model", "Loading default texture {}",
        defaultTexture.path.string()
    );
    defaultTexture.pixels = imageLoad(
        defaultTexture.path, defaultTexture.width, defaultTexture.height
    );
    if (defaultTexture.pixels == nullptr) {
        Log::error(
            "Model", "Failed to load default texture: {}",
            defaultTexture.path.string()
        );
    }

    // Load textures in parallel (async)
    {
        auto t1 = std::chrono::high_resolution_clock::now();

        // Collect images that need to be loaded
        std::vector<std::pair<size_t, fs::path>> imagesToLoad;
        for (size_t i = 0; i < images.size(); ++i) {
            if (images[i].toLoad) {
                imagesToLoad.push_back({i, images[i].path});
            }
        }

        // Load images in parallel
        if (!imagesToLoad.empty()) {
            Log::debug("Model", "Loading {} images in parallel...", imagesToLoad.size());
            auto results = loadImagesParallel(imagesToLoad);

            for (const auto& result : results) {
                images[result.index].pixels = result.pixels;
                images[result.index].width = result.width;
                images[result.index].height = result.height;

                if (!result.success) {
                    Log::warning(
                        "Model",
                        "Failed to load texture: {}, using default texture",
                        images[result.index].path.string()
                    );
                }
            }
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms_double = t2 - t1;
        Log::debug(
            "Model", "Image loading took {}ms (parallel)", ms_double.count()
        );
    }

    Log::info(
        "Model",
        "Loaded model: {} total vertices, {} total indices, {} geometry ranges",
        modelData.getTotalVertexCount(), modelData.getTotalIndexCount(),
        modelData.getGeometryCount()
    );

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    Log::info("Model", "Total loading time: {:.1f}ms", totalMs.count());

    // Store the model path and project root for potential reload
    currentModelPath = path.string();
    this->projectRoot = projRoot;

    // Set up file watcher for auto-reload
    if (fileWatcher && fileWatchingEnabled) {
        fileWatcher->setReloadCallback([this](
                                           const std::string& filepath
                                       ) {
            Log::info(
                "Model", "Detected change in model file: {}", filepath
            );
            pendingReload = true;
        });
        fileWatcher->watchFile(path.string());
        fileWatcher->setLoadingState(
            ModelFileWatcher::LoadingState::Loaded
        );
    }
}

void ModelNode::clearPrimitives() {
    for (auto& image : images)
        image.image = {};
    baseTextureArray = {};
    vertexDataArray = {};
    modelMatrixArray = {};
    cameraUboArray = {};
    cameraUbo = nullptr;
}

void ModelNode::createPrimitives(primitives::Store& store) {
    primitives::StoreHandle textureNotFound{};
    auto createNewDefaultTexture = [this, &store]() -> auto {
        auto texture = store.newImage();
        auto& storeImage = store.images[texture.handle];
        storeImage.imageData = defaultTexture.pixels;
        storeImage.imageSize =
            defaultTexture.width * defaultTexture.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = defaultTexture.width;
        storeImage.imageInfo.extent.height = defaultTexture.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        return texture;
    };

    for (auto& image : images) {
        // We don't want to create primitives for images we didn't
        // even load in the first place...
        if (!image.toLoad)
            continue;

        // If we didn't successfully load the image, just pass the
        // handle to the default texture instead
        if (image.pixels == nullptr) {
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();
            image.image = textureNotFound;
        }

        image.image = store.newImage();
        auto& storeImage = store.images[image.image.handle];
        storeImage.imageData = image.pixels;
        storeImage.imageSize = image.width * image.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = image.width;
        storeImage.imageInfo.extent.height = image.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        // Store original image path for code generation (wuffs loading)
        storeImage.originalImagePath = image.path.generic_string();
    }

    baseTextureArray = store.newArray();
    auto& array = store.arrays[baseTextureArray.handle];
    array.type = primitives::Type::Image;

    array.handles.resize(modelData.ranges.size());
    auto handles = std::views::zip(array.handles, modelData.ranges);
    for (auto&& [handle, range] : handles) {
        assert(range.materialIndex >= 0);
        int img = materials[range.materialIndex].baseTextureIndex;
        if (img < 0) {
            // Only create a default texture if we actually need one
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();

            handle = textureNotFound.handle;
        } else {
            assert(images[img].image.isValid());
            handle = images[img].image.handle;
        }
    }

    // TODO
    vertexDataArray = store.newArray();
    auto& vertexArray = store.arrays[vertexDataArray.handle];
    vertexArray.type = primitives::Type::VertexData;

    vertexArray.handles.resize(modelData.ranges.size());

    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        const auto& range = modelData.ranges[i];

        // Create a VertexData primitive for this geometry
        primitives::StoreHandle hVertexData = store.newVertexData();
        primitives::VertexData& vertexData =
            store.vertexDatas[hVertexData.handle];

        // Calculate spans for this specific geometry range
        size_t vertexOffset = range.firstVertex * sizeof(Vertex);
        size_t vertexSize = range.vertexCount * sizeof(Vertex);
        size_t indexOffset = range.firstIndex * sizeof(uint32_t);
        size_t indexSize = range.indexCount * sizeof(uint32_t);

        // Set up vertex data span
        uint8_t* vertexDataPtr = reinterpret_cast<uint8_t*>(
            modelData.vertices.data() + range.firstVertex
        );
        vertexData.vertexData =
            std::span<uint8_t>(vertexDataPtr, vertexSize);
        vertexData.vertexDataSize = vertexSize;
        vertexData.vertexCount = range.vertexCount;

        // Set up index data span
        uint32_t* indexDataPtr =
            modelData.indices.data() + range.firstIndex;
        vertexData.indexData =
            std::span<uint32_t>(indexDataPtr, range.indexCount);
        vertexData.indexDataSize = indexSize;
        vertexData.indexCount = range.indexCount;

        // Set up vertex input descriptions
        vertexData.bindingDescription = Vertex::getBindingDescription();
        vertexData.attributeDescriptions =
            Vertex::getAttributeDescriptions();

        // Set up model file path and geometry index for code generation
        vertexData.modelFilePath = settings.modelPath;
        vertexData.geometryIndex = static_cast<uint32_t>(i);

        // Store handle in array
        vertexArray.handles[i] = hVertexData.handle;

        Log::debug(
            "Model",
            "Created VertexData primitive for range {}: {} vertices, "
            "{} indices",
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

    // Create camera UBO if we have cameras
    if (!gltfCameras.empty()) {
        // Create UniformBuffer primitive for camera
        primitives::StoreHandle hCameraUbo = store.newUniformBuffer();
        cameraUbo = &store.uniformBuffers[hCameraUbo.handle];

        // Point to our persistent camera data
        // TODO: Use the same struct as for other fixed camcam
        cameraUbo->dataType = primitives::UniformDataType::Camera;
        cameraUbo->data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&cameraData),
            sizeof(ModelCameraData)
        );
        cameraUbo->extraData = &cameraType;

        // Create array with single Camera
        cameraUboArray = store.newArray();
        auto& camArray = store.arrays[cameraUboArray.handle];
        camArray.type = primitives::Type::UniformBuffer;
        camArray.handles = {hCameraUbo.handle};

        // Update camera matrices now that UBO is set up
        updateCameraFromSelection();

        Log::debug(
            "ModelNode",
            "Created camera UBO primitive for selected GLTF camera"
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
}

void ModelNode::updateCameraFromSelection() {
    // Check if we have a valid camera selected
    if (selectedCameraIndex < 0 ||
        selectedCameraIndex >= static_cast<int>(gltfCameras.size())) {
        // No camera selected - use default identity matrices
        cameraData.view = glm::mat4(1.0f);
        cameraData.invView = glm::mat4(1.0f);
        cameraData.proj = glm::mat4(1.0f);
        // Note: UBO update is handled by Camera::recordCommands() for
        // non-fixed cameras
        return;
    }

    const GLTFCamera& gltfCam = gltfCameras[selectedCameraIndex];

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

// ============================================================================
// File Watcher Integration
// ============================================================================

void ModelNode::setFileWatchingEnabled(bool enabled) {
    fileWatchingEnabled = enabled;

    if (fileWatcher) {
        if (enabled && !currentModelPath.empty()) {
            // Set up callback and start watching
            fileWatcher->setReloadCallback(
                [this](const std::string& filepath) {
                    Log::info(
                        "Model", "Detected change in model file: {}",
                        filepath
                    );
                    pendingReload = true;
                }
            );
            fileWatcher->watchFile(currentModelPath);
            Log::info(
                "Model", "File watching enabled for: {}",
                currentModelPath
            );
        } else if (!enabled) {
            fileWatcher->stopWatching();
            Log::info("Model", "File watching disabled");
        }
    }
}

bool ModelNode::isFileWatchingEnabled() const {
    return fileWatchingEnabled && fileWatcher &&
           fileWatcher->isWatching();
}

ModelFileWatcher::LoadingState ModelNode::getLoadingState() const {
    if (fileWatcher) {
        return fileWatcher->getLoadingState();
    }
    return ModelFileWatcher::LoadingState::Idle;
}

const std::string& ModelNode::getLastError() const {
    static std::string emptyError;
    if (fileWatcher) {
        return fileWatcher->getLastError();
    }
    return emptyError;
}

void ModelNode::reloadModel() {
    if (currentModelPath.empty()) {
        Log::warning("Model", "Cannot reload: no model path set");
        return;
    }

    Log::info("Model", "Reloading model from: {}", currentModelPath);

    if (fileWatcher) {
        fileWatcher->setLoadingState(
            ModelFileWatcher::LoadingState::Loading
        );
    }

    try {
        // Store current state that should persist across reload
        int savedCameraIndex = selectedCameraIndex;
        bool savedNeedsCameraApply = needsCameraApply;

        // Reload the model
        loadModel(std::filesystem::path(currentModelPath), projectRoot);

        // Restore camera selection if it's still valid
        if (savedCameraIndex >= 0 &&
            savedCameraIndex < static_cast<int>(gltfCameras.size())) {
            selectedCameraIndex = savedCameraIndex;
            needsCameraApply = savedNeedsCameraApply;
        }

        if (fileWatcher) {
            fileWatcher->setLoadingState(
                ModelFileWatcher::LoadingState::Loaded
            );
        }

        Log::info("Model", "Model reloaded successfully");
    } catch (const std::exception& e) {
        Log::error("Model", "Failed to reload model: {}", e.what());
        if (fileWatcher) {
            fileWatcher->setLoadingState(
                ModelFileWatcher::LoadingState::Error
            );
            fileWatcher->setLastError(e.what());
        }
    }

    pendingReload = false;
}