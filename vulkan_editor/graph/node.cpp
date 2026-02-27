#include "node.h"
#include "pin_registry.h"
#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace {
constexpr float PADDING_X = 10.0f;
constexpr float MIN_NODE_WIDTH = 140.0f;
constexpr float MAX_NODE_WIDTH = 360.0f;
constexpr float OUTPUT_PADDING_X = 16.0f;
constexpr float PIN_GAP = 4.0f;
constexpr float ICON_SIZE_FACTOR = 0.75f;
} // namespace

// Static helper implementations
float Node::CalculateNodeWidth(
    const std::string& nodeName,
    const std::vector<std::string>& pinLabels
) {
    float maxLabelWidth = ImGui::CalcTextSize(nodeName.c_str()).x;

    for (const auto& label : pinLabels) {
        float labelWidth = ImGui::CalcTextSize(label.c_str()).x + 40.0f;
        maxLabelWidth = ImMax(maxLabelWidth, labelWidth);
    }

    float desiredWidth = maxLabelWidth + (PADDING_X * 4.0f);
    return ImClamp(desiredWidth, MIN_NODE_WIDTH, MAX_NODE_WIDTH);
}

void Node::DrawInputPin(
    ax::NodeEditor::PinId pinId,
    const std::string& label,
    int pinType,
    bool isLinked,
    float nodeWidth,
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
) {
    builder.Input(pinId);

    float alpha = ImGui::GetStyle().Alpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    // Draw icon - white for input pins
    DrawPinIcon(pinType, isLinked, static_cast<int>(alpha * 255), true);

    ImGui::SameLine(0, PIN_GAP);
    ImGui::TextUnformatted(label.c_str());

    // Dummy for spacing
    ImGui::Dummy(ImVec2(nodeWidth - 40.0f, 0));

    ImGui::PopStyleVar();
    builder.EndInput();
}

void Node::DrawOutputPin(
    ax::NodeEditor::PinId pinId,
    const std::string& label,
    int pinType,
    bool isLinked,
    float nodeWidth,
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
) {
    float alpha = ImGui::GetStyle().Alpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    builder.Output(pinId);

    // Calculate offset to push content to the right
    float iconSize = ImGui::GetTextLineHeight() * ICON_SIZE_FACTOR;
    float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
    float totalWidth = labelWidth + PIN_GAP + iconSize;
    float offset = nodeWidth - totalWidth - OUTPUT_PADDING_X;

    if (offset > 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }

    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine(0, PIN_GAP);

    // Draw icon - red for output pins
    DrawPinIcon(pinType, isLinked, static_cast<int>(alpha * 255), false);

    builder.EndOutput();
    ImGui::PopStyleVar();
}

void Node::DrawPinIcon(
    int pinType,
    bool connected,
    int alpha,
    bool isInput
) {
    int iconSize = static_cast<int>(ImGui::GetTextLineHeight());
    ax::Widgets::IconType iconType = ax::Widgets::IconType::Circle;

    // Black for input pins, bright red for output pins
    ImColor color = isInput ? ImColor(0, 0, 0) : ImColor(255, 50, 50);
    color.Value.w = alpha / 255.0f;

    ax::Widgets::Icon(
        ImVec2(
            static_cast<float>(iconSize), static_cast<float>(iconSize)
        ),
        iconType, connected, color, ImColor(32, 32, 32, alpha)
    );
}

void Node::unregisterPins(PinRegistry& registry) {
    registry.unregisterPinsForNode(id);
}