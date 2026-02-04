// links.cpp
// Consolidated links management implementation
#include "link.h"
#include "../util/logger.h"
#include "../ui/attachment_editor_ui.h"
#include "node_graph.h"
#include "pipeline_node.h"
#include <algorithm>

// ============================================================================
// LinkValidator Implementation
// ============================================================================

AttachmentConfig* LinkValidator::FindAttachmentForPin(
    PipelineNode* node,
    ax::NodeEditor::PinId pinId,
    NodeGraph& graph
) {
    if (!node)
        return nullptr;

    // Check output pins
    auto result = graph.findPin(pinId);
    const Pin* pin = result.pin;
    if (pin) {
        return FindMatchingAttachment(node, pin->label);
    }

    return nullptr;
}

FormatCompatibilityInfo LinkValidator::CanInputAcceptFormat(
    PipelineNode* inputNode,
    ax::NodeEditor::PinId inputPinId,
    VkFormat outputFormat,
    NodeGraph& graph
) {
    FormatCompatibilityInfo result;

    if (!inputNode) {
        result.reason = "Invalid input node";
        return result;
    }

    auto pinLookup = graph.findPin(inputPinId);
    const Pin* pin = pinLookup.pin;
    if (!pin) {
        result.reason = "Pin not found";
        return result;
    }

    return CheckFormatCompatibility(pin, outputFormat);
}

const Pin* LinkValidator::FindPinInList(
    const std::vector<Pin>& pins,
    ax::NodeEditor::PinId pinId
) {
    for (const auto& pin : pins) {
        if (pin.id == pinId) {
            return &pin;
        }
    }
    return nullptr;
}

ShaderTypes::AttachmentConfig* LinkValidator::FindMatchingAttachment(
    PipelineNode* node,
    const std::string& pinLabel
) {
    auto it = std::find_if(
        node->shaderReflection.attachmentConfigs.begin(),
        node->shaderReflection.attachmentConfigs.end(),
        [&pinLabel](const ShaderTypes::AttachmentConfig& config) {
            return config.name == pinLabel;
        }
    );

    if (it != node->shaderReflection.attachmentConfigs.end()) {
        return &(*it);
    }

    return nullptr;
}

FormatCompatibilityInfo LinkValidator::CheckFormatCompatibility(
    const Pin* pin,
    VkFormat outputFormat
) {
    FormatCompatibilityInfo result;

    switch (pin->type) {
    case PinType::Image: {
        std::vector<VkFormat> imageFormats =
            AttachmentEditorUI::GetImageFormats();
        for (const auto& format : imageFormats) {
            if (format == outputFormat) {
                result.compatible = true;
                result.reason = "Exact image format match";
                return result;
            }
        }
        result.reason = "Mismatched image format for texture input";
        break;
    }
    case PinType::UniformBuffer:
        result.compatible = true;
        break;
    default:
        result.reason = "Matching is not defined for this pin type";
        break;
    }

    return result;
}

bool LinkValidator::CanCreateLink(
    NodeGraph& graph,
    ax::NodeEditor::PinId startId,
    ax::NodeEditor::PinId endId,
    bool logOnFailure
) {
    auto start = graph.findPin(startId);
    auto end = graph.findPin(endId);

    // Basic validation
    if (!ValidateBasicLinkRequirements(start, end)) {
        return false;
    }

    // Determine output and input pins
    PinLookupResult output, input;
    DetermineOutputInput(start, end, output, input);

    // Type compatibility check
    if (!CheckTypeCompatibility(graph, output, input, logOnFailure)) {
        return false;
    }

    // Input single-link constraint
    if (LinkManager::isPinLinked(graph.pinToLinks, input.pin->id)) {
        if (logOnFailure) {
            Log::warning(
                "Node Editor",
                "Cannot connect: input pin '{}' is already linked",
                input.pin->label
            );
        }
        return false;
    }

    return true;
}

const char* LinkValidator::GetPinTypeName(PinType type) {
    switch (type) {
    case PinType::UniformBuffer:
        return "Uniform Buffer";
    case PinType::Image:
        return "Image";
    case PinType::VertexData:
        return "Vertex data";
    case PinType::Camera:
        return "Camera";
    case PinType::Light:
        return "Light";
    case PinType::ModelCameras:
        return "Model Cameras";
    default:
        return "Unknown";
    }
}

bool LinkValidator::ArePinTypesCompatible(
    PinType outputType,
    PinType inputType
) {
    // Pin types must match exactly
    if (outputType == inputType) {
        return true;
    }

    // No cross-type connections allowed
    return false;
}

bool LinkValidator::ValidateBasicLinkRequirements(
    const PinLookupResult& start,
    const PinLookupResult& end
) {
    // Check if both pins exist
    if (!start.pin || !end.pin || !start.node || !end.node) {
        return false;
    }

    // Cannot link to self
    if (start.node->getId() == end.node->getId()) {
        return false;
    }

    // Must link Output to Input (different kinds)
    if (start.kind == end.kind) {
        return false;
    }

    return true;
}

void LinkValidator::DetermineOutputInput(
    const PinLookupResult& start,
    const PinLookupResult& end,
    PinLookupResult& output,
    PinLookupResult& input
) {
    if (start.kind == NodePinKind::Output) {
        output = start;
        input = end;
    } else {
        output = end;
        input = start;
    }
}

bool LinkValidator::CheckTypeCompatibility(
    NodeGraph& graph,
    const PinLookupResult& output,
    const PinLookupResult& input,
    bool logOnFailure
) {
    // First, check if pin types are compatible at a basic level
    if (!ArePinTypesCompatible(output.pin->type, input.pin->type)) {
        if (logOnFailure) {
            Log::error(
                "Node Editor",
                "Pin types incompatible: {} cannot connect to {}",
                GetPinTypeName(output.pin->type),
                GetPinTypeName(input.pin->type)
            );
        }
        return false;
    }

    PipelineNode* outputPipeline =
        dynamic_cast<PipelineNode*>(output.node);
    PipelineNode* inputPipeline =
        dynamic_cast<PipelineNode*>(input.node);

    if (!outputPipeline || !inputPipeline) {
        return true; // Allow if not pipeline nodes
    }

    // Check attachment format compatibility for image pins
    if (output.pin->type == PinType::Image) {
        return CheckAttachmentFormatCompatibility(
            graph, outputPipeline, inputPipeline, output, input, logOnFailure
        );
    }

    return true;
}

bool LinkValidator::CheckAttachmentFormatCompatibility(
    NodeGraph& graph,
    PipelineNode* outputPipeline,
    PipelineNode* inputPipeline,
    const PinLookupResult& output,
    const PinLookupResult& input,
    bool logOnFailure
) {
    AttachmentConfig* outputAttachment =
        FindAttachmentForPin(outputPipeline, output.pin->id, graph);

    if (!outputAttachment) {
        Log::debug(
            "Node Editor",
            "Could not find attachment config for output pin"
        );
        return true; // Allow for backwards compatibility
    }

    auto compatibility = CanInputAcceptFormat(
        inputPipeline, input.pin->id, outputAttachment->format, graph
    );

    if (!compatibility.compatible) {
        if (logOnFailure) {
            Log::error(
                "Node Editor",
                "Format incompatibility: {} (Output format: {})",
                compatibility.reason, outputAttachment->name
            );
        }
        return false;
    }

    Log::debug(
        "Node Editor", "Format compatibility OK: {}",
        compatibility.reason
    );
    return true;
}

// ============================================================================
// LinkManager Implementation
// ============================================================================

namespace LinkManager {

void addLink(
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks,
    const Link& link
) {
    links.push_back(link);
    pinToLinks[link.startPin].insert(link.id);
    pinToLinks[link.endPin].insert(link.id);
}

void removeLink(
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks,
    ax::NodeEditor::LinkId id
) {
    auto it = std::find_if(
        links.begin(), links.end(),
        [&](const Link& l) { return l.id == id; }
    );

    if (it != links.end()) {
        // Remove from index
        pinToLinks[it->startPin].erase(id);
        pinToLinks[it->endPin].erase(id);

        // Clean up empty sets
        if (pinToLinks[it->startPin].empty())
            pinToLinks.erase(it->startPin);
        if (pinToLinks[it->endPin].empty())
            pinToLinks.erase(it->endPin);

        // Remove link
        links.erase(it);
    }
}

void removeLinksForPin(
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks,
    ax::NodeEditor::PinId pinId
) {
    auto it = pinToLinks.find(pinId);
    if (it == pinToLinks.end())
        return;

    // Copy the set since we'll modify it during iteration
    auto linkIds = it->second;
    for (const auto& linkId : linkIds) {
        removeLink(links, pinToLinks, linkId);
    }
}

bool isPinLinked(
    const PinToLinksIndex& pinToLinks,
    ax::NodeEditor::PinId id
) {
    auto it = pinToLinks.find(id);
    return it != pinToLinks.end() && !it->second.empty();
}

void removeInvalidLinks(
    NodeGraph& graph,
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks
) {
    // Collect links to remove (where either pin no longer exists)
    std::vector<ax::NodeEditor::LinkId> toRemove;

    for (const auto& link : links) {
        auto startResult = graph.findPin(link.startPin);
        auto endResult = graph.findPin(link.endPin);

        if (!startResult.pin || !endResult.pin) {
            toRemove.push_back(link.id);
        }
    }

    // Remove collected links
    for (const auto& linkId : toRemove) {
        removeLink(links, pinToLinks, linkId);
    }
}

void clearLinks(
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks
) {
    links.clear();
    pinToLinks.clear();
}

} // namespace LinkManager
