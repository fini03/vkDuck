#pragma once
#include "../asset/model_manager.h"
#include "../config/vulkan_enums.h"
#include "model_types.h"
#include "node.h"
#include "pin_registry.h"
#include "vulkan_editor/io/serialization.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

using namespace ShaderTypes;

// EditorImage, EditorMaterial, EditorGeometryRange, ConsolidatedModelData
// are now defined in model_manager.h to avoid circular dependencies

// Use primitives::LightData directly for shader compatibility
using LightData = primitives::LightData;

// GLTFCamera is now provided by vkDuck/model_loader.h

struct ModelSettings : public ISerializable {
    char modelPath[256] = "";
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    int topology = 0;
    bool primitiveRestart = false;
    glm::mat4 modelMatrix = glm::mat4(1.0f);

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["modelPath"] = modelPath;
        j["position"] = {position.x, position.y, position.z};
        j["rotation"] = {rotation.x, rotation.y, rotation.z};
        j["scale"] = {scale.x, scale.y, scale.z};
        j["topology"] = topology;
        j["primitiveRestart"] = primitiveRestart;
        return j;
    }

    void fromJson(const nlohmann::json& j) override {
        copyString("modelPath", j, modelPath, sizeof(modelPath));

        topology = j.value("topology", 0);
        primitiveRestart = j.value("primitiveRestart", false);
    }

private:
    static void copyString(
        const std::string& key,
        const nlohmann::json& j,
        char* dest,
        size_t size
    ) {
        std::string val = j.value(key, "");
        std::strncpy(dest, val.c_str(), size - 1);
        dest[size - 1] = '\0';
    }
};

/**
 * @class ModelNode
 * @brief References a cached 3D model for rendering in the pipeline.
 *
 * Uses ModelManager for loading and caching models. Handles per-node
 * settings like topology override, camera/light selection, and transform
 * matrices. File watching is managed by ModelManager.
 */
class ModelNode : public Node, public ISerializable {
public:
    // Use centralized Vulkan enum configuration
    static constexpr auto& topologyOptionsEnum = VkEnumConfig::topologyOptions;

    ModelNode();
    ModelNode(int id);
    ~ModelNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Pin registration (new system)
    void registerPins(PinRegistry& registry) override;
    bool usesPinRegistry() const override { return usesRegistry; }

    /**
     * @brief Load a model via ModelManager.
     *
     * The model is loaded and cached by ModelManager. This node
     * stores a handle to the cached model.
     *
     * @param relativePath Path relative to project root
     */
    void loadModel(const std::filesystem::path& relativePath);

    /**
     * @brief Set model from an existing ModelHandle.
     *
     * Useful when selecting a model from the Asset Library.
     */
    void setModel(ModelHandle handle);

    /**
     * @brief Get the current model handle.
     */
    ModelHandle getModelHandle() const { return modelHandle_; }

    /**
     * @brief Check if a model is loaded and ready.
     */
    bool hasModel() const;

    /**
     * @brief Get the cached model data (read-only).
     */
    const CachedModel* getCachedModel() const;

    static const std::vector<const char*> topologyOptions;
    ModelSettings settings;

    // Legacy pins (kept for backwards compatibility)
    Pin modelMatrixPin;
    Pin texturePin;
    Pin vertexDataPin;
    Pin cameraPin;
    Pin lightPin;

    // New registry handles
    PinHandle modelMatrixPinHandle = INVALID_PIN_HANDLE;
    PinHandle texturePinHandle = INVALID_PIN_HANDLE;
    PinHandle vertexDataPinHandle = INVALID_PIN_HANDLE;
    PinHandle cameraPinHandle = INVALID_PIN_HANDLE;
    PinHandle lightPinHandle = INVALID_PIN_HANDLE;

    // Per-node camera selection (indexes into cached model's cameras)
    int selectedCameraIndex{-1};
    bool needsCameraApply{false};
    ModelCameraData cameraData;
    float aspectRatio{16.0f / 9.0f};

    void updateCameraFromSelection();

    // Per-node light selection (indexes into cached model's lights)
    int selectedLightIndex{-1};
    primitives::LightsBuffer lightsBuffer;

    void updateLightsFromGLTF();
    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    virtual void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const override;

private:
    void createDefaultPins();
    void onModelReloaded();
    bool usesRegistry = false;

    // Handle to cached model in ModelManager
    ModelHandle modelHandle_;

    // GPU primitive handles (created per-node instance)
    primitives::StoreHandle baseTextureArray{};
    primitives::StoreHandle vertexDataArray{};
    primitives::StoreHandle modelMatrixArray{};
    primitives::StoreHandle cameraUboArray{};
    primitives::UniformBuffer* cameraUbo{nullptr};
    primitives::CameraType cameraType{primitives::CameraType::Fixed};
    primitives::StoreHandle lightUboArray{};
    primitives::UniformBuffer* lightUbo{nullptr};
    primitives::Light* lightPrimitive{nullptr};

    // Per-node model matrices (allows different transforms per node)
    std::vector<ModelMatrices> modelMatricesData;
};