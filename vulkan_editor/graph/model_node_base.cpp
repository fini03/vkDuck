#include "model_node_base.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <cstring>
#include <imgui.h>
#include <imgui_node_editor.h>

#include "external/utilities/builders.h"

namespace {
constexpr float PADDING_X = 10.0f;
constexpr const char* LOG_CATEGORY = "ModelNodeBase";
}

namespace ed = ax::NodeEditor;

ModelNodeBase::ModelNodeBase()
    : Node() {
}

ModelNodeBase::ModelNodeBase(int id)
    : Node(id) {
}

ModelNodeBase::~ModelNodeBase() {
    // Release reference to cached model (check for null during shutdown)
    if (modelHandle_.isValid() && g_modelManager) {
        g_modelManager->removeReference(modelHandle_);
    }
}

void ModelNodeBase::setModel(ModelHandle handle) {
    if (!g_modelManager->isLoaded(handle)) {
        Log::warning(LOG_CATEGORY, "Cannot set model: handle not loaded");
        return;
    }

    // Release previous reference
    if (modelHandle_.isValid()) {
        g_modelManager->removeReference(modelHandle_);
    }

    // Set new handle and add reference
    modelHandle_ = handle;
    g_modelManager->addReference(modelHandle_);

    // Update path for serialization
    const CachedModel* cached = g_modelManager->getModel(modelHandle_);
    if (cached) {
        std::strncpy(
            modelPath,
            cached->path.string().c_str(),
            sizeof(modelPath) - 1
        );
        modelPath[sizeof(modelPath) - 1] = '\0';
        Log::info(LOG_CATEGORY, "Model set to '{}'", cached->displayName);
    }

    // Allow derived classes to handle model change
    onModelSet();
}

bool ModelNodeBase::hasModel() const {
    return modelHandle_.isValid() && g_modelManager->isLoaded(modelHandle_);
}

const CachedModel* ModelNodeBase::getCachedModel() const {
    if (!modelHandle_.isValid()) {
        return nullptr;
    }
    return g_modelManager->getModel(modelHandle_);
}

nlohmann::json ModelNodeBase::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};
    j["modelPath"] = modelPath;
    return j;
}

void ModelNodeBase::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Model Node");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(),
            j["position"][1].get<float>()
        );
    }

    std::string path = j.value("modelPath", "");
    std::strncpy(modelPath, path.c_str(), sizeof(modelPath) - 1);
    modelPath[sizeof(modelPath) - 1] = '\0';
}

void ModelNodeBase::renderModelNodeHeader(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    float nodeWidth
) const {
    // Orange background for model nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(180, 115, 0, 80));

    builder.Begin(id);

    // Draw header - orange for model nodes
    builder.Header(ImColor(255, 165, 0));

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
            const_cast<ModelNodeBase*>(this)->isRenaming = true;
        }
    } else {
        // Editable name
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText(
            "##NodeName",
            nameBuffer,
            sizeof(nameBuffer),
            ImGuiInputTextFlags_AutoSelectAll
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<ModelNodeBase*>(this)->name = nameBuffer;
            const_cast<ModelNodeBase*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();
}

float ModelNodeBase::calculateModelNodeWidth(
    const std::string& nodeName,
    const std::vector<std::string>& pinLabels
) {
    return CalculateNodeWidth(nodeName, pinLabels);
}
