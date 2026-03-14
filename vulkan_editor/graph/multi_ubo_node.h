#pragma once
#include "multi_model_node_base.h"
#include <glm/gtc/matrix_transform.hpp>

/**
 * @class MultiUBONode
 * @brief Outputs combined uniform buffer data from multiple models.
 *
 * Output pins:
 * - modelMatrixPin (UniformBuffer[]) - per-geometry model/normal matrices
 * - cameraPin (UniformBuffer) - selected GLTF camera from merged cameras
 * - lightPin (UniformBuffer) - combined GLTF lights from all models
 *
 * Camera/light pins only appear if any model contains them.
 * UI shows dropdown to select which camera to use from the combined list.
 */
class MultiUBONode : public MultiModelNodeBase {
public:
    MultiUBONode();
    explicit MultiUBONode(int id);
    ~MultiUBONode() override;

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

    // Camera/light selection (from merged cameras/lights)
    int selectedCameraIndex{-1};
    float aspectRatio{16.0f / 9.0f};

    void updateCameraFromSelection();
    void updateLightsFromMerged();

    // Returns pins that should have their links removed
    std::vector<ax::NodeEditor::PinId> getPinsToUnlink() const;

    // Output pins
    Pin modelMatrixPin;
    Pin cameraPin;
    Pin lightPin;

    PinHandle modelMatrixPinHandle = INVALID_PIN_HANDLE;
    PinHandle cameraPinHandle = INVALID_PIN_HANDLE;
    PinHandle lightPinHandle = INVALID_PIN_HANDLE;

    // Camera data for external access
    ModelCameraData cameraData;

private:
    void createDefaultPins();
    void onModelsChanged() override;
    bool usesRegistry_ = false;

    // Primitive handles
    primitives::StoreHandle modelMatrixArray_{};
    primitives::StoreHandle cameraUboArray_{};
    primitives::StoreHandle lightUboArray_{};

    // Per-node data storage
    std::vector<ModelMatrices> modelMatricesData_;
    primitives::CameraType cameraType_{primitives::CameraType::Fixed};
    primitives::LightsBuffer lightsBuffer_;

    primitives::UniformBuffer* cameraUbo_{nullptr};
    primitives::UniformBuffer* lightUbo_{nullptr};
    primitives::Light* lightPrimitive_{nullptr};
};
