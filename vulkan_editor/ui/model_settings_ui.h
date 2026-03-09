#pragma once
#include <imgui.h>
#include <filesystem>

class ModelNodeBase;
class VertexDataNode;
class UBONode;
class MaterialNode;
class ShaderManager;
class NodeGraph;

/// UI helper for editing model node settings
class ModelSettingsUI {
public:
    // Draw settings for any model-derived node (dispatches based on type)
    static void Draw(
        ModelNodeBase* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );

private:
    // Common model info display (shared by all model node types)
    static void DrawModelInfo(ModelNodeBase* node);

    // Specific UI for each node type
    static void DrawVertexDataSettings(VertexDataNode* node);
    static void DrawUBOSettings(UBONode* node);
    static void DrawMaterialSettings(MaterialNode* node);
};
