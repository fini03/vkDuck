#pragma once
#include "../asset/model_manager.h"
#include "model_types.h"
#include "node.h"
#include "pin_registry.h"
#include "vulkan_editor/io/serialization.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ShaderTypes;

namespace ax::NodeEditor::Utilities {
struct BlueprintNodeBuilder;
}

/**
 * @struct ModelEntry
 * @brief Represents a single model in the multi-model source node.
 */
struct ModelEntry {
    ModelHandle handle;
    char path[256]{};
    bool enabled{true};
};

/**
 * @struct ConsolidatedRangeInfo
 * @brief Tracks the origin of each consolidated geometry range.
 */
struct ConsolidatedRangeInfo {
    size_t modelIndex;         // Which model in models_ array
    size_t originalRangeIndex; // Index in original model's ranges
    uint32_t vertexOffset;     // Offset into consolidated vertex buffer
    uint32_t indexOffset;      // Offset into consolidated index buffer
};

/**
 * @class MultiModelSourceNode
 * @brief Source node that manages multiple GLTF models and provides consolidated data.
 *
 * This node is the single source of truth for multi-model data. Consumer nodes
 * (MultiVertexDataNode, MultiMaterialNode, MultiUBONode) connect to this node's
 * output pin to access the consolidated model data.
 *
 * Features:
 * - Multiple ModelHandle management with reference counting
 * - Data consolidation (vertices, indices, materials, cameras, lights)
 * - Serialization of model paths array
 * - Orange header for model nodes
 */
class MultiModelSourceNode : public Node, public ISerializable {
public:
    MultiModelSourceNode();
    explicit MultiModelSourceNode(int id);
    ~MultiModelSourceNode() override;

    // Model management
    void addModel(ModelHandle handle);
    void removeModel(size_t index);
    void setModelEnabled(size_t index, bool enabled);
    void reorderModel(size_t fromIndex, size_t toIndex);

    size_t getModelCount() const { return models_.size(); }
    const ModelEntry& getModel(size_t index) const { return models_[index]; }
    ModelEntry& getModel(size_t index) { return models_[index]; }
    const std::vector<ModelEntry>& getModels() const { return models_; }
    bool hasModels() const;

    // Serialization
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Consolidated data accessors (for consumer nodes)
    const std::vector<Vertex>& getConsolidatedVertices() const {
        return consolidatedVertices_;
    }
    const std::vector<uint32_t>& getConsolidatedIndices() const {
        return consolidatedIndices_;
    }
    const std::vector<EditorGeometryRange>& getConsolidatedRanges() const {
        return consolidatedRanges_;
    }
    const std::vector<ConsolidatedRangeInfo>& getRangeInfo() const {
        return rangeInfo_;
    }
    const std::vector<EditorMaterial>& getMergedMaterials() const {
        return mergedMaterials_;
    }
    const std::vector<EditorImage*>& getMergedImages() const {
        return mergedImages_;
    }
    const std::vector<GLTFCamera>& getMergedCameras() const {
        return mergedCameras_;
    }
    const std::vector<GLTFLight>& getMergedLights() const {
        return mergedLights_;
    }
    const std::vector<std::unordered_map<int, int>>& getTextureIndexRemap() const {
        return textureIndexRemap_;
    }

    // Node interface
    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& nodeGraph
    ) const override;

    void registerPins(PinRegistry& registry) override;
    bool usesPinRegistry() const override { return usesRegistry_; }

    // Primitives (source node doesn't create GPU primitives directly)
    void clearPrimitives() override {}
    void createPrimitives(primitives::Store& store) override {}
    void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>& outputs
    ) const override {}

    // Output pin
    Pin modelSourcePin;
    PinHandle modelSourcePinHandle = INVALID_PIN_HANDLE;

    // Trigger rebuild (called by UI when models change)
    void rebuildConsolidatedData();

private:
    void createDefaultPins();

    std::vector<ModelEntry> models_;

    // Consolidated data (rebuilt when models change)
    std::vector<Vertex> consolidatedVertices_;
    std::vector<uint32_t> consolidatedIndices_;
    std::vector<EditorGeometryRange> consolidatedRanges_;
    std::vector<ConsolidatedRangeInfo> rangeInfo_;

    // Merged auxiliary data
    std::vector<EditorMaterial> mergedMaterials_;
    std::vector<EditorImage*> mergedImages_; // Pointers to CachedModel images
    std::vector<GLTFCamera> mergedCameras_;
    std::vector<GLTFLight> mergedLights_;

    // Texture index remapping per model (original index -> merged index)
    std::vector<std::unordered_map<int, int>> textureIndexRemap_;

    bool usesRegistry_ = false;
};
