#pragma once
#include "../graph/link.h"
#include "external/utilities/builders.h"
#include "vulkan_editor/graph/node_graph.h"
#include <imgui_node_editor.h>
#include <memory>

using namespace ShaderTypes;

class ShaderManager;
class PipelineSettingsUI;
class PipelineNode;
class ModelNode;
class PresentNode;
class LightNode;
class CameraNodeBase;
class OrbitalCameraNode;
class FPSCameraNode;
class FixedCameraNode;
namespace ed = ax::NodeEditor;

/**
 * @class PipelineEditorUI
 * @brief Main node graph editor interface using ImGui Node Editor.
 *
 * Provides a two-pane layout with node settings on the left and the visual
 * graph on the right. Handles node creation, link validation, context menus,
 * and selection management.
 */
class PipelineEditorUI {
public:
    explicit PipelineEditorUI(NodeGraph& g);
    ~PipelineEditorUI();

    void draw(
        ShaderManager* shaderManager,
        PipelineSettingsUI* settingsUI
    );

    void DrawNode(
        Node* node,
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
    );

    void ClearSelection();

private:
    void DrawLeftPane(float paneWidth, ShaderManager* shaderManager, PipelineSettingsUI* settingsUI);
    void DrawPaneHeader(float paneWidth);
    void DrawNodeSettings(ShaderManager* shaderManager);
    void DrawGraph(ShaderManager* shaderManager);

    void DrawAllNodes(ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder);
    void DrawAllLinks();

    void HandleLinkCreation();
    void CreateNewLink(ax::NodeEditor::PinId startId, ax::NodeEditor::PinId endId);
    void ShowIncompatiblePinsTooltip();
    void HandleNewNodeCreation();
    void ShowCreateNodeLabel();

    void HandleDeletion();
    void DeleteLinks();
    void DeleteNodes();

    void HandleContextMenus(ShaderManager* shaderManager);
    void ShowContextMenuPopups(ShaderManager* shaderManager);

    void HandleSelection();
    void UpdateSelectedNode(ed::NodeId selectedNodeId);
    bool HasPresentNode() const;
    bool HasOrbitalOrFPSCamera() const;

    void ShowNodeContextMenu(ed::NodeId* nodeId);
    void ShowPinContextMenu(ed::PinId* pinId);
    void ShowLinkContextMenu(ed::LinkId* linkId);
    void ShowBackgroundContextMenu(ShaderManager* shaderManager);

    NodeGraph& graph;
    ed::EditorContext* context = nullptr;

    float leftPaneWidth = 400.0f;
    float rightPaneWidth = 800.0f;

    PipelineNode* selectedPipeline = nullptr;
    ModelNode* selectedModel = nullptr;
    PresentNode* selectedPresent = nullptr;
    CameraNodeBase* selectedCamera = nullptr;
    LightNode* selectedLight = nullptr;
    ed::NodeId contextNodeId = 0;
    ed::PinId contextPinId = 0;
    ed::LinkId contextLinkId = 0;
    bool createNewNode = false;
    ImVec2 newNodePosition = ImVec2(0, 0);

public:
    void SyncNodePositionsFromEditor();
    void ApplyNodePositionsToEditor();

    void* m_HeaderBackground = nullptr;
};
