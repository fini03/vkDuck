#pragma once
#include "../asset/model_manager.h"
#include "model_types.h"
#include "node.h"
#include "pin_registry.h"
#include "vulkan_editor/io/serialization.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

using namespace ShaderTypes;

namespace ax::NodeEditor::Utilities {
struct BlueprintNodeBuilder;
}

/**
 * @class ModelNodeBase
 * @brief Abstract base class for model-related nodes.
 *
 * Provides shared functionality:
 * - ModelHandle management (reference counting)
 * - setModel() from Asset Library
 * - Serialization of model path and handle
 * - Common rendering helpers (orange header for model nodes)
 *
 * Derived classes:
 * - VertexDataNode: geometry data
 * - UBONode: model matrices, camera, lights
 * - MaterialNode: PBR textures
 */
class ModelNodeBase : public Node, public ISerializable {
public:
    ModelNodeBase();
    explicit ModelNodeBase(int id);
    ~ModelNodeBase() override;

    // Model management
    void setModel(ModelHandle handle);
    ModelHandle getModelHandle() const { return modelHandle_; }
    bool hasModel() const;
    const CachedModel* getCachedModel() const;

    // Base serialization (derived classes should call these)
    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Settings path (for serialization)
    char modelPath[256] = "";

protected:
    ModelHandle modelHandle_;

    // Called after model is set - derived classes can override
    virtual void onModelSet() {}

    // Common rendering helper for orange model node header
    void renderModelNodeHeader(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        float nodeWidth
    ) const;

    // Calculate node width based on pin labels
    static float calculateModelNodeWidth(
        const std::string& nodeName,
        const std::vector<std::string>& pinLabels
    );
};
