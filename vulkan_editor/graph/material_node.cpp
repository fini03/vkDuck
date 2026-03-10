#include "material_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <imgui.h>
#include <imgui_node_editor.h>

#include <vkDuck/model_loader.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr const char* LOG_CATEGORY = "MaterialNode";
}

namespace ed = ax::NodeEditor;

MaterialNode::MaterialNode()
    : ModelNodeBase() {
    name = "Material";
    createDefaultPins();
}

MaterialNode::MaterialNode(int id)
    : ModelNodeBase(id) {
    name = "Material";
    createDefaultPins();
}

MaterialNode::~MaterialNode() = default;

void MaterialNode::createDefaultPins() {
    // Base color texture output
    baseColorPin.id = ed::PinId(GetNextGlobalId());
    baseColorPin.type = PinType::Image;
    baseColorPin.label = "Base Color";

    // Metallic-roughness texture output
    metallicRoughnessPin.id = ed::PinId(GetNextGlobalId());
    metallicRoughnessPin.type = PinType::Image;
    metallicRoughnessPin.label = "MetallicRoughness";

    // Normal map texture output
    normalPin.id = ed::PinId(GetNextGlobalId());
    normalPin.type = PinType::Image;
    normalPin.label = "Normal";

    // Emissive texture output
    emissivePin.id = ed::PinId(GetNextGlobalId());
    emissivePin.type = PinType::Image;
    emissivePin.label = "Emissive";
}

void MaterialNode::registerPins(PinRegistry& registry) {
    baseColorPinHandle = registry.registerPinWithId(
        id,
        baseColorPin.id,
        baseColorPin.type,
        PinKind::Output,
        baseColorPin.label
    );

    metallicRoughnessPinHandle = registry.registerPinWithId(
        id,
        metallicRoughnessPin.id,
        metallicRoughnessPin.type,
        PinKind::Output,
        metallicRoughnessPin.label
    );

    normalPinHandle = registry.registerPinWithId(
        id,
        normalPin.id,
        normalPin.type,
        PinKind::Output,
        normalPin.label
    );

    emissivePinHandle = registry.registerPinWithId(
        id,
        emissivePin.id,
        emissivePin.type,
        PinKind::Output,
        emissivePin.label
    );

    usesRegistry_ = true;
}

nlohmann::json MaterialNode::toJson() const {
    nlohmann::json j = ModelNodeBase::toJson();
    j["type"] = "material";

    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back({
        {"id", baseColorPin.id.Get()},
        {"type", static_cast<int>(baseColorPin.type)},
        {"label", baseColorPin.label}
    });
    j["outputPins"].push_back({
        {"id", metallicRoughnessPin.id.Get()},
        {"type", static_cast<int>(metallicRoughnessPin.type)},
        {"label", metallicRoughnessPin.label}
    });
    j["outputPins"].push_back({
        {"id", normalPin.id.Get()},
        {"type", static_cast<int>(normalPin.type)},
        {"label", normalPin.label}
    });
    j["outputPins"].push_back({
        {"id", emissivePin.id.Get()},
        {"type", static_cast<int>(emissivePin.type)},
        {"label", emissivePin.label}
    });

    return j;
}

void MaterialNode::fromJson(const nlohmann::json& j) {
    ModelNodeBase::fromJson(j);

    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0)
            baseColorPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1)
            metallicRoughnessPin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2)
            normalPin.id = ed::PinId(pins[2]["id"].get<int>());
        if (pins.size() > 3)
            emissivePin.id = ed::PinId(pins[3]["id"].get<int>());
    }
}

void MaterialNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    std::vector<std::string> pinLabels = {
        baseColorPin.label,
        metallicRoughnessPin.label,
        normalPin.label,
        emissivePin.label
    };
    float nodeWidth = calculateModelNodeWidth(name, pinLabels);

    renderModelNodeHeader(builder, nodeWidth);

    // Draw all output pins
    DrawOutputPin(
        baseColorPin.id,
        baseColorPin.label,
        static_cast<int>(baseColorPin.type),
        nodeGraph.isPinLinked(baseColorPin.id),
        nodeWidth,
        builder
    );

    DrawOutputPin(
        metallicRoughnessPin.id,
        metallicRoughnessPin.label,
        static_cast<int>(metallicRoughnessPin.type),
        nodeGraph.isPinLinked(metallicRoughnessPin.id),
        nodeWidth,
        builder
    );

    DrawOutputPin(
        normalPin.id,
        normalPin.label,
        static_cast<int>(normalPin.type),
        nodeGraph.isPinLinked(normalPin.id),
        nodeWidth,
        builder
    );

    DrawOutputPin(
        emissivePin.id,
        emissivePin.label,
        static_cast<int>(emissivePin.type),
        nodeGraph.isPinLinked(emissivePin.id),
        nodeWidth,
        builder
    );

    builder.End();
    ed::PopStyleColor();
}

void MaterialNode::clearPrimitives() {
    baseColorArray_ = {};
    metallicRoughnessArray_ = {};
    normalArray_ = {};
    emissiveArray_ = {};
    defaultWhiteTexture_ = {};
    defaultNormalTexture_ = {};
    defaultProjectTexture_ = {};
    defaultBlackTexture_ = {};
    defaultWhitePixels_.clear();
    defaultNormalPixels_.clear();
    defaultBlackPixels_.clear();
}

primitives::StoreHandle MaterialNode::createDefaultTexture(
    primitives::Store& store,
    std::vector<uint8_t>& pixelStorage,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a
) {
    // Create 1x1 BGRA texture (Vulkan format is B8G8R8A8)
    pixelStorage = {b, g, r, a};

    auto handle = store.newImage();
    auto& storeImage = store.images[handle.handle];

    storeImage.imageData = pixelStorage.data();
    storeImage.imageSize = 4;
    storeImage.extentType = ExtentType::Custom;
    storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    storeImage.imageInfo.extent.width = 1;
    storeImage.imageInfo.extent.height = 1;
    storeImage.imageInfo.extent.depth = 1;
    storeImage.imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    storeImage.viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    return handle;
}

primitives::StoreHandle MaterialNode::createImagePrimitive(
    primitives::Store& store,
    const EditorImage& image
) {
    auto handle = store.newImage();
    auto& storeImage = store.images[handle.handle];

    storeImage.imageData =
        const_cast<void*>(static_cast<const void*>(image.pixels));
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

    return handle;
}

void MaterialNode::createPrimitives(primitives::Store& store) {
    const CachedModel* cached = getCachedModel();
    if (!cached) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: no model loaded");
        return;
    }

    const auto& modelData = cached->modelData;
    const auto& images = cached->images;
    const auto& materials = cached->materials;

    // Create per-channel default textures
    // White (255, 255, 255) for base color fallback
    defaultWhiteTexture_ = createDefaultTexture(
        store, defaultWhitePixels_, 255, 255, 255, 255
    );

    // Flat normal (128, 128, 255) - points straight up in tangent space
    defaultNormalTexture_ = createDefaultTexture(
        store, defaultNormalPixels_, 128, 128, 255, 255
    );

    // Black (0, 0, 0) for emissive fallback (no emission)
    defaultBlackTexture_ = createDefaultTexture(
        store, defaultBlackPixels_, 0, 0, 0, 255
    );

    // Project's default.png for metallic-roughness fallback
    if (cached->defaultTexture.pixels) {
        defaultProjectTexture_ = createImagePrimitive(store, cached->defaultTexture);
    } else {
        // Fallback to white if default.png not loaded
        Log::warning(LOG_CATEGORY, "No default texture loaded, using white for metRough fallback");
        defaultProjectTexture_ = defaultWhiteTexture_;
    }

    // Build image handle lookup (create primitives for all loaded images)
    std::vector<primitives::StoreHandle> imageHandles(images.size());
    int loadedCount = 0;
    int failedCount = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];
        if (image.pixels && image.toLoad) {
            imageHandles[i] = createImagePrimitive(store, image);
            loadedCount++;
        } else if (image.toLoad) {
            Log::warning(LOG_CATEGORY, "Image {} has no pixels: {}", i, image.path.string());
            failedCount++;
        }
    }
    Log::debug(LOG_CATEGORY, "Images: {} total, {} loaded, {} failed",
        images.size(), loadedCount, failedCount);

    // Helper to get texture handle or default (with logging)
    int defaultFallbackCount = 0;
    int fallbackNoTexture = 0;      // texIdx < 0
    int fallbackOutOfBounds = 0;    // texIdx >= imageHandles.size()
    int fallbackInvalidHandle = 0;  // handle not valid
    auto getTextureHandle = [&](int textureIndex,
                                 primitives::StoreHandle defaultTex,
                                 const char* channelName,
                                 size_t rangeIdx,
                                 int matIdx)
        -> primitives::StoreHandle {
        if (textureIndex < 0) {
            fallbackNoTexture++;
            defaultFallbackCount++;
            Log::debug(LOG_CATEGORY, "Fallback {}: range={} mat={} (no texture assigned)",
                channelName, rangeIdx, matIdx);
            return defaultTex;
        }
        if (static_cast<size_t>(textureIndex) >= imageHandles.size()) {
            fallbackOutOfBounds++;
            defaultFallbackCount++;
            Log::warning(LOG_CATEGORY, "Fallback {}: range={} mat={} texIdx={} (out of bounds, max={})",
                channelName, rangeIdx, matIdx, textureIndex, imageHandles.size());
            return defaultTex;
        }
        if (!imageHandles[textureIndex].isValid()) {
            fallbackInvalidHandle++;
            defaultFallbackCount++;
            Log::warning(LOG_CATEGORY, "Fallback {}: range={} mat={} texIdx={} (image failed to load)",
                channelName, rangeIdx, matIdx, textureIndex);
            return defaultTex;
        }
        return imageHandles[textureIndex];
    };

    // Helper to create texture array for a material property
    auto createTextureArray =
        [&](primitives::StoreHandle& arrayHandle,
            std::function<int(const EditorMaterial&)> getMaterialIndex,
            primitives::StoreHandle defaultTex,
            const char* channelName) {
            arrayHandle = store.newArray();
            auto& array = store.arrays[arrayHandle.handle];
            array.type = primitives::Type::Image;
            array.handles.resize(modelData.ranges.size());

            for (size_t i = 0; i < modelData.ranges.size(); ++i) {
                int matIdx = modelData.ranges[i].materialIndex;
                int texIdx = -1;
                if (matIdx >= 0 &&
                    static_cast<size_t>(matIdx) < materials.size()) {
                    texIdx = getMaterialIndex(materials[matIdx]);
                }
                array.handles[i] =
                    getTextureHandle(texIdx, defaultTex, channelName, i, matIdx).handle;
            }
        };

    // Debug: log material texture indices
    int validBaseColor = 0, validMetRough = 0, validNormal = 0, validEmissive = 0;
    for (size_t i = 0; i < materials.size(); ++i) {
        const auto& m = materials[i];
        if (m.baseColorTextureIndex >= 0) validBaseColor++;
        if (m.metallicRoughnessTextureIndex >= 0) validMetRough++;
        if (m.normalTextureIndex >= 0) validNormal++;
        if (m.emissiveTextureIndex >= 0) validEmissive++;
    }
    Log::debug(LOG_CATEGORY, "Materials: {} total, baseColor={}, metRough={}, normal={}, emissive={}",
        materials.size(), validBaseColor, validMetRough, validNormal, validEmissive);

    // Debug: count ranges without materials
    int rangesNoMaterial = 0;
    for (const auto& range : modelData.ranges) {
        if (range.materialIndex < 0) rangesNoMaterial++;
    }
    if (rangesNoMaterial > 0) {
        Log::debug(LOG_CATEGORY, "Ranges without material: {}/{}", rangesNoMaterial, modelData.ranges.size());
    }

    // Create texture arrays for each PBR channel with appropriate defaults
    createTextureArray(
        baseColorArray_,
        [](const EditorMaterial& m) { return m.baseColorTextureIndex; },
        defaultWhiteTexture_,
        "baseColor"
    );

    createTextureArray(
        metallicRoughnessArray_,
        [](const EditorMaterial& m) {
            return m.metallicRoughnessTextureIndex;
        },
        defaultProjectTexture_,
        "metallicRoughness"
    );

    createTextureArray(
        normalArray_,
        [](const EditorMaterial& m) { return m.normalTextureIndex; },
        defaultNormalTexture_,
        "normal"
    );

    createTextureArray(
        emissiveArray_,
        [](const EditorMaterial& m) { return m.emissiveTextureIndex; },
        defaultBlackTexture_,
        "emissive"
    );

    Log::debug(
        LOG_CATEGORY,
        "Created material texture arrays for {} geometry ranges ({} default fallbacks: {} no texture, {} out of bounds, {} invalid handle)",
        modelData.ranges.size(),
        defaultFallbackCount,
        fallbackNoTexture,
        fallbackOutOfBounds,
        fallbackInvalidHandle
    );
}

void MaterialNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>&
        outputs
) const {
    if (baseColorArray_.isValid()) {
        outputs.push_back({baseColorPin.id, baseColorArray_});
    }
    if (metallicRoughnessArray_.isValid()) {
        outputs.push_back({metallicRoughnessPin.id, metallicRoughnessArray_});
    }
    if (normalArray_.isValid()) {
        outputs.push_back({normalPin.id, normalArray_});
    }
    if (emissiveArray_.isValid()) {
        outputs.push_back({emissivePin.id, emissiveArray_});
    }
}
