#pragma once
#include "multi_model_consumer_base.h"
#include "vulkan_editor/io/serialization.h"

/**
 * @class MultiVertexDataNode
 * @brief Consumer node that outputs combined vertex/index data from a model source.
 *
 * Connects to a MultiModelSourceNode via input pin and creates VertexData primitives
 * from the consolidated geometry data.
 *
 * Single output pin: vertexDataPin (VertexData type)
 * Outputs an array of VertexData primitives, one per consolidated geometry range.
 *
 * Use case: Connect to a Model Source to get combined geometry for rendering.
 */
class MultiVertexDataNode : public MultiModelConsumerBase, public ISerializable {
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

    // Store graph reference for accessing source node during createPrimitives
    void setGraph(NodeGraph* graph) { graph_ = graph; }

    // Output pin
    Pin vertexDataPin;
    PinHandle vertexDataPinHandle = INVALID_PIN_HANDLE;

private:
    void createDefaultPins();
    NodeGraph* graph_ = nullptr;
    primitives::StoreHandle vertexDataArray_{};
};
