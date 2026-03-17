#include "material_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <unordered_set>
#include <vkDuck/model_loader.h>
#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr const char* LOG_CATEGORY = "MaterialNode";
}

namespace ed = ax::NodeEditor;

// ============================================================================
// Construction
// ============================================================================

MaterialNode::MaterialNode() : ModelNodeBase() {
    name = "Material";
    createDefaultPins();
}

MaterialNode::MaterialNode(int id) : ModelNodeBase(id) {
    name = "Material";
    createDefaultPins();
}

MaterialNode::~MaterialNode() = default;

void MaterialNode::createDefaultPins() {
    baseColorPin.id = ed::PinId(GetNextGlobalId());
    baseColorPin.type = PinType::Image;
    baseColorPin.label = "Base Color";

    metallicRoughnessPin.id = ed::PinId(GetNextGlobalId());
    metallicRoughnessPin.type = PinType::Image;
    metallicRoughnessPin.label = "MetallicRoughness";

    normalPin.id = ed::PinId(GetNextGlobalId());
    normalPin.type = PinType::Image;
    normalPin.label = "Normal";

    emissivePin.id = ed::PinId(GetNextGlobalId());
    emissivePin.type = PinType::Image;
    emissivePin.label = "Emissive";

    materialParamsPin.id = ed::PinId(GetNextGlobalId());
    materialParamsPin.type = PinType::UniformBuffer;
    materialParamsPin.label = "MaterialParams";
}

// ============================================================================
// Pin Registry
// ============================================================================

void MaterialNode::registerPins(PinRegistry& registry) {
    baseColorPinHandle = registry.registerPinWithId(
        id, baseColorPin.id, baseColorPin.type, PinKind::Output, baseColorPin.label);

    metallicRoughnessPinHandle = registry.registerPinWithId(
        id, metallicRoughnessPin.id, metallicRoughnessPin.type,
        PinKind::Output, metallicRoughnessPin.label);

    normalPinHandle = registry.registerPinWithId(
        id, normalPin.id, normalPin.type, PinKind::Output, normalPin.label);

    emissivePinHandle = registry.registerPinWithId(
        id, emissivePin.id, emissivePin.type, PinKind::Output, emissivePin.label);

    materialParamsPinHandle = registry.registerPinWithId(
        id, materialParamsPin.id, materialParamsPin.type,
        PinKind::Output, materialParamsPin.label);

    usesRegistry_ = true;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json MaterialNode::toJson() const {
    nlohmann::json j = ModelNodeBase::toJson();
    j["type"] = "material";

    j["outputPins"] = nlohmann::json::array({
        {{"id", baseColorPin.id.Get()},
         {"type", static_cast<int>(baseColorPin.type)},
         {"label", baseColorPin.label}},
        {{"id", metallicRoughnessPin.id.Get()},
         {"type", static_cast<int>(metallicRoughnessPin.type)},
         {"label", metallicRoughnessPin.label}},
        {{"id", normalPin.id.Get()},
         {"type", static_cast<int>(normalPin.type)},
         {"label", normalPin.label}},
        {{"id", emissivePin.id.Get()},
         {"type", static_cast<int>(emissivePin.type)},
         {"label", emissivePin.label}},
        {{"id", materialParamsPin.id.Get()},
         {"type", static_cast<int>(materialParamsPin.type)},
         {"label", materialParamsPin.label}}
    });

    return j;
}

void MaterialNode::fromJson(const nlohmann::json& j) {
    ModelNodeBase::fromJson(j);

    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        const auto& pins = j["outputPins"];
        if (pins.size() > 0) baseColorPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1) metallicRoughnessPin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2) normalPin.id = ed::PinId(pins[2]["id"].get<int>());
        if (pins.size() > 3) emissivePin.id = ed::PinId(pins[3]["id"].get<int>());
        if (pins.size() > 4) materialParamsPin.id = ed::PinId(pins[4]["id"].get<int>());
    }
}

// ============================================================================
// Rendering
// ============================================================================

void MaterialNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    std::vector<std::string> pinLabels = {
        baseColorPin.label,
        metallicRoughnessPin.label,
        normalPin.label,
        emissivePin.label,
        materialParamsPin.label
    };
    float nodeWidth = calculateModelNodeWidth(name, pinLabels);
    renderModelNodeHeader(builder, nodeWidth);

    auto drawPin = [&](const Pin& pin) {
        DrawOutputPin(
            pin.id, pin.label, static_cast<int>(pin.type),
            nodeGraph.isPinLinked(pin.id), nodeWidth, builder);
    };

    drawPin(baseColorPin);
    drawPin(metallicRoughnessPin);
    drawPin(normalPin);
    drawPin(emissivePin);
    drawPin(materialParamsPin);

    builder.End();
    ed::PopStyleColor();
}

// ============================================================================
// Primitives
// ============================================================================

void MaterialNode::clearPrimitives() {
    baseColorArray_ = {};
    metallicRoughnessArray_ = {};
    normalArray_ = {};
    emissiveArray_ = {};
    materialParamsArray_ = {};
    defaultWhite_ = {};
    defaultMetallicRough_ = {};
    defaultNormal_ = {};
    defaultBlack_ = {};
    defaultWhitePixels_.clear();
    defaultMetallicRoughPixels_.clear();
    defaultNormalPixels_.clear();
    defaultBlackPixels_.clear();
    materialParamsData_.clear();
}

primitives::StoreHandle MaterialNode::createDefaultTexture(
    primitives::Store& store,
    std::vector<uint8_t>& pixelStorage,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    bool linear
) {
    // Create 1x1 BGRA texture (Vulkan B8G8R8A8 format)
    pixelStorage = {b, g, r, a};

    auto handle = store.newImage();
    auto& img = store.images[handle.handle];
    img.imageData = pixelStorage.data();
    img.imageSize = 4;
    img.extentType = ExtentType::Custom;
    // Use UNORM for linear data (normals, metallic-roughness), SRGB for color data
    img.imageInfo.format = linear ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB;
    img.imageInfo.extent = {1, 1, 1};
    img.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    return handle;
}

primitives::StoreHandle MaterialNode::createImagePrimitive(
    primitives::Store& store,
    const EditorImage& image,
    bool linear
) {
    auto handle = store.newImage();
    auto& img = store.images[handle.handle];
    img.imageData = const_cast<void*>(static_cast<const void*>(image.pixels));
    img.imageSize = image.width * image.height * 4;
    img.extentType = ExtentType::Custom;
    // Use UNORM for linear data (normals, metallic-roughness), SRGB for color data
    img.imageInfo.format = linear ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB;
    img.imageInfo.extent = {image.width, image.height, 1};
    img.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img.originalImagePath = image.path.generic_string();
    return handle;
}

void MaterialNode::createPrimitives(primitives::Store& store) {
    const CachedModel* cached = getCachedModel();
    if (!cached) {
        Log::warning(LOG_CATEGORY, "No model loaded");
        return;
    }

    const auto& modelData = cached->modelData;
    const auto& images = cached->images;
    const auto& materials = cached->materials;
    const size_t numRanges = modelData.ranges.size();

    // Create PBR-compliant default textures
    // White (1,1,1,1) - baseColor fallback (sRGB color data)
    defaultWhite_ = createDefaultTexture(store, defaultWhitePixels_, 255, 255, 255, 255, false);

    // Metallic-roughness: R=0 (unused), G=255 (rough=1), B=0 (metal=0), A=255
    // In shader: roughness = sample.g, metallic = sample.b (linear data)
    defaultMetallicRough_ = createDefaultTexture(store, defaultMetallicRoughPixels_, 0, 255, 0, 255, true);

    // Flat normal (128,128,255) -> (0.5, 0.5, 1) in [0,1] -> (0, 0, 1) in [-1,1] (linear data)
    defaultNormal_ = createDefaultTexture(store, defaultNormalPixels_, 128, 128, 255, 255, true);

    // Black (0,0,0) - emissive fallback (no emission) (sRGB color data)
    defaultBlack_ = createDefaultTexture(store, defaultBlackPixels_, 0, 0, 0, 255, false);

    // Identify which images are used for linear data (normals, metallic-roughness)
    std::unordered_set<int> linearTextureIndices;
    for (const auto& mat : materials) {
        if (mat.normalTextureIndex >= 0)
            linearTextureIndices.insert(mat.normalTextureIndex);
        if (mat.metallicRoughnessTextureIndex >= 0)
            linearTextureIndices.insert(mat.metallicRoughnessTextureIndex);
    }

    // Build image primitives for all loaded textures with correct format
    std::vector<primitives::StoreHandle> imageHandles(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].pixels && images[i].toLoad) {
            bool isLinear = linearTextureIndices.count(static_cast<int>(i)) > 0;
            imageHandles[i] = createImagePrimitive(store, images[i], isLinear);
        }
    }

    // Helper: resolve texture index to handle with fallback
    auto resolveTexture = [&](int texIdx, primitives::StoreHandle fallback)
        -> primitives::StoreHandle {
        if (texIdx >= 0 && static_cast<size_t>(texIdx) < imageHandles.size() &&
            imageHandles[texIdx].isValid()) {
            return imageHandles[texIdx];
        }
        return fallback;
    };

    // Helper: create texture array for all geometry ranges
    auto createTextureArray = [&](
        primitives::StoreHandle& arrayHandle,
        auto getTexIndex,
        primitives::StoreHandle fallback
    ) {
        arrayHandle = store.newArray();
        auto& arr = store.arrays[arrayHandle.handle];
        arr.type = primitives::Type::Image;
        arr.handles.resize(numRanges);

        for (size_t i = 0; i < numRanges; ++i) {
            int matIdx = modelData.ranges[i].materialIndex;
            int texIdx = (matIdx >= 0 && static_cast<size_t>(matIdx) < materials.size())
                ? getTexIndex(materials[matIdx]) : -1;
            arr.handles[i] = resolveTexture(texIdx, fallback).handle;
        }
    };

    // Create texture arrays for each PBR channel
    createTextureArray(baseColorArray_,
        [](const EditorMaterial& m) { return m.baseColorTextureIndex; },
        defaultWhite_);

    createTextureArray(metallicRoughnessArray_,
        [](const EditorMaterial& m) { return m.metallicRoughnessTextureIndex; },
        defaultMetallicRough_);

    createTextureArray(normalArray_,
        [](const EditorMaterial& m) { return m.normalTextureIndex; },
        defaultNormal_);

    createTextureArray(emissiveArray_,
        [](const EditorMaterial& m) { return m.emissiveTextureIndex; },
        defaultBlack_);

    // Create MaterialParams UBO array (one per geometry range)
    materialParamsData_.resize(numRanges);
    materialParamsArray_ = store.newArray();
    auto& paramsArr = store.arrays[materialParamsArray_.handle];
    paramsArr.type = primitives::Type::UniformBuffer;
    paramsArr.handles.resize(numRanges);

    for (size_t i = 0; i < numRanges; ++i) {
        int matIdx = modelData.ranges[i].materialIndex;

        // Default PBR values
        MaterialParams& params = materialParamsData_[i];
        params.baseColorFactor = glm::vec4(1.0f);
        params.emissiveFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        params.metallicFactor = 1.0f;
        params.roughnessFactor = 1.0f;

        // Override with material values if present
        if (matIdx >= 0 && static_cast<size_t>(matIdx) < materials.size()) {
            const auto& mat = materials[matIdx];
            params.baseColorFactor = mat.baseColorFactor;
            params.emissiveFactor = glm::vec4(mat.emissiveFactor, 1.0f);
            params.metallicFactor = mat.metallicFactor;
            params.roughnessFactor = mat.roughnessFactor;
        }

        auto uboHandle = store.newUniformBuffer();
        auto& ubo = store.uniformBuffers[uboHandle.handle];
        ubo.data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&materialParamsData_[i]),
            sizeof(MaterialParams));
        paramsArr.handles[i] = uboHandle.handle;
    }

    Log::info(LOG_CATEGORY, "Created material arrays for {} geometry ranges", numRanges);
}

void MaterialNode::getOutputPrimitives(
    const primitives::Store& /*store*/,
    std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>& outputs
) const {
    if (baseColorArray_.isValid())
        outputs.emplace_back(baseColorPin.id, baseColorArray_);
    if (metallicRoughnessArray_.isValid())
        outputs.emplace_back(metallicRoughnessPin.id, metallicRoughnessArray_);
    if (normalArray_.isValid())
        outputs.emplace_back(normalPin.id, normalArray_);
    if (emissiveArray_.isValid())
        outputs.emplace_back(emissivePin.id, emissiveArray_);
    if (materialParamsArray_.isValid())
        outputs.emplace_back(materialParamsPin.id, materialParamsArray_);
}
