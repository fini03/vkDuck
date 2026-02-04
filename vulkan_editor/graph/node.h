#pragma once
#include "../util/logger.h"
#include "../shader/shader_types.h"
#include "vulkan_editor/gpu/primitives.h"
#include <algorithm>
#include <imgui_node_editor.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace ax {
namespace NodeEditor {
namespace Utilities {
struct BlueprintNodeBuilder;
}
}
}

class TemplateLoader;
class NodeGraph;
struct ResourceHandle;
struct Link;

using ShaderTypes::BindingInfo;
using ShaderTypes::MemberInfo;

/**
 * @class Node
 * @brief Abstract base class for all visual graph nodes in the pipeline editor.
 *
 * Nodes represent components of a Vulkan graphics pipeline (cameras, models,
 * pipelines, lights, etc.). Each node exposes input/output pins for connections
 * and can create GPU primitives for live rendering.
 *
 * Subclasses must implement:
 * - render(): Draw the node in the ImGui node editor
 * - createPrimitives(): Create GPU resources for live preview
 * - getOutputPrimitives()/getInputPrimitives(): Map pins to GPU handles
 */
class Node {
public:
    virtual void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const = 0;

    Node(int id)
        : id(id) {}

    Node() {
        id = GetNextGlobalId();
        Log::debug("Node", "Node created with id: {}", id);
    }

    virtual ~Node() {}

    int getId() const {
        return id;
    }

    virtual void clearPrimitives() {}
    virtual void createPrimitives(primitives::Store& store) {}

    virtual void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const {}

    virtual void getInputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::LinkSlot>>& inputs
    ) const {}

    static int GetNextGlobalId() {
        return ++s_GlobalIdCounter;
    }

    static void SetNextGlobalId(int id) {
        s_GlobalIdCounter = id;
    }

protected:
    static float CalculateNodeWidth(
        const std::string& nodeName,
        const std::vector<std::string>& pinLabels
    );

    static void DrawInputPin(
        ax::NodeEditor::PinId pinId,
        const std::string& label,
        int pinType,
        bool isLinked,
        float nodeWidth,
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
    );

    static void DrawOutputPin(
        ax::NodeEditor::PinId pinId,
        const std::string& label,
        int pinType,
        bool isLinked,
        float nodeWidth,
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
    );

    static void DrawPinIcon(
        int pinType,
        bool connected,
        int alpha,
        bool isInput
    );

public:
    std::string name{"m_graphicsPipeline"};
    bool isRenaming = false;
    int id;
    static inline int s_GlobalIdCounter = 0;

    ImVec2 position = ImVec2(100, 100);

    std::vector<BindingInfo> inputBindings;
    std::vector<BindingInfo> outputBindings;
};