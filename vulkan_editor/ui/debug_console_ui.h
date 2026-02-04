#pragma once

#include <imgui.h>
#include <string>

class DebugConsoleUI {
public:
    static void Draw();

private:
    static bool showDebug;
    static bool showInfo;
    static bool showWarning;
    static bool showError;
    static bool autoScroll;
    static char searchFilter[256];
    static int selectedCategory; // 0 = All
};
