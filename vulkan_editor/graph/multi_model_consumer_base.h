#pragma once
#include "multi_model_source_node.h"
#include "node.h"
#include "pin_registry.h"
#include <imgui_node_editor.h>

using namespace ShaderTypes;

namespace ax::NodeEditor::Utilities {
struct BlueprintNodeBuilder;
}

class NodeGraph;

/**
 * @class MultiModelConsumerBase
 * @brief Base class for nodes that consume data from a MultiModelSourceNode.
 *
 * Consumer nodes connect to a MultiModelSourceNode via an input pin and
 * access the consolidated model data through the source node's accessors.
 *
 * Derived classes:
 * - MultiVertexDataNode: creates VertexData primitives
 * - MultiMaterialNode: creates PBR texture primitives
 * - MultiUBONode: creates UBO primitives (matrices, camera, lights)
 */
class MultiModelConsumerBase : public Node {
public:
    MultiModelConsumerBase();
    explicit MultiModelConsumerBase(int id);
    ~MultiModelConsumerBase() override = default;

    // Find connected source node via graph links
    MultiModelSourceNode* findSourceNode(NodeGraph& graph) const;

    // Check if connected to a valid source with models
    bool hasValidSource(NodeGraph& graph) const;

    // Input pin for source connection
    Pin sourceInputPin;
    PinHandle sourceInputPinHandle = INVALID_PIN_HANDLE;

    // Pin lookup for O(1) findPin - checks source input pin
    // Derived classes should call this then check their own pins
    PinLookup getPinById(ax::NodeEditor::PinId id) override {
        if (sourceInputPin.id == id) return {&sourceInputPin, true};
        return {};
    }

protected:
    // Register the source input pin (call from derived registerPins)
    void registerSourceInputPin(PinRegistry& registry);

    // Render warning when not connected to source
    void renderSourceWarning() const;

    // Common header rendering for consumer nodes (purple header)
    void renderConsumerNodeHeader(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        float nodeWidth
    ) const;

    // Serialize/deserialize source input pin
    void sourceInputPinToJson(nlohmann::json& j) const;
    void sourceInputPinFromJson(const nlohmann::json& j);

    // Calculate node width based on pin labels
    static float calculateConsumerNodeWidth(
        const std::string& nodeName,
        const std::vector<std::string>& pinLabels
    );

    bool usesRegistry_ = false;
};
