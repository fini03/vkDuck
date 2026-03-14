#pragma once
#include "multi_model_node_base.h"

/**
 * @class MultiVertexDataNode
 * @brief Outputs combined vertex/index data from multiple models.
 *
 * Single output pin: vertexDataPin (VertexData type)
 * Outputs an array of VertexData primitives, one per consolidated geometry range.
 *
 * All geometry from enabled models is merged into consolidated vertex/index buffers
 * with properly rebased indices and ranges.
 *
 * Use case: Load multiple GLTF files and render them as a combined scene.
 */
class MultiVertexDataNode : public MultiModelNodeBase {
public:
    MultiVertexDataNode();
    explicit MultiVertexDataNode(int id);
    ~MultiVertexDataNode() override;

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

    // Output pin
    Pin vertexDataPin;
    PinHandle vertexDataPinHandle = INVALID_PIN_HANDLE;

private:
    void createDefaultPins();
    bool usesRegistry_ = false;
    primitives::StoreHandle vertexDataArray_{};
};
