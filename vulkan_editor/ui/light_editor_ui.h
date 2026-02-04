#pragma once
#include <imgui.h>

class LightNode;

class LightEditorUI {
public:
    static void Draw(LightNode* lights);
};