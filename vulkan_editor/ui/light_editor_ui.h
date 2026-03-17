#pragma once
#include <imgui.h>

class LightNode;
class MultiUBONode;
class NodeGraph;

class LightEditorUI {
public:
    static void Draw(LightNode* lights, MultiUBONode* uboNode = nullptr, NodeGraph* graph = nullptr);
};