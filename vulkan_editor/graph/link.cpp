// link.cpp
// Consolidated link validation and management
//
// Note: NodeGraph::validateLink()/canCreateLink() use the new validation chain
// for pins in the registry. LinkValidator provides fallback validation for
// legacy pins not yet migrated to the registry.

#include "link.h"
#include "../util/logger.h"
#include "node_graph.h"
#include "pipeline_node.h"
#include "validation_rules.h"  // For GetAllowedImageFormats()
#include <algorithm>

// ============================================================================
// Internal Helpers (for legacy validation fallback)
// ============================================================================

namespace {

// Find attachment config by pin label
AttachmentConfig* findAttachment(PipelineNode* node, const std::string& label) {
    if (!node)
        return nullptr;

    auto& configs = node->shaderReflection.attachmentConfigs;
    auto it = std::find_if(configs.begin(), configs.end(),
        [&label](const AttachmentConfig& c) { return c.name == label; });

    return (it != configs.end()) ? &(*it) : nullptr;
}

// Check if output format is acceptable for an image input pin
ValidationResult checkImageFormatCompatibility(
    const Pin* inputPin,
    VkFormat outputFormat
) {
    if (inputPin->type != PinType::Image) {
        return ValidationResult::Ok();  // Not an image pin, skip format check
    }

    // Check against allowed image formats (from validation_rules.cpp)
    for (VkFormat allowed : GetAllowedImageFormats()) {
        if (allowed == outputFormat) {
            return ValidationResult::Ok();
        }
    }

    return ValidationResult::Fail("Image format incompatible");
}

// Validate format compatibility between two pipeline nodes
ValidationResult checkPipelineFormatCompatibility(
    NodeGraph& graph,
    const PinPair& pins
) {
    auto* outputNode = dynamic_cast<PipelineNode*>(pins.output.node);
    auto* inputNode = dynamic_cast<PipelineNode*>(pins.input.node);

    // Format check only applies between pipeline nodes
    if (!outputNode || !inputNode) {
        return ValidationResult::Ok();
    }

    // Only check format for image pins
    if (pins.output.pin->type != PinType::Image) {
        return ValidationResult::Ok();
    }

    // Find the output attachment to get its format
    auto* attachment = findAttachment(outputNode, pins.output.pin->label);
    if (!attachment) {
        // No attachment config found - allow for backwards compatibility
        return ValidationResult::Ok();
    }

    return checkImageFormatCompatibility(pins.input.pin, attachment->format);
}

}  // anonymous namespace

// ============================================================================
// PinPair Implementation
// ============================================================================

std::optional<PinPair> PinPair::create(
    NodeGraph& graph,
    ed::PinId a,
    ed::PinId b
) {
    auto pinA = graph.findPin(a);
    auto pinB = graph.findPin(b);

    // Both pins must exist
    if (!pinA.pin || !pinB.pin || !pinA.node || !pinB.node) {
        return std::nullopt;
    }

    // Cannot link to same node
    if (pinA.node->getId() == pinB.node->getId()) {
        return std::nullopt;
    }

    // Must be output→input (different kinds)
    if (pinA.kind == pinB.kind) {
        return std::nullopt;
    }

    // Normalize to output→input
    PinPair result;
    if (pinA.kind == NodePinKind::Output) {
        result.output = pinA;
        result.input = pinB;
    } else {
        result.output = pinB;
        result.input = pinA;
    }

    return result;
}

// ============================================================================
// LinkValidator Implementation
// ============================================================================

namespace LinkValidator {

const char* pinTypeName(PinType type) {
    switch (type) {
    case PinType::UniformBuffer:
        return "Uniform Buffer";
    case PinType::Image:
        return "Image";
    case PinType::VertexData:
        return "Vertex Data";
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

bool arePinTypesCompatible(PinType outputType, PinType inputType) {
    // Currently: types must match exactly
    return outputType == inputType;
}

ValidationResult validate(NodeGraph& graph, ed::PinId startId, ed::PinId endId) {
    // Legacy validation path - used as fallback when pins aren't in registry
    // Do NOT delegate to graph.validateLink() here - that would cause infinite recursion

    // Create normalized pin pair
    auto pins = PinPair::create(graph, startId, endId);
    if (!pins) {
        return ValidationResult::Fail("Invalid pins or same node");
    }

    // Check type compatibility
    if (!arePinTypesCompatible(pins->output.pin->type, pins->input.pin->type)) {
        return ValidationResult::Fail(
            std::string(pinTypeName(pins->output.pin->type)) +
            " cannot connect to " +
            pinTypeName(pins->input.pin->type)
        );
    }

    // Check format compatibility (for pipeline image pins)
    auto formatResult = checkPipelineFormatCompatibility(graph, *pins);
    if (!formatResult) {
        return formatResult;
    }

    return ValidationResult::Ok();
}

ValidationResult canCreate(NodeGraph& graph, ed::PinId startId, ed::PinId endId) {
    // Legacy validation path - used as fallback when pins aren't in registry
    // Do NOT delegate to graph.canCreateLink() here - that would cause infinite recursion

    // First, check if the link would be valid
    auto result = validate(graph, startId, endId);
    if (!result) {
        return result;
    }

    // Additional constraint: input pins can only have one link
    auto pins = PinPair::create(graph, startId, endId);
    if (pins && LinkManager::isPinLinked(graph.pinToLinks, pins->input.pin->id)) {
        return ValidationResult::Fail(
            "Input pin '" + pins->input.pin->label + "' is already linked"
        );
    }

    return ValidationResult::Ok();
}

}  // namespace LinkValidator

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
    ed::LinkId id
) {
    auto it = std::find_if(links.begin(), links.end(),
        [&](const Link& l) { return l.id == id; });

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
    ed::PinId pinId
) {
    auto it = pinToLinks.find(pinId);
    if (it == pinToLinks.end())
        return;

    // Copy IDs since we'll modify during iteration
    auto linkIds = it->second;
    for (const auto& linkId : linkIds) {
        removeLink(links, pinToLinks, linkId);
    }
}

bool isPinLinked(const PinToLinksIndex& pinToLinks, ed::PinId id) {
    auto it = pinToLinks.find(id);
    return it != pinToLinks.end() && !it->second.empty();
}

void removeInvalidLinks(
    NodeGraph& graph,
    std::vector<Link>& links,
    PinToLinksIndex& pinToLinks
) {
    std::vector<ed::LinkId> toRemove;

    for (const auto& link : links) {
        if (!LinkValidator::validate(graph, link.startPin, link.endPin)) {
            toRemove.push_back(link.id);
        }
    }

    for (const auto& linkId : toRemove) {
        removeLink(links, pinToLinks, linkId);
    }
}

void clearLinks(std::vector<Link>& links, PinToLinksIndex& pinToLinks) {
    links.clear();
    pinToLinks.clear();
}

}  // namespace LinkManager
