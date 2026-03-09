#pragma once
#include "model_node_base.h"

/**
 * @class MaterialNode
 * @brief Outputs PBR texture arrays from a model.
 *
 * Output pins (each is an array of textures, one per geometry range):
 * - baseColorPin (Image) - Base color / albedo textures
 * - emissivePin (Image) - Emissive textures
 * - metallicRoughnessPin (Image) - Metallic-roughness packed textures
 * - normalPin (Image) - Normal map textures
 *
 * Missing textures use generated default textures:
 * - baseColor: white (1,1,1,1)
 * - emissive: black (0,0,0,1)
 * - metallicRoughness: default values (metal=1, rough=1) as (255, 255, 0, 255)
 * - normal: flat normal (0.5, 0.5, 1.0) as (128, 128, 255, 255)
 */
class MaterialNode : public ModelNodeBase {
public:
    MaterialNode();
    explicit MaterialNode(int id);
    ~MaterialNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    void registerPins(PinRegistry& registry) override;
    bool usesPinRegistry() const override { return usesRegistry_; }

    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>&
            outputs
    ) const override;

    // Output pins
    Pin baseColorPin;
    Pin emissivePin;
    Pin metallicRoughnessPin;
    Pin normalPin;

    PinHandle baseColorPinHandle = INVALID_PIN_HANDLE;
    PinHandle emissivePinHandle = INVALID_PIN_HANDLE;
    PinHandle metallicRoughnessPinHandle = INVALID_PIN_HANDLE;
    PinHandle normalPinHandle = INVALID_PIN_HANDLE;

private:
    void createDefaultPins();
    bool usesRegistry_ = false;

    // Primitive handles for texture arrays
    primitives::StoreHandle baseColorArray_{};
    primitives::StoreHandle emissiveArray_{};
    primitives::StoreHandle metallicRoughnessArray_{};
    primitives::StoreHandle normalArray_{};

    // Default texture handles
    primitives::StoreHandle defaultWhiteTexture_{};
    primitives::StoreHandle defaultBlackTexture_{};
    primitives::StoreHandle defaultNormalTexture_{};
    primitives::StoreHandle defaultMetallicRoughnessTexture_{};

    // Default texture pixel data (persisted for GPU upload)
    std::vector<uint8_t> defaultWhitePixels_;
    std::vector<uint8_t> defaultBlackPixels_;
    std::vector<uint8_t> defaultNormalPixels_;
    std::vector<uint8_t> defaultMetallicRoughnessPixels_;

    // Helper to create a 1x1 default texture
    primitives::StoreHandle createDefaultTexture(
        primitives::Store& store,
        std::vector<uint8_t>& pixelStorage,
        uint8_t r,
        uint8_t g,
        uint8_t b,
        uint8_t a
    );

    // Helper to create image primitive from EditorImage
    primitives::StoreHandle createImagePrimitive(
        primitives::Store& store,
        const EditorImage& image
    );
};
