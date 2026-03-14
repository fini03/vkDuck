#include "multi_model_consumer_base.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <imgui.h>
#include <imgui_node_editor.h>

#include "external/utilities/builders.h"

namespace {
constexpr float PADDING_X = 10.0f;
} // namespace

namespace ed = ax::NodeEditor;

MultiModelConsumerBase::MultiModelConsumerBase()
    : Node() {
    // Create source input pin
    sourceInputPin.id = ed::PinId(GetNextGlobalId());
    sourceInputPin.type = PinType::ModelSource;
    sourceInputPin.label = "Source";
}

MultiModelConsumerBase::MultiModelConsumerBase(int id)
    : Node(id) {
    // Create source input pin
    sourceInputPin.id = ed::PinId(GetNextGlobalId());
    sourceInputPin.type = PinType::ModelSource;
    sourceInputPin.label = "Source";
}

void MultiModelConsumerBase::registerSourceInputPin(PinRegistry& registry) {
    sourceInputPinHandle = registry.registerPinWithId(
        id,
        sourceInputPin.id,
        sourceInputPin.type,
        PinKind::Input,
        sourceInputPin.label
    );
    usesRegistry_ = true;
}

MultiModelSourceNode* MultiModelConsumerBase::findSourceNode(
    NodeGraph& graph
) const {
    // Find link connected to our input pin
    for (const auto& link : graph.links) {
        if (link.endPin == sourceInputPin.id) {
            // Find the node that owns the start pin
            auto result = graph.findPin(link.startPin);
            if (result.node) {
                if (auto* source = dynamic_cast<MultiModelSourceNode*>(result.node)) {
                    return source;
                }
            }
        }
    }
    return nullptr;
}

bool MultiModelConsumerBase::hasValidSource(NodeGraph& graph) const {
    MultiModelSourceNode* source = findSourceNode(graph);
    return source && source->hasModels();
}

void MultiModelConsumerBase::renderSourceWarning() const {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    ImGui::TextUnformatted("Connect to Model Source");
    ImGui::PopStyleColor();
}

void MultiModelConsumerBase::renderConsumerNodeHeader(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    float nodeWidth
) const {
    // Purple background for consumer nodes (to distinguish from source)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(120, 80, 160, 80));

    builder.Begin(id);

    // Draw header - purple for consumer nodes
    builder.Header(ImColor(160, 100, 200));

    float availWidth = nodeWidth - PADDING_X * 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    if (!isRenaming) {
        // Center text if it fits
        if (textSize.x < availWidth) {
            float centerOffset = (availWidth - textSize.x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            const_cast<MultiModelConsumerBase*>(this)->isRenaming = true;
        }
    } else {
        // Editable name
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText("##NodeName", nameBuffer, sizeof(nameBuffer),
                         ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<MultiModelConsumerBase*>(this)->name = nameBuffer;
            const_cast<MultiModelConsumerBase*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();
}

void MultiModelConsumerBase::sourceInputPinToJson(nlohmann::json& j) const {
    if (!j.contains("inputPins")) {
        j["inputPins"] = nlohmann::json::array();
    }
    j["inputPins"].push_back({
        {"id", sourceInputPin.id.Get()},
        {"type", static_cast<int>(sourceInputPin.type)},
        {"label", sourceInputPin.label}
    });
}

void MultiModelConsumerBase::sourceInputPinFromJson(const nlohmann::json& j) {
    if (j.contains("inputPins") && j["inputPins"].is_array()) {
        auto& pins = j["inputPins"];
        // First input pin is always the source input
        if (pins.size() > 0) {
            sourceInputPin.id = ed::PinId(pins[0]["id"].get<int>());
        }
    }
}

float MultiModelConsumerBase::calculateConsumerNodeWidth(
    const std::string& nodeName,
    const std::vector<std::string>& pinLabels
) {
    return CalculateNodeWidth(nodeName, pinLabels);
}
