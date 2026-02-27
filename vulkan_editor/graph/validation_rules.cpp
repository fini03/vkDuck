#include "validation_rules.h"
#include "../util/logger.h"
#include "node_graph.h"
#include "pipeline_node.h"
#include <algorithm>

// ============================================================================
// Allowed Image Formats (canonical source - moved from UI)
// ============================================================================

static const std::vector<VkFormat> s_AllowedImageFormats = {
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R8G8B8A8_SRGB,
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R16G16B16A16_UNORM,
    VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D32_SFLOAT_S8_UINT
};

const std::vector<VkFormat>& GetAllowedImageFormats() {
    return s_AllowedImageFormats;
}

// ============================================================================
// LinkValidationChain Implementation
// ============================================================================

void LinkValidationChain::addRule(std::unique_ptr<IValidationRule> rule) {
    rules.push_back(std::move(rule));
}

ValidationResult LinkValidationChain::validate(const ValidationContext& ctx
) const {
    for (const auto& rule : rules) {
        auto result = rule->check(ctx);
        if (!result) {
            Log::debug(
                "ValidationChain",
                "Rule '{}' failed: {}",
                rule->name(),
                result.reason
            );
            return result;
        }
    }
    return ValidationResult::Ok();
}

std::vector<std::pair<std::string, ValidationResult>>
LinkValidationChain::validateAll(const ValidationContext& ctx) const {
    std::vector<std::pair<std::string, ValidationResult>> results;
    for (const auto& rule : rules) {
        auto result = rule->check(ctx);
        if (!result) {
            results.emplace_back(rule->name(), result);
        }
    }
    return results;
}

void LinkValidationChain::clear() {
    rules.clear();
}

// ============================================================================
// TypeCompatibilityRule Implementation
// ============================================================================

ValidationResult TypeCompatibilityRule::check(const ValidationContext& ctx
) const {
    if (!ctx.outputPin || !ctx.inputPin) {
        return ValidationResult::Fail("Invalid pins");
    }

    // Currently: types must match exactly
    if (ctx.outputPin->type != ctx.inputPin->type) {
        return ValidationResult::Fail(
            std::string(LinkValidator::pinTypeName(ctx.outputPin->type)) +
            " cannot connect to " +
            LinkValidator::pinTypeName(ctx.inputPin->type)
        );
    }

    return ValidationResult::Ok();
}

// ============================================================================
// SingleInputLinkRule Implementation
// ============================================================================

ValidationResult SingleInputLinkRule::check(const ValidationContext& ctx
) const {
    if (!ctx.graph || !ctx.inputPin) {
        return ValidationResult::Fail("Invalid context");
    }

    // Check if input pin already has a link
    if (LinkManager::isPinLinked(ctx.graph->pinToLinks, ctx.inputPin->id)) {
        return ValidationResult::Fail(
            "Input pin '" + ctx.inputPin->label + "' is already linked"
        );
    }

    return ValidationResult::Ok();
}

// ============================================================================
// ImageFormatRule Implementation
// ============================================================================

ImageFormatRule::ImageFormatRule(std::vector<VkFormat> allowed)
    : allowedFormats(std::move(allowed)) {}

ValidationResult ImageFormatRule::check(const ValidationContext& ctx) const {
    if (!ctx.outputPin || !ctx.inputPin) {
        return ValidationResult::Fail("Invalid pins");
    }

    // Only applies to image pins
    if (ctx.outputPin->type != PinType::Image) {
        return ValidationResult::Ok();
    }

    // Need to get format from pipeline node's attachment config
    auto* pipelineNode = dynamic_cast<PipelineNode*>(ctx.outputNode);
    if (!pipelineNode) {
        // Not a pipeline node output - skip format check
        return ValidationResult::Ok();
    }

    // Find the attachment config for this output pin
    VkFormat outputFormat = VK_FORMAT_UNDEFINED;
    for (const auto& config : pipelineNode->shaderReflection.attachmentConfigs
    ) {
        if (config.pin.label == ctx.outputPin->label) {
            outputFormat = config.format;
            break;
        }
    }

    if (outputFormat == VK_FORMAT_UNDEFINED) {
        // No attachment config found - allow for backwards compatibility
        return ValidationResult::Ok();
    }

    // Check if format is in allowed list
    auto it =
        std::find(allowedFormats.begin(), allowedFormats.end(), outputFormat);
    if (it == allowedFormats.end()) {
        return ValidationResult::Fail("Image format not supported");
    }

    return ValidationResult::Ok();
}

// ============================================================================
// PipelineFormatRule Implementation
// ============================================================================

namespace {

// Find attachment config by pin label
const ShaderTypes::AttachmentConfig*
findAttachment(const PipelineNode* node, const std::string& label) {
    if (!node)
        return nullptr;

    const auto& configs = node->shaderReflection.attachmentConfigs;
    auto it = std::find_if(
        configs.begin(),
        configs.end(),
        [&label](const ShaderTypes::AttachmentConfig& c) {
            return c.name == label;
        }
    );

    return (it != configs.end()) ? &(*it) : nullptr;
}

}  // namespace

ValidationResult PipelineFormatRule::check(const ValidationContext& ctx
) const {
    if (!ctx.outputPin || !ctx.inputPin) {
        return ValidationResult::Fail("Invalid pins");
    }

    auto* outputNode = dynamic_cast<PipelineNode*>(ctx.outputNode);
    auto* inputNode = dynamic_cast<PipelineNode*>(ctx.inputNode);

    // Format check only applies between pipeline nodes
    if (!outputNode || !inputNode) {
        return ValidationResult::Ok();
    }

    // Only check format for image pins
    if (ctx.outputPin->type != PinType::Image) {
        return ValidationResult::Ok();
    }

    // Find the output attachment to get its format
    auto* attachment = findAttachment(outputNode, ctx.outputPin->label);
    if (!attachment) {
        // No attachment config found - allow for backwards compatibility
        return ValidationResult::Ok();
    }

    // Check against allowed formats
    const auto& allowed = GetAllowedImageFormats();
    auto it = std::find(allowed.begin(), allowed.end(), attachment->format);
    if (it == allowed.end()) {
        return ValidationResult::Fail("Image format incompatible");
    }

    return ValidationResult::Ok();
}
