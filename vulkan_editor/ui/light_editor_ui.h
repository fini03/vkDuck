#pragma once
#include <imgui.h>

class LightNode;
class UBONode;

class LightEditorUI {
public:
    static void Draw(LightNode* lights, UBONode* uboNode = nullptr);
};