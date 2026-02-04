#include "present_node.h"
#include "node_graph.h"
#include "external/utilities/builders.h"
#include "vulkan_editor/gpu/primitives.h"
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

PresentNode::PresentNode()
    : Node() {
    name = "Present";
    createDefaultPins();
}

PresentNode::PresentNode(int id)
    : Node(id) {
    name = "Present";
    createDefaultPins();
}

PresentNode::~PresentNode() {}

nlohmann::json PresentNode::toJson() const {
    nlohmann::json j;
    j["type"] = "present";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};

    // Store input pin
    j["inputPins"] = nlohmann::json::array();
    j["inputPins"].push_back(
        {{"id", imagePin.id.Get()},
         {"type", static_cast<int>(imagePin.type)},
         {"label", imagePin.label}}
    );

    return j;
}

void PresentNode::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Present");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }

    // Restore input pin by index
    if (j.contains("inputPins") && j["inputPins"].is_array() &&
        !j["inputPins"].empty()) {
        imagePin.id = ed::PinId(j["inputPins"][0]["id"].get<int>());
    }
}

void PresentNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    namespace ed = ax::NodeEditor;
    std::vector<std::string> pinLabels = {imagePin.label};
    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(35, 145, 35., 80));

    builder.Begin(id);

    // Draw header - green for present node
    builder.Header(ImColor(50, 205, 50));

    float availWidth = nodeWidth - 20.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    // Center text
    if (textSize.x < availWidth) {
        float centerOffset = (availWidth - textSize.x) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
    }

    ImGui::TextUnformatted(name.c_str());

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    // Draw input pin
    DrawInputPin(
        imagePin.id, imagePin.label, static_cast<int>(imagePin.type),
        graph.isPinLinked(imagePin.id), nodeWidth, builder
    );

    builder.End();
    ed::PopStyleColor();
}

void PresentNode::createDefaultPins() {
    imagePin.id = ed::PinId(GetNextGlobalId());
    imagePin.type = PinType::Image;
    imagePin.label = "Presentation Image";
}

void PresentNode::clearPrimitives() {
    present = {};
}

void PresentNode::createPrimitives(primitives::Store& store) {
    present = store.newPresent();
}

void PresentNode::getInputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::LinkSlot>>& inputs
) const {
    assert(present.isValid());
    inputs.push_back({imagePin.id, {.handle = present, .slot = 0}});
}