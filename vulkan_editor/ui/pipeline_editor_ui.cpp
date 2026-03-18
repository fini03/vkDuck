#define IMGUI_DEFINE_MATH_OPERATORS
#include "pipeline_editor_ui.h"
#include "attachment_editor_ui.h"
#include "camera_editor_ui.h"
#include "light_editor_ui.h"
// model_settings_ui.h deprecated - using multi_model_settings_ui
#include "multi_model_settings_ui.h"
#include "pipeline_settings_ui.h"
#include "vulkan_editor/asset/model_manager.h"
#include "vulkan_editor/util/logger.h"
#include "vulkan_editor/graph/camera_node.h"
#include "vulkan_editor/graph/fixed_camera_node.h"
#include "vulkan_editor/graph/fps_camera_node.h"
#include "vulkan_editor/graph/light_node.h"
// Singular model nodes deprecated - moved to graph/deprecated/
#include "vulkan_editor/graph/pipeline_node.h"
#include "vulkan_editor/graph/present_node.h"
#include "vulkan_editor/graph/multi_model_source_node.h"
#include "vulkan_editor/graph/multi_vertex_data_node.h"
#include "vulkan_editor/graph/multi_ubo_node.h"
#include "vulkan_editor/graph/multi_material_node.h"
#include "vulkan_editor/shader/shader_manager.h"

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include <imgui.h>
#include <imgui_internal.h>

// ============================================================================
// Constants
// ============================================================================
namespace {
constexpr float SPLITTER_THICKNESS = 4.0f;
constexpr float MIN_PANE_SIZE = 50.0f;
constexpr float LINK_THICKNESS = 2.0f;
} // namespace

// ============================================================================
// UI Splitter Helper
// ============================================================================
static bool Splitter(
    bool splitVertically,
    float thickness,
    float* size1,
    float* size2,
    float minSize1,
    float minSize2,
    float splitterLongAxisSize = -1.0f
) {
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");

    ImRect bb;
    bb.Min =
        window->DC.CursorPos +
        (splitVertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(
                          splitVertically
                              ? ImVec2(thickness, splitterLongAxisSize)
                              : ImVec2(splitterLongAxisSize, thickness),
                          0.0f, 0.0f
                      );

    return SplitterBehavior(
        bb, id, splitVertically ? ImGuiAxis_X : ImGuiAxis_Y, size1,
        size2, minSize1, minSize2, 0.0f
    );
}

// ============================================================================
// Constructor / Destructor
// ============================================================================
PipelineEditorUI::PipelineEditorUI(NodeGraph& g)
    : graph(g) {
    ed::Config config;
    config.SettingsFile =
        nullptr; // TODO: Fix loading pipeline editor settings
    context = ed::CreateEditor(&config);
}

PipelineEditorUI::~PipelineEditorUI() {
    ed::DestroyEditor(context);
}

// ============================================================================
// Main Draw Function
// ============================================================================
void PipelineEditorUI::draw(
    ShaderManager* shaderManager,
    PipelineSettingsUI* settingsUI
) {
    float availableHeight = ImGui::GetContentRegionAvail().y;

    Splitter(
        true, SPLITTER_THICKNESS, &leftPaneWidth, &rightPaneWidth,
        MIN_PANE_SIZE, MIN_PANE_SIZE, availableHeight
    );

    DrawLeftPane(
        leftPaneWidth - SPLITTER_THICKNESS, shaderManager, settingsUI
    );
    ImGui::SameLine(0.0f, SPLITTER_THICKNESS);
    DrawGraph(shaderManager);
}

// ============================================================================
// Left Pane (Settings Panel)
// ============================================================================
void PipelineEditorUI::DrawLeftPane(
    float paneWidth,
    ShaderManager* shaderManager,
    PipelineSettingsUI* settingsUI
) {
    ImGui::BeginChild("Selection", ImVec2(paneWidth, 0));
    paneWidth = ImGui::GetContentRegionAvail().x;

    DrawPaneHeader(paneWidth);
    DrawNodeSettings(shaderManager);

    ImGui::EndChild();
}

void PipelineEditorUI::DrawPaneHeader(float paneWidth) {
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImGui::GetCursorScreenPos() +
            ImVec2(paneWidth, ImGui::GetTextLineHeight()),
        ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]),
        ImGui::GetTextLineHeight() * 0.25f
    );

    ImGui::Spacing();
    ImGui::SameLine();

    // Dynamic header based on selection
    if (selectedPipeline) {
        ImGui::TextUnformatted("Pipeline Settings");
    } else if (selectedMultiModelSource || selectedMultiVertexData || selectedMultiUBO || selectedMultiMaterial) {
        ImGui::TextUnformatted("Model Settings");
    } else {
        ImGui::TextUnformatted("Node Settings");
    }

    ImGui::Separator();
}

void PipelineEditorUI::DrawNodeSettings(ShaderManager* shaderManager) {
    if (selectedPipeline) {
        PipelineSettingsUI::Draw(
            selectedPipeline, graph, shaderManager
        );
    } else if (selectedCamera) {
        // Find first MultiUBONode with GLTF cameras for initialization
        MultiUBONode* uboWithCameras = nullptr;
        for (const auto& node : graph.nodes) {
            if (auto* ubo = dynamic_cast<MultiUBONode*>(node.get())) {
                // Multi UBO gets cameras from source node
                if (auto* source = ubo->findSourceNode(graph)) {
                    if (!source->getMergedCameras().empty()) {
                        uboWithCameras = ubo;
                        break;
                    }
                }
            }
        }
        CameraEditorUI::Draw(selectedCamera, &graph, uboWithCameras);
    } else if (selectedLight) {
        // Find first MultiUBONode with GLTF lights for initialization
        MultiUBONode* uboWithLights = nullptr;
        for (const auto& node : graph.nodes) {
            if (auto* ubo = dynamic_cast<MultiUBONode*>(node.get())) {
                // Multi UBO gets lights from source node
                if (auto* source = ubo->findSourceNode(graph)) {
                    if (!source->getMergedLights().empty()) {
                        uboWithLights = ubo;
                        break;
                    }
                }
            }
        }
        LightEditorUI::Draw(selectedLight, uboWithLights, &graph);
    } else if (selectedPresent) {
        ImGui::TextWrapped("Present Node - displays final output");
    } else if (selectedMultiModelSource) {
        MultiModelSettingsUI::Draw(selectedMultiModelSource, shaderManager, &graph);
    } else if (selectedMultiVertexData) {
        MultiModelSettingsUI::Draw(selectedMultiVertexData, shaderManager, &graph);
    } else if (selectedMultiUBO) {
        MultiModelSettingsUI::Draw(selectedMultiUBO, shaderManager, &graph);
    } else if (selectedMultiMaterial) {
        MultiModelSettingsUI::Draw(selectedMultiMaterial, shaderManager, &graph);
    } else {
        ImGui::TextWrapped(
            "Select a node in the graph to view properties."
        );
    }
}

// ============================================================================
// Node Drawing
// ============================================================================
void PipelineEditorUI::DrawNode(
    Node* node,
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
) {
    // Simply delegate to the node's render method
    node->render(builder, graph);
}

void PipelineEditorUI::DrawAllNodes(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder
) {
    for (const auto& node : graph.nodes) {
        // Each node now handles its own rendering
        node->render(builder, graph);
    }
}

// ============================================================================
// Graph Drawing
// ============================================================================
void PipelineEditorUI::DrawGraph(ShaderManager* shaderManager) {
    ed::SetCurrentEditor(context);
    ed::Begin("Node editor", ImVec2(0, 0));

    auto cursorTopLeft = ImGui::GetCursorScreenPos();
    ax::NodeEditor::Utilities::BlueprintNodeBuilder builder(
        reinterpret_cast<ImTextureID>(m_HeaderBackground),
        m_HeaderBackground ? 256 : 0, m_HeaderBackground ? 256 : 0
    );

    DrawAllNodes(builder);
    DrawAllLinks();
    HandleLinkCreation();
    HandleDeletion();
    HandleContextMenus(shaderManager);
    HandleSelection();

    ImGui::SetCursorScreenPos(cursorTopLeft);
    ed::End();
}

void PipelineEditorUI::DrawAllLinks() {
    for (const auto& link : graph.links) {
        // Bright red links to match output pins
        ed::Link(
            link.id, link.startPin, link.endPin, ImColor(255, 255, 255),
            LINK_THICKNESS
        );
    }
}

void PipelineEditorUI::HandleLinkCreation() {
    // Bright red for link creation preview
    if (!ed::BeginCreate(ImColor(255, 255, 255), LINK_THICKNESS)) {
        ed::EndCreate();
        return;
    }

    ed::PinId startId = 0, endId = 0;
    if (ed::QueryNewLink(&startId, &endId)) {
        auto result = LinkValidator::canCreate(graph, startId, endId);
        if (result) {
            // Normalize to output→input direction using PinPair
            auto pins = PinPair::create(graph, startId, endId);
            if (pins) {
                startId = pins->output.pin->id;
                endId = pins->input.pin->id;
            }

            if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                CreateNewLink(startId, endId);
        } else {
            ed::RejectNewItem(ImColor(255, 128, 128), LINK_THICKNESS);
            ShowIncompatiblePinsTooltip();
            // Log failure reason on mouse release
            if (ImGui::IsMouseReleased(0) && !result.reason.empty()) {
                Log::warning("Node Editor", "{}", result.reason);
            }
        }
    }

    HandleNewNodeCreation();
    ed::EndCreate();
}

void PipelineEditorUI::CreateNewLink(
    ed::PinId startId,
    ed::PinId endId
) {
    Link link;
    link.id = Node::GetNextGlobalId();
    link.startPin = startId;
    link.endPin = endId;
    graph.addLink(link);
    // Light count is user-controlled - shader uses numLights header for dynamic sizing
}

void PipelineEditorUI::ShowIncompatiblePinsTooltip() {
    ImGui::BeginTooltip();
    ImGui::TextColored(
        ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Incompatible Pins"
    );
    ImGui::EndTooltip();
}

void PipelineEditorUI::HandleNewNodeCreation() {
    ed::PinId pinId;
    if (ed::QueryNewNode(&pinId)) {
        ShowCreateNodeLabel();
        if (ed::AcceptNewItem()) {
            createNewNode = true;
            ed::Suspend();
            ImGui::OpenPopup("Create New Node");
            ed::Resume();
        }
    }
}

void PipelineEditorUI::ShowCreateNodeLabel() {
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() - ImGui::GetTextLineHeight()
    );
    auto size = ImGui::CalcTextSize("+ Create Node");
    auto padding = ImGui::GetStyle().FramePadding;
    auto spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::SetCursorPos(
        ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y)
    );

    auto rectMin = ImGui::GetCursorScreenPos() - padding;
    auto rectMax = ImGui::GetCursorScreenPos() + size + padding;
    auto drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        rectMin, rectMax, ImColor(32, 45, 32, 180), size.y * 0.15f
    );

    ImGui::TextUnformatted("+ Create Node");
}

void PipelineEditorUI::HandleDeletion() {
    if (!ed::BeginDelete()) {
        ed::EndDelete();
        return;
    }

    DeleteLinks();
    DeleteNodes();

    ed::EndDelete();
}

void PipelineEditorUI::DeleteLinks() {
    ed::LinkId linkId = 0;
    while (ed::QueryDeletedLink(&linkId)) {
        if (ed::AcceptDeletedItem()) {
            graph.removeLink(linkId);
        }
    }
}

void PipelineEditorUI::DeleteNodes() {
    ed::NodeId nodeId = 0;
    while (ed::QueryDeletedNode(&nodeId)) {
        if (ed::AcceptDeletedItem()) {
            uint64_t deletedId = nodeId.Get();

            // Check and nullify if the deleted node was selected
            if (selectedPipeline &&
                static_cast<uint64_t>(selectedPipeline->getId()) ==
                    deletedId) {
                selectedPipeline = nullptr;
            }
            if (selectedPresent &&
                static_cast<uint64_t>(selectedPresent->getId()) ==
                    deletedId) {
                selectedPresent = nullptr;
            }
            if (selectedCamera &&
                static_cast<uint64_t>(selectedCamera->getId()) ==
                    deletedId) {
                selectedCamera = nullptr;
            }
            if (selectedLight &&
                static_cast<uint64_t>(selectedLight->getId()) ==
                    deletedId) {
                selectedLight = nullptr;
            }
            if (selectedMultiModelSource &&
                static_cast<uint64_t>(selectedMultiModelSource->getId()) ==
                    deletedId) {
                selectedMultiModelSource = nullptr;
            }
            if (selectedMultiVertexData &&
                static_cast<uint64_t>(selectedMultiVertexData->getId()) ==
                    deletedId) {
                selectedMultiVertexData = nullptr;
            }
            if (selectedMultiUBO &&
                static_cast<uint64_t>(selectedMultiUBO->getId()) ==
                    deletedId) {
                selectedMultiUBO = nullptr;
            }
            if (selectedMultiMaterial &&
                static_cast<uint64_t>(selectedMultiMaterial->getId()) ==
                    deletedId) {
                selectedMultiMaterial = nullptr;
            }

            graph.removeNode(nodeId);
        }
    }
}

bool PipelineEditorUI::HasPresentNode() const {
    for (const auto& node : graph.nodes) {
        if (dynamic_cast<PresentNode*>(node.get())) {
            return true;
        }
    }
    return false;
}

bool PipelineEditorUI::HasOrbitalOrFPSCamera() const {
    for (const auto& node : graph.nodes) {
        if (dynamic_cast<OrbitalCameraNode*>(node.get()) ||
            dynamic_cast<FPSCameraNode*>(node.get())) {
            return true;
        }
    }
    return false;
}

void PipelineEditorUI::HandleContextMenus(
    ShaderManager* shaderManager
) {
    ed::Suspend();

    if (ed::ShowNodeContextMenu(&contextNodeId)) {
        ImGui::OpenPopup("Node Context Menu");
    } else if (ed::ShowPinContextMenu(&contextPinId)) {
        ImGui::OpenPopup("Pin Context Menu");
    } else if (ed::ShowLinkContextMenu(&contextLinkId)) {
        ImGui::OpenPopup("Link Context Menu");
    } else if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("Create New Node");
        // Capture cursor position in canvas space for new node placement
        newNodePosition = ed::ScreenToCanvas(ImGui::GetMousePos());
    }

    ShowContextMenuPopups(shaderManager);

    ed::Resume();
}

void PipelineEditorUI::ShowContextMenuPopups(
    ShaderManager* shaderManager
) {
    if (ImGui::BeginPopup("Create New Node")) {
        ShowBackgroundContextMenu(shaderManager);
        ImGui::EndPopup();
    } else {
        createNewNode = false;
    }

    if (ImGui::BeginPopup("Node Context Menu")) {
        if (ImGui::MenuItem("Delete")) {
            ed::DeleteNode(contextNodeId);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu")) {
        if (ImGui::MenuItem("Delete")) {
            ed::DeleteLink(contextLinkId);
        }
        ImGui::EndPopup();
    }
}

void PipelineEditorUI::HandleSelection() {
    if (ed::GetSelectedObjectCount() == 0) {
        return;
    }

    std::vector<ed::NodeId> selectedNodes(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(
        selectedNodes.data(), static_cast<int>(selectedNodes.size())
    );

    if (nodeCount > 0) {
        UpdateSelectedNode(selectedNodes[0]);
    } else {
        ClearSelection();
    }
}

void PipelineEditorUI::UpdateSelectedNode(ed::NodeId selectedNodeId) {
    for (const auto& node : graph.nodes) {
        if (static_cast<uint64_t>(node->getId()) ==
            selectedNodeId.Get()) {
            selectedPipeline = dynamic_cast<PipelineNode*>(node.get());
            selectedMultiModelSource = dynamic_cast<MultiModelSourceNode*>(node.get());
            selectedMultiVertexData = dynamic_cast<MultiVertexDataNode*>(node.get());
            selectedMultiUBO = dynamic_cast<MultiUBONode*>(node.get());
            selectedMultiMaterial = dynamic_cast<MultiMaterialNode*>(node.get());
            selectedCamera = dynamic_cast<CameraNodeBase*>(node.get());
            selectedLight = dynamic_cast<LightNode*>(node.get());
            selectedPresent = dynamic_cast<PresentNode*>(node.get());
            return;
        }
    }
}

void PipelineEditorUI::ClearSelection() {
    selectedPipeline = nullptr;
    selectedMultiModelSource = nullptr;
    selectedMultiVertexData = nullptr;
    selectedMultiUBO = nullptr;
    selectedMultiMaterial = nullptr;
    selectedCamera = nullptr;
    selectedLight = nullptr;
    selectedPresent = nullptr;
}

// ============================================================================
// Get All Model Nodes
// ============================================================================
std::vector<MultiVertexDataNode*> PipelineEditorUI::getAllMultiVertexDataNodes() const {
    std::vector<MultiVertexDataNode*> result;
    for (const auto& node : graph.nodes) {
        if (auto* multiVertex = dynamic_cast<MultiVertexDataNode*>(node.get())) {
            result.push_back(multiVertex);
        }
    }
    return result;
}

std::vector<MultiUBONode*> PipelineEditorUI::getAllMultiUBONodes() const {
    std::vector<MultiUBONode*> result;
    for (const auto& node : graph.nodes) {
        if (auto* multiUbo = dynamic_cast<MultiUBONode*>(node.get())) {
            result.push_back(multiUbo);
        }
    }
    return result;
}

std::vector<MultiMaterialNode*> PipelineEditorUI::getAllMultiMaterialNodes() const {
    std::vector<MultiMaterialNode*> result;
    for (const auto& node : graph.nodes) {
        if (auto* multiMaterial = dynamic_cast<MultiMaterialNode*>(node.get())) {
            result.push_back(multiMaterial);
        }
    }
    return result;
}

std::vector<MultiModelSourceNode*> PipelineEditorUI::getAllMultiModelSourceNodes() const {
    std::vector<MultiModelSourceNode*> result;
    for (const auto& node : graph.nodes) {
        if (auto* source = dynamic_cast<MultiModelSourceNode*>(node.get())) {
            result.push_back(source);
        }
    }
    return result;
}

// ============================================================================
// Context Menu
// ============================================================================
void PipelineEditorUI::ShowBackgroundContextMenu(
    ShaderManager* shaderManager
) {
    ImGui::Text("Create Node");
    ImGui::Separator();

    // Helper lambda to set position on newly created node
    auto setNodePosition = [this](Node* node) {
        node->position = newNodePosition;
        ed::SetNodePosition(ed::NodeId(node->getId()), newNodePosition);
    };

    // Camera submenu
    if (ImGui::BeginMenu("Camera")) {
        bool hasMovableCamera = HasOrbitalOrFPSCamera();

        // Orbital and FPS are mutually exclusive - only one allowed
        if (ImGui::MenuItem("Orbital Camera", nullptr, false, !hasMovableCamera)) {
            auto camera = std::make_unique<OrbitalCameraNode>();
            auto* nodePtr = camera.get();
            graph.addNode(std::move(camera));
            setNodePosition(nodePtr);
        }
        if (hasMovableCamera && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Only one Orbital or FPS Camera allowed per graph.");
        }

        if (ImGui::MenuItem("FPS Camera", nullptr, false, !hasMovableCamera)) {
            auto camera = std::make_unique<FPSCameraNode>();
            auto* nodePtr = camera.get();
            graph.addNode(std::move(camera));
            setNodePosition(nodePtr);
        }
        if (hasMovableCamera && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Only one Orbital or FPS Camera allowed per graph.");
        }

        // Fixed camera is always allowed (separate from movable cameras)
        if (ImGui::MenuItem("Fixed Camera")) {
            auto camera = std::make_unique<FixedCameraNode>();
            auto* nodePtr = camera.get();
            graph.addNode(std::move(camera));
            setNodePosition(nodePtr);
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Light Node")) {
        auto light = std::make_unique<LightNode>();
        auto* nodePtr = light.get();
        graph.addNode(std::move(light));
        setNodePosition(nodePtr);
    }

    if (ImGui::MenuItem("Pipeline Node")) {
        auto pipeline = std::make_unique<PipelineNode>();
        auto* nodePtr = pipeline.get();
        graph.addNode(std::move(pipeline));
        setNodePosition(nodePtr);
    }

    // Model submenu for nodes that can load multiple GLTF files
    if (ImGui::BeginMenu("Model")) {
        if (ImGui::MenuItem("Model Source")) {
            auto node = std::make_unique<MultiModelSourceNode>();
            auto* nodePtr = node.get();
            graph.addNode(std::move(node));
            setNodePosition(nodePtr);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Vertex Data")) {
            auto node = std::make_unique<MultiVertexDataNode>();
            auto* nodePtr = node.get();
            nodePtr->setGraph(&graph);
            graph.addNode(std::move(node));
            setNodePosition(nodePtr);
        }

        if (ImGui::MenuItem("UBO")) {
            auto node = std::make_unique<MultiUBONode>();
            auto* nodePtr = node.get();
            nodePtr->setGraph(&graph);
            graph.addNode(std::move(node));
            setNodePosition(nodePtr);
        }

        if (ImGui::MenuItem("Material")) {
            auto node = std::make_unique<MultiMaterialNode>();
            auto* nodePtr = node.get();
            nodePtr->setGraph(&graph);
            graph.addNode(std::move(node));
            setNodePosition(nodePtr);
        }

        ImGui::EndMenu();
    }

    bool presentExists = HasPresentNode();

    if (ImGui::MenuItem(
            "Present Node", nullptr, false, !presentExists
        )) {
        auto present = std::make_unique<PresentNode>();
        auto* nodePtr = present.get();
        graph.addNode(std::move(present));
        setNodePosition(nodePtr);
    }

    if (presentExists &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Only one Present Node is allowed per graph."
        );
    }
}

// ============================================================================
// Position Synchronization
// ============================================================================

void PipelineEditorUI::SyncNodePositionsFromEditor() {
    // Sync positions from imgui-node-editor to Node::position before saving
    for (auto& node : graph.nodes) {
        node->position = ed::GetNodePosition(ed::NodeId(node->getId()));
    }
}

void PipelineEditorUI::ApplyNodePositionsToEditor() {
    // Apply positions from Node::position to imgui-node-editor after loading
    for (auto& node : graph.nodes) {
        ed::SetNodePosition(ed::NodeId(node->getId()), node->position);
    }
}
