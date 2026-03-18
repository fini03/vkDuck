#pragma once
#include "node.h"
#include "pin_registry.h"
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

    // Pin registration (new system)
    void registerPins(PinRegistry& registry) override;
    bool usesPinRegistry() const override { return usesRegistry; }

    // Pin lookup for O(1) findPin
    PinLookup getPinById(ax::NodeEditor::PinId id) override {
        if (lightArrayPin.id == id) return {&lightArrayPin, false};
        return {};
    }

    void ensureLightCount();

    // Light array configuration - user-controlled dynamic size
    primitives::LightsBuffer lightsBuffer;
    int numLights{6};  // User-configurable count (no limit)

    // Legacy pin (kept for backwards compatibility)
    Pin lightArrayPin;

    // New registry handle
    PinHandle lightArrayPinHandle = INVALID_PIN_HANDLE;

private:
    void createDefaultPins();
    primitives::UniformBuffer* lightUbo{nullptr};
    primitives::Light* lightPrimitive{nullptr};
    primitives::StoreHandle lightUboArray{};
    bool usesRegistry = false;
};