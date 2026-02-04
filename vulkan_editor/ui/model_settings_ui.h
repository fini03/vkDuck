#pragma once
#include <imgui.h>
#include <filesystem>

class ModelNode;
class ShaderManager;
class NodeGraph;

/// UI helper for editing model node settings
class ModelSettingsUI {
public:
    static void Draw(ModelNode* modelNode, ShaderManager* shaderManager, NodeGraph* graph = nullptr);
};