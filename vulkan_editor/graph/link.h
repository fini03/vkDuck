#pragma once

#include "../shader/shader_types.h"
#include <algorithm>
#include <functional>
#include <imgui_node_editor.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

using namespace ShaderTypes;
namespace ed = ax::NodeEditor;

class Node;
class NodeGraph;
class PipelineNode;

struct Link {
    ax::NodeEditor::LinkId id;
    ax::NodeEditor::PinId startPin;
    ax::NodeEditor::PinId endPin;
};

enum class NodePinKind { Input, Output, None };

struct PinLookupResult {
    Node* node = nullptr;
    Pin* pin = nullptr;
    NodePinKind kind = NodePinKind::None;
};

namespace std {
template <>
struct hash<ax::NodeEditor::PinId> {
    std::size_t
    operator()(const ax::NodeEditor::PinId& id) const noexcept {
        return std::hash<void*>()(
            reinterpret_cast<void*>(id.AsPointer())
        );
    }
};

template <>
struct equal_to<ax::NodeEditor::PinId> {
    bool operator()(
        const ax::NodeEditor::PinId& lhs,
        const ax::NodeEditor::PinId& rhs
    ) const noexcept {
        return lhs == rhs;
    }
};

template <>
struct hash<ax::NodeEditor::LinkId> {
    std::size_t
    operator()(const ax::NodeEditor::LinkId& id) const noexcept {
        return std::hash<void*>()(
            reinterpret_cast<void*>(id.AsPointer())
        );
    }
};

template <>
struct equal_to<ax::NodeEditor::LinkId> {
    bool operator()(
        const ax::NodeEditor::LinkId& lhs,
        const ax::NodeEditor::LinkId& rhs
    ) const noexcept {
        return lhs == rhs;
    }
};
}

using PinToLinksIndex = std::unordered_map<
    ax::NodeEditor::PinId,
    std::unordered_set<ax::NodeEditor::LinkId>>;

struct FormatCompatibilityInfo {
    bool compatible = false;
    std::string reason;
};

/**
 * @class LinkValidator
 * @brief Validates pin compatibility and format constraints for node connections.
 */
class LinkValidator {
public:
    static AttachmentConfig* FindAttachmentForPin(
        PipelineNode* node, ax::NodeEditor::PinId pinId, NodeGraph& graph
    );
    static FormatCompatibilityInfo CanInputAcceptFormat(
        PipelineNode* inputNode, ax::NodeEditor::PinId inputPinId,
        VkFormat outputFormat, NodeGraph& graph
    );
    static bool CanCreateLink(
        NodeGraph& graph, ax::NodeEditor::PinId startId,
        ax::NodeEditor::PinId endId, bool logOnFailure = false
    );
    static const char* GetPinTypeName(PinType type);
    static bool ArePinTypesCompatible(PinType outputType, PinType inputType);

private:
    static const Pin* FindPinInList(
        const std::vector<Pin>& pins,
        ax::NodeEditor::PinId pinId
    );

    static ShaderTypes::AttachmentConfig* FindMatchingAttachment(
        PipelineNode* node,
        const std::string& pinLabel
    );

    static FormatCompatibilityInfo CheckFormatCompatibility(
        const Pin* pin,
        VkFormat outputFormat
    );

    static bool ValidateBasicLinkRequirements(
        const PinLookupResult& start,
        const PinLookupResult& end
    );

    static void DetermineOutputInput(
        const PinLookupResult& start,
        const PinLookupResult& end,
        PinLookupResult& output,
        PinLookupResult& input
    );

    static bool CheckTypeCompatibility(
        NodeGraph& graph,
        const PinLookupResult& output,
        const PinLookupResult& input,
        bool logOnFailure
    );

    static bool CheckAttachmentFormatCompatibility(
        NodeGraph& graph,
        PipelineNode* outputPipeline,
        PipelineNode* inputPipeline,
        const PinLookupResult& output,
        const PinLookupResult& input,
        bool logOnFailure
    );
};

namespace LinkManager {
void addLink(std::vector<Link>& links, PinToLinksIndex& pinToLinks, const Link& link);
void removeLink(std::vector<Link>& links, PinToLinksIndex& pinToLinks, ax::NodeEditor::LinkId id);
void removeLinksForPin(std::vector<Link>& links, PinToLinksIndex& pinToLinks, ax::NodeEditor::PinId pinId);
bool isPinLinked(const PinToLinksIndex& pinToLinks, ax::NodeEditor::PinId id);
void removeInvalidLinks(NodeGraph& graph, std::vector<Link>& links, PinToLinksIndex& pinToLinks);
void clearLinks(std::vector<Link>& links, PinToLinksIndex& pinToLinks);
}
