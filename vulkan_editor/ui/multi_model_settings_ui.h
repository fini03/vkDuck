#pragma once
#include <imgui.h>
#include <filesystem>

class MultiModelNodeBase;
class MultiVertexDataNode;
class MultiUBONode;
class MultiMaterialNode;
class ShaderManager;
class NodeGraph;

/// UI helper for editing multi-model node settings
class MultiModelSettingsUI {
public:
    // Draw settings for any multi-model node (dispatches based on type)
    static void Draw(
        MultiModelNodeBase* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );

private:
    // Common model list management (shared by all multi-model node types)
    static void DrawModelList(MultiModelNodeBase* node);

    // Specific UI for each node type
    static void DrawMultiVertexDataSettings(MultiVertexDataNode* node);
    static void DrawMultiUBOSettings(MultiUBONode* node);
    static void DrawMultiMaterialSettings(MultiMaterialNode* node);
};
