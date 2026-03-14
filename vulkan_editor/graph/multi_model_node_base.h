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
 * @brief Represents a single model in the multi-model node.
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
 * @class MultiModelNodeBase
 * @brief Abstract base class for multi-model nodes.
 *
 * Provides shared functionality for nodes that reference multiple GLTF models:
 * - Multiple ModelHandle management with reference counting
 * - Data consolidation (vertices, indices, materials, cameras, lights)
 * - Serialization of model paths array
 * - Common rendering helpers (orange header for model nodes)
 *
 * Derived classes:
 * - MultiVertexDataNode: combined geometry data
 * - MultiUBONode: combined matrices, cameras, lights
 * - MultiMaterialNode: combined PBR textures
 */
class MultiModelNodeBase : public Node, public ISerializable {
public:
    MultiModelNodeBase();
    explicit MultiModelNodeBase(int id);
    ~MultiModelNodeBase() override;

    // Model management
    void addModel(ModelHandle handle);
    void removeModel(size_t index);
    void setModelEnabled(size_t index, bool enabled);
    void reorderModel(size_t fromIndex, size_t toIndex);

    size_t getModelCount() const { return models_.size(); }
    const ModelEntry& getModel(size_t index) const { return models_[index]; }
    bool hasModels() const;

    // Base serialization (derived classes should call these)
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Consolidated data accessors
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

protected:
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

    // Called after models change - derived classes can override
    virtual void onModelsChanged() {}

    // Rebuild all consolidated data from current models
    void rebuildConsolidatedData();

    // Common rendering helper for orange multi-model node header
    void renderMultiModelNodeHeader(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        float nodeWidth
    ) const;

    // Calculate node width based on pin labels
    static float calculateMultiModelNodeWidth(
        const std::string& nodeName,
        const std::vector<std::string>& pinLabels
    );
};
