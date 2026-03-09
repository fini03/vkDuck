#pragma once
#include <imgui.h>

class CameraNodeBase;
class OrbitalCameraNode;
class FPSCameraNode;
class FixedCameraNode;
class UBONode;
class NodeGraph;

class CameraEditorUI {
public:
    // Draw UI for any camera type (dispatches to specific methods)
    static void Draw(
        CameraNodeBase* camera,
        NodeGraph* graph = nullptr,
        UBONode* uboNode = nullptr
    );

private:
    // Specific UI for each camera type
    static void DrawOrbitalCamera(
        OrbitalCameraNode* camera,
        UBONode* uboNode
    );
    static void DrawFPSCamera(
        FPSCameraNode* camera,
        UBONode* uboNode
    );
    static void DrawFixedCamera(FixedCameraNode* camera);

    // Common projection UI (shared by all types)
    static bool DrawProjectionSettings(CameraNodeBase* camera);
    static void DrawDebugInfo(CameraNodeBase* camera);
};
