#pragma once
#include "node.h"
#include "vulkan_editor/io/serialization.h"
#include "vulkan_editor/gpu/primitives.h"
#include <glm/glm.hpp>
#include <vector>

using namespace ShaderTypes;

// Use LightData from primitives namespace
using LightData = primitives::LightData;

class LightNode : public Node, public ISerializable {
public:
    LightNode();
    LightNode(int id);
    ~LightNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const override;

    // Serialization
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    void ensureLightCount();

    // Light array configuration
    std::vector<LightData> lights;
    int numLights{6};

    // When connected to a pipeline, this is true and numLights is
    // read-only
    bool shaderControlledCount{false};

    // Public pin for node graph connections
    Pin lightArrayPin;

private:
    void createDefaultPins();
    primitives::UniformBuffer* lightUbo{nullptr};
    primitives::Light* lightPrimitive{nullptr};
    primitives::StoreHandle lightUboArray{};
};