#pragma once
#include "../shader/shader_types.h"
#include "node.h"
#include "vulkan_editor/io/serialization.h"
#include "vulkan_editor/gpu/primitives.h"

using namespace ShaderTypes;

class PresentNode : public Node, public ISerializable {
public:
    PresentNode();
    PresentNode(int id);
    ~PresentNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    // ISerializable
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    Pin imagePin;

    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    virtual void getInputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::LinkSlot>>& inputs
    ) const override;

private:
    void createDefaultPins();

    primitives::StoreHandle present{};
};
