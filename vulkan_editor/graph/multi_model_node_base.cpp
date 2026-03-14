#include "multi_model_node_base.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <cstring>
#include <imgui.h>
#include <imgui_node_editor.h>

#include "external/utilities/builders.h"

namespace {
constexpr float PADDING_X = 10.0f;
constexpr const char* LOG_CATEGORY = "MultiModelNodeBase";
} // namespace

namespace ed = ax::NodeEditor;

MultiModelNodeBase::MultiModelNodeBase()
    : Node() {
}

MultiModelNodeBase::MultiModelNodeBase(int id)
    : Node(id) {
}

MultiModelNodeBase::~MultiModelNodeBase() {
    // Release references to all cached models
    if (g_modelManager) {
        for (auto& entry : models_) {
            if (entry.handle.isValid()) {
                g_modelManager->removeReference(entry.handle);
            }
        }
    }
}

void MultiModelNodeBase::addModel(ModelHandle handle) {
    if (!g_modelManager || !g_modelManager->isLoaded(handle)) {
        Log::warning(LOG_CATEGORY, "Cannot add model: handle not loaded");
        return;
    }

    // Check if model is already in the list (prevent duplicates)
    for (const auto& entry : models_) {
        if (entry.handle == handle) {
            Log::debug(LOG_CATEGORY, "Model already in node, skipping duplicate");
            return;
        }
    }

    // Add reference
    g_modelManager->addReference(handle);

    // Create new entry
    ModelEntry entry;
    entry.handle = handle;
    entry.enabled = true;

    // Copy path for serialization
    const CachedModel* cached = g_modelManager->getModel(handle);
    if (cached) {
        std::strncpy(entry.path, cached->path.string().c_str(),
                     sizeof(entry.path) - 1);
        entry.path[sizeof(entry.path) - 1] = '\0';
        Log::info(LOG_CATEGORY, "Added model '{}'", cached->displayName);
    }

    models_.push_back(entry);

    // Rebuild consolidated data
    rebuildConsolidatedData();
    onModelsChanged();
}

void MultiModelNodeBase::removeModel(size_t index) {
    if (index >= models_.size()) {
        Log::warning(LOG_CATEGORY, "Cannot remove model: index out of range");
        return;
    }

    // Release reference
    if (g_modelManager && models_[index].handle.isValid()) {
        g_modelManager->removeReference(models_[index].handle);
    }

    models_.erase(models_.begin() + static_cast<ptrdiff_t>(index));

    // Rebuild consolidated data
    rebuildConsolidatedData();
    onModelsChanged();
}

void MultiModelNodeBase::setModelEnabled(size_t index, bool enabled) {
    if (index >= models_.size()) {
        return;
    }

    if (models_[index].enabled != enabled) {
        models_[index].enabled = enabled;

        // Rebuild consolidated data
        rebuildConsolidatedData();
        onModelsChanged();
    }
}

void MultiModelNodeBase::reorderModel(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= models_.size() || toIndex >= models_.size()) {
        return;
    }

    if (fromIndex == toIndex) {
        return;
    }

    ModelEntry entry = models_[fromIndex];
    models_.erase(models_.begin() + static_cast<ptrdiff_t>(fromIndex));
    models_.insert(models_.begin() + static_cast<ptrdiff_t>(toIndex), entry);

    // Rebuild consolidated data
    rebuildConsolidatedData();
    onModelsChanged();
}

bool MultiModelNodeBase::hasModels() const {
    for (const auto& entry : models_) {
        if (entry.handle.isValid() && entry.enabled &&
            g_modelManager && g_modelManager->isLoaded(entry.handle)) {
            return true;
        }
    }
    return false;
}

void MultiModelNodeBase::rebuildConsolidatedData() {
    // Clear all consolidated data
    consolidatedVertices_.clear();
    consolidatedIndices_.clear();
    consolidatedRanges_.clear();
    rangeInfo_.clear();
    mergedMaterials_.clear();
    mergedImages_.clear();
    mergedCameras_.clear();
    mergedLights_.clear();
    textureIndexRemap_.clear();

    if (!g_modelManager) {
        return;
    }

    uint32_t currentVertexOffset = 0;
    uint32_t currentIndexOffset = 0;
    int currentMaterialOffset = 0;
    int currentImageOffset = 0;

    for (size_t mi = 0; mi < models_.size(); ++mi) {
        const ModelEntry& entry = models_[mi];

        // Skip disabled models
        if (!entry.enabled) {
            textureIndexRemap_.push_back({});
            continue;
        }

        // Skip invalid/unloaded models
        if (!entry.handle.isValid()) {
            textureIndexRemap_.push_back({});
            continue;
        }

        const CachedModel* cached = g_modelManager->getModel(entry.handle);
        if (!cached || cached->status != ModelStatus::Loaded) {
            textureIndexRemap_.push_back({});
            continue;
        }

        const auto& modelData = cached->modelData;

        // Build texture index remap for this model
        std::unordered_map<int, int> texRemap;
        for (size_t i = 0; i < cached->images.size(); ++i) {
            texRemap[static_cast<int>(i)] =
                currentImageOffset + static_cast<int>(i);
            mergedImages_.push_back(
                const_cast<EditorImage*>(&cached->images[i]));
        }
        textureIndexRemap_.push_back(texRemap);

        // Copy materials with remapped texture indices
        for (const auto& mat : cached->materials) {
            EditorMaterial remapped = mat;
            auto remapIndex = [&texRemap](int idx) -> int {
                if (idx < 0)
                    return -1;
                auto it = texRemap.find(idx);
                return (it != texRemap.end()) ? it->second : -1;
            };
            remapped.baseColorTextureIndex =
                remapIndex(mat.baseColorTextureIndex);
            remapped.emissiveTextureIndex =
                remapIndex(mat.emissiveTextureIndex);
            remapped.metallicRoughnessTextureIndex =
                remapIndex(mat.metallicRoughnessTextureIndex);
            remapped.normalTextureIndex = remapIndex(mat.normalTextureIndex);
            mergedMaterials_.push_back(remapped);
        }

        // Append vertices
        consolidatedVertices_.insert(consolidatedVertices_.end(),
                                     modelData.vertices.begin(),
                                     modelData.vertices.end());

        // Append indices (already relative within each model, so just copy)
        consolidatedIndices_.insert(consolidatedIndices_.end(),
                                    modelData.indices.begin(),
                                    modelData.indices.end());

        // Create consolidated ranges with material index offset
        for (size_t ri = 0; ri < modelData.ranges.size(); ++ri) {
            const auto& srcRange = modelData.ranges[ri];

            EditorGeometryRange newRange;
            newRange.firstVertex = srcRange.firstVertex + currentVertexOffset;
            newRange.vertexCount = srcRange.vertexCount;
            newRange.firstIndex = srcRange.firstIndex + currentIndexOffset;
            newRange.indexCount = srcRange.indexCount;
            newRange.materialIndex =
                (srcRange.materialIndex >= 0)
                    ? srcRange.materialIndex + currentMaterialOffset
                    : -1;
            newRange.topology = srcRange.topology;

            consolidatedRanges_.push_back(newRange);

            // Track range origin
            ConsolidatedRangeInfo info;
            info.modelIndex = mi;
            info.originalRangeIndex = ri;
            info.vertexOffset = currentVertexOffset;
            info.indexOffset = currentIndexOffset;
            rangeInfo_.push_back(info);
        }

        // Merge cameras and lights
        mergedCameras_.insert(mergedCameras_.end(), cached->cameras.begin(),
                              cached->cameras.end());
        mergedLights_.insert(mergedLights_.end(), cached->lights.begin(),
                             cached->lights.end());

        // Update offsets for next model
        currentVertexOffset +=
            static_cast<uint32_t>(modelData.vertices.size());
        currentIndexOffset += static_cast<uint32_t>(modelData.indices.size());
        currentMaterialOffset += static_cast<int>(cached->materials.size());
        currentImageOffset += static_cast<int>(cached->images.size());
    }

    Log::info(LOG_CATEGORY,
              "Consolidated {} models: {} vertices, {} indices, {} ranges",
              models_.size(), consolidatedVertices_.size(),
              consolidatedIndices_.size(), consolidatedRanges_.size());
}

nlohmann::json MultiModelNodeBase::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};

    // Serialize all models
    j["models"] = nlohmann::json::array();
    for (const auto& entry : models_) {
        j["models"].push_back(
            {{"path", entry.path}, {"enabled", entry.enabled}});
    }

    return j;
}

void MultiModelNodeBase::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Multi Model Node");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position =
            ImVec2(j["position"][0].get<float>(), j["position"][1].get<float>());
    }

    // Note: models are loaded by the graph serializer after fromJson()
    // We just store the paths here for reference
    if (j.contains("models") && j["models"].is_array()) {
        for (const auto& modelJson : j["models"]) {
            ModelEntry entry;
            entry.handle = {}; // Will be set by graph serializer
            entry.enabled = modelJson.value("enabled", true);

            std::string path = modelJson.value("path", "");
            std::strncpy(entry.path, path.c_str(), sizeof(entry.path) - 1);
            entry.path[sizeof(entry.path) - 1] = '\0';

            models_.push_back(entry);
        }
    }
}

void MultiModelNodeBase::renderMultiModelNodeHeader(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    float nodeWidth) const {
    // Darker orange background for multi-model nodes
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(200, 100, 0, 80));

    builder.Begin(id);

    // Draw header - orange for model nodes
    builder.Header(ImColor(255, 140, 0));

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
            const_cast<MultiModelNodeBase*>(this)->isRenaming = true;
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
            const_cast<MultiModelNodeBase*>(this)->name = nameBuffer;
            const_cast<MultiModelNodeBase*>(this)->isRenaming = false;
        }
    }

    // Show model count badge
    ImGui::SameLine();
    size_t enabledCount = 0;
    for (const auto& entry : models_) {
        if (entry.enabled)
            ++enabledCount;
    }
    ImGui::TextDisabled("[%zu]", enabledCount);

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();
}

float MultiModelNodeBase::calculateMultiModelNodeWidth(
    const std::string& nodeName, const std::vector<std::string>& pinLabels) {
    return CalculateNodeWidth(nodeName, pinLabels);
}
