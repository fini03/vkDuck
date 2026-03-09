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

    // Emissive texture output
    emissivePin.id = ed::PinId(GetNextGlobalId());
    emissivePin.type = PinType::Image;
    emissivePin.label = "Emissive";

    // Metallic-roughness texture output
    metallicRoughnessPin.id = ed::PinId(GetNextGlobalId());
    metallicRoughnessPin.type = PinType::Image;
    metallicRoughnessPin.label = "MetallicRoughness";

    // Normal map texture output
    normalPin.id = ed::PinId(GetNextGlobalId());
    normalPin.type = PinType::Image;
    normalPin.label = "Normal";
}

void MaterialNode::registerPins(PinRegistry& registry) {
    baseColorPinHandle = registry.registerPinWithId(
        id,
        baseColorPin.id,
        baseColorPin.type,
        PinKind::Output,
        baseColorPin.label
    );

    emissivePinHandle = registry.registerPinWithId(
        id,
        emissivePin.id,
        emissivePin.type,
        PinKind::Output,
        emissivePin.label
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
        {"id", emissivePin.id.Get()},
        {"type", static_cast<int>(emissivePin.type)},
        {"label", emissivePin.label}
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

    return j;
}

void MaterialNode::fromJson(const nlohmann::json& j) {
    ModelNodeBase::fromJson(j);

    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0)
            baseColorPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1)
            emissivePin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2)
            metallicRoughnessPin.id = ed::PinId(pins[2]["id"].get<int>());
        if (pins.size() > 3)
            normalPin.id = ed::PinId(pins[3]["id"].get<int>());
    }
}

void MaterialNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    std::vector<std::string> pinLabels = {
        baseColorPin.label,
        emissivePin.label,
        metallicRoughnessPin.label,
        normalPin.label
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
        emissivePin.id,
        emissivePin.label,
        static_cast<int>(emissivePin.type),
        nodeGraph.isPinLinked(emissivePin.id),
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

    builder.End();
    ed::PopStyleColor();
}

void MaterialNode::clearPrimitives() {
    baseColorArray_ = {};
    emissiveArray_ = {};
    metallicRoughnessArray_ = {};
    normalArray_ = {};
    defaultWhiteTexture_ = {};
    defaultBlackTexture_ = {};
    defaultNormalTexture_ = {};
    defaultMetallicRoughnessTexture_ = {};
    defaultWhitePixels_.clear();
    defaultBlackPixels_.clear();
    defaultNormalPixels_.clear();
    defaultMetallicRoughnessPixels_.clear();
}

primitives::StoreHandle MaterialNode::createDefaultTexture(
    primitives::Store& store,
    std::vector<uint8_t>& pixelStorage,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a
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

    // Create default textures
    // White (255, 255, 255) for base color fallback
    defaultWhiteTexture_ = createDefaultTexture(
        store, defaultWhitePixels_, 255, 255, 255, 255
    );

    // Black (0, 0, 0) for emissive fallback
    defaultBlackTexture_ = createDefaultTexture(
        store, defaultBlackPixels_, 0, 0, 0, 255
    );

    // Flat normal (128, 128, 255) - points straight up in tangent space
    defaultNormalTexture_ = createDefaultTexture(
        store, defaultNormalPixels_, 128, 128, 255, 255
    );

    // Metallic-roughness default: Green channel = roughness, Blue channel = metallic
    // Default: fully rough (255), non-metallic (0)
    defaultMetallicRoughnessTexture_ = createDefaultTexture(
        store, defaultMetallicRoughnessPixels_, 0, 255, 0, 255
    );

    // Build image handle lookup (create primitives for all loaded images)
    std::vector<primitives::StoreHandle> imageHandles(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];
        if (image.pixels && image.toLoad) {
            imageHandles[i] = createImagePrimitive(store, image);
        }
    }

    // Helper to get texture handle or default
    auto getTextureHandle = [&](int textureIndex,
                                 primitives::StoreHandle defaultTex)
        -> primitives::StoreHandle {
        if (textureIndex >= 0 &&
            static_cast<size_t>(textureIndex) < imageHandles.size() &&
            imageHandles[textureIndex].isValid()) {
            return imageHandles[textureIndex];
        }
        return defaultTex;
    };

    // Helper to create texture array for a material property
    auto createTextureArray =
        [&](primitives::StoreHandle& arrayHandle,
            std::function<int(const EditorMaterial&)> getMaterialIndex,
            primitives::StoreHandle defaultTex) {
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
                    getTextureHandle(texIdx, defaultTex).handle;
            }
        };

    // Create texture arrays for each PBR channel
    createTextureArray(
        baseColorArray_,
        [](const EditorMaterial& m) { return m.baseColorTextureIndex; },
        defaultWhiteTexture_
    );

    createTextureArray(
        emissiveArray_,
        [](const EditorMaterial& m) { return m.emissiveTextureIndex; },
        defaultBlackTexture_
    );

    createTextureArray(
        metallicRoughnessArray_,
        [](const EditorMaterial& m) {
            return m.metallicRoughnessTextureIndex;
        },
        defaultMetallicRoughnessTexture_
    );

    createTextureArray(
        normalArray_,
        [](const EditorMaterial& m) { return m.normalTextureIndex; },
        defaultNormalTexture_
    );

    Log::debug(
        LOG_CATEGORY,
        "Created material texture arrays for {} geometry ranges",
        modelData.ranges.size()
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
    if (emissiveArray_.isValid()) {
        outputs.push_back({emissivePin.id, emissiveArray_});
    }
    if (metallicRoughnessArray_.isValid()) {
        outputs.push_back({metallicRoughnessPin.id, metallicRoughnessArray_});
    }
    if (normalArray_.isValid()) {
        outputs.push_back({normalPin.id, normalArray_});
    }
}
