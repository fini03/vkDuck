#pragma once
#include <imgui.h>

class CameraNodeBase;
class OrbitalCameraNode;
class FPSCameraNode;
class FixedCameraNode;
class ModelNode;
class NodeGraph;

class CameraEditorUI {
public:
    // Draw UI for any camera type (dispatches to specific methods)
    static void Draw(
        CameraNodeBase* camera,
        NodeGraph* graph = nullptr,
        ModelNode* modelNode = nullptr
    );

private:
    // Specific UI for each camera type
    static void DrawOrbitalCamera(
        OrbitalCameraNode* camera,
        ModelNode* modelNode
    );
    static void DrawFPSCamera(
        FPSCameraNode* camera,
        ModelNode* modelNode
    );
    static void DrawFixedCamera(FixedCameraNode* camera);

    // Common projection UI (shared by all types)
    static bool DrawProjectionSettings(CameraNodeBase* camera);
    static void DrawDebugInfo(CameraNodeBase* camera);
};
