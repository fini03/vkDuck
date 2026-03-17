#pragma once
#include "model_node_base.h"

/**
 * @class MaterialNode
 * @brief Outputs PBR texture arrays and material parameters from a model.
 *
 * Output pins (each array has one element per geometry range):
 * - baseColorPin (Image[]) - Base color / albedo textures
 * - metallicRoughnessPin (Image[]) - Metallic-roughness packed textures (G=rough, B=metal)
 * - normalPin (Image[]) - Normal map textures
 * - emissivePin (Image[]) - Emissive textures
 * - materialParamsPin (UBO[]) - PBR factors per geometry (MaterialParams struct)
 *
 * Default textures for missing materials (glTF PBR compliant):
 * - baseColor: white (1,1,1,1)
 * - metallicRoughness: (0, 1, 0) -> metallic=0 (dielectric), roughness=1 (fully rough)
 * - normal: flat (0.5, 0.5, 1) -> points straight up in tangent space
 * - emissive: black (0,0,0)
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
    Pin metallicRoughnessPin;
    Pin normalPin;
    Pin emissivePin;
    Pin materialParamsPin;

    PinHandle baseColorPinHandle = INVALID_PIN_HANDLE;
    PinHandle metallicRoughnessPinHandle = INVALID_PIN_HANDLE;
    PinHandle normalPinHandle = INVALID_PIN_HANDLE;
    PinHandle emissivePinHandle = INVALID_PIN_HANDLE;
    PinHandle materialParamsPinHandle = INVALID_PIN_HANDLE;

private:
    void createDefaultPins();
    bool usesRegistry_ = false;

    // Texture array handles (one texture per geometry range)
    primitives::StoreHandle baseColorArray_{};
    primitives::StoreHandle metallicRoughnessArray_{};
    primitives::StoreHandle normalArray_{};
    primitives::StoreHandle emissiveArray_{};
    primitives::StoreHandle materialParamsArray_{};

    // Default texture handles (1x1 fallbacks)
    primitives::StoreHandle defaultWhite_{};           // baseColor fallback
    primitives::StoreHandle defaultMetallicRough_{};   // metallic=0, roughness=1
    primitives::StoreHandle defaultNormal_{};          // flat normal (0.5, 0.5, 1)
    primitives::StoreHandle defaultBlack_{};           // emissive fallback

    // Pixel storage for default textures (persisted for GPU upload)
    std::vector<uint8_t> defaultWhitePixels_;
    std::vector<uint8_t> defaultMetallicRoughPixels_;
    std::vector<uint8_t> defaultNormalPixels_;
    std::vector<uint8_t> defaultBlackPixels_;

    // MaterialParams data per geometry range (persisted for GPU upload)
    std::vector<MaterialParams> materialParamsData_;

    // Create a 1x1 default texture with given RGBA values
    // linear=true for normal/metallic-roughness (UNORM), false for color data (SRGB)
    primitives::StoreHandle createDefaultTexture(
        primitives::Store& store,
        std::vector<uint8_t>& pixelStorage,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a,
        bool linear
    );

    // Create image primitive from loaded texture
    // linear=true for normal/metallic-roughness (UNORM), false for color data (SRGB)
    primitives::StoreHandle createImagePrimitive(
        primitives::Store& store,
        const EditorImage& image,
        bool linear
    );
};
