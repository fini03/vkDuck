#pragma once
#include "model_node_base.h"
#include <glm/gtc/matrix_transform.hpp>

/**
 * @class UBONode
 * @brief Outputs uniform buffer data (matrices, camera, lights) from a model.
 *
 * Output pins:
 * - modelMatrixPin (UniformBuffer) - per-geometry model/normal matrices
 * - cameraPin (UniformBuffer) - GLTF embedded camera (if present)
 * - lightPin (UniformBuffer) - GLTF embedded lights (if present)
 *
 * Camera/light pins only appear if the model contains them.
 * UI shows dropdown to select which camera/light to use.
 */
class UBONode : public ModelNodeBase {
public:
    UBONode();
    explicit UBONode(int id);
    ~UBONode() override;

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

    // Camera/light selection (from GLTF)
    int selectedCameraIndex{-1};
    int selectedLightIndex{-1};
    float aspectRatio{16.0f / 9.0f};

    void updateCameraFromSelection();
    void updateLightsFromGLTF();

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
    void onModelSet() override;
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
