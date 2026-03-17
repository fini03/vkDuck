#pragma once
#include <imgui.h>

class CameraNodeBase;
class OrbitalCameraNode;
class FPSCameraNode;
class FixedCameraNode;
class MultiUBONode;
class NodeGraph;

class CameraEditorUI {
public:
    // Draw UI for any camera type (dispatches to specific methods)
    static void Draw(
        CameraNodeBase* camera,
        NodeGraph* graph = nullptr,
        MultiUBONode* uboNode = nullptr
    );

private:
    // Specific UI for each camera type
    static void DrawOrbitalCamera(
        OrbitalCameraNode* camera,
        NodeGraph* graph,
        MultiUBONode* uboNode
    );
    static void DrawFPSCamera(
        FPSCameraNode* camera,
        NodeGraph* graph,
        MultiUBONode* uboNode
    );
    static void DrawFixedCamera(FixedCameraNode* camera);

    // Common projection UI (shared by all types)
    static bool DrawProjectionSettings(CameraNodeBase* camera);
    static void DrawDebugInfo(CameraNodeBase* camera);
};
