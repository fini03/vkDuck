#pragma once
#include <imgui.h>
#include <filesystem>

class MultiModelSourceNode;
class MultiVertexDataNode;
class MultiUBONode;
class MultiMaterialNode;
class ShaderManager;
class NodeGraph;

/// UI helper for editing multi-model node settings
class MultiModelSettingsUI {
public:
    // Draw settings for source node (model list management)
    static void Draw(
        MultiModelSourceNode* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );

    // Draw settings for consumer nodes (connection status + node-specific)
    static void Draw(
        MultiVertexDataNode* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );
    static void Draw(
        MultiUBONode* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );
    static void Draw(
        MultiMaterialNode* node,
        ShaderManager* shaderManager = nullptr,
        NodeGraph* graph = nullptr
    );

private:
    // Model list management for source node
    static void DrawModelList(MultiModelSourceNode* node);

    // Connection status for consumer nodes
    static void DrawConnectionStatus(
        const char* nodeType,
        bool hasValidSource,
        const char* sourceName
    );

    // Specific UI for each consumer type
    static void DrawMultiVertexDataSettings(MultiVertexDataNode* node, NodeGraph* graph);
    static void DrawMultiUBOSettings(MultiUBONode* node, NodeGraph* graph);
    static void DrawMultiMaterialSettings(MultiMaterialNode* node, NodeGraph* graph);
};
