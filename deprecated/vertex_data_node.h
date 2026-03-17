#pragma once
#include "model_node_base.h"

/**
 * @class VertexDataNode
 * @brief Outputs vertex/index data from a model as arrays per geometry range.
 *
 * Single output pin: vertexDataPin (VertexData type)
 * Outputs an array of VertexData primitives, one per geometry range.
 *
 * Use case: Connect to Pipeline node's vertex data input for rendering.
 */
class VertexDataNode : public ModelNodeBase {
public:
    VertexDataNode();
    explicit VertexDataNode(int id);
    ~VertexDataNode() override;

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
