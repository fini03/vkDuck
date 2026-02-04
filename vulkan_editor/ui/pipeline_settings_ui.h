#pragma once
#include <imgui.h>
#include <iostream>
#include <memory>
#include <vector>

class PipelineNode;
class ShaderManager;
class Node;
class NodeGraph;

/// UI helper for editing Vulkan pipeline settings (depth, rasterizer,
/// blending, etc.)
class PipelineSettingsUI {
private:
public:
    static void Draw(
        PipelineNode* selectedPipelineNode,
        NodeGraph& graph,
        ShaderManager* shader_manager
    );

    static void DrawCameraLightSettings(
        PipelineNode* pipeline,
        NodeGraph& graph
    );
};
