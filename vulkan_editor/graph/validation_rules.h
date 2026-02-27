#pragma once

#include "link.h"
#include "pin_registry.h"
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

class Node;
class NodeGraph;

/**
 * @struct ValidationContext
 * @brief Contains all information needed to validate a potential link.
 */
struct ValidationContext {
    NodeGraph* graph;
    const PinEntry* outputPin;
    const PinEntry* inputPin;
    Node* outputNode;
    Node* inputNode;
};

/**
 * @class IValidationRule
 * @brief Interface for link validation rules (similar to Java interface pattern).
 *
 * Implement this interface to add custom validation logic.
 * Rules are checked in order - first failure stops the chain.
 */
class IValidationRule {
public:
    virtual ~IValidationRule() = default;

    /**
     * Check if the proposed link is valid.
     * @param ctx The validation context with pin and node information.
     * @return ValidationResult::Ok() if valid, ValidationResult::Fail(reason) if not.
     */
    virtual ValidationResult check(const ValidationContext& ctx) const = 0;

    /**
     * Get the name of this rule (for debugging/logging).
     */
    virtual std::string name() const = 0;
};

/**
 * @class LinkValidationChain
 * @brief Manages a chain of validation rules.
 *
 * Rules are checked in order. First failure stops the chain and returns the error.
 */
class LinkValidationChain {
public:
    /**
     * Add a rule to the chain.
     */
    void addRule(std::unique_ptr<IValidationRule> rule);

    /**
     * Add a rule to the chain (convenience template).
     */
    template <typename T, typename... Args>
    void addRule(Args&&... args) {
        rules.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    /**
     * Validate a proposed link through all rules.
     * @param ctx The validation context.
     * @return ValidationResult::Ok() if all rules pass, first failure otherwise.
     */
    ValidationResult validate(const ValidationContext& ctx) const;

    /**
     * Get all validation failures (for detailed diagnostics).
     */
    std::vector<std::pair<std::string, ValidationResult>>
    validateAll(const ValidationContext& ctx) const;

    /**
     * Clear all rules.
     */
    void clear();

    /**
     * Get the number of rules.
     */
    size_t size() const { return rules.size(); }

private:
    std::vector<std::unique_ptr<IValidationRule>> rules;
};

// ============================================================================
// Built-in Validation Rules
// ============================================================================

/**
 * @class TypeCompatibilityRule
 * @brief Ensures output and input pin types are compatible.
 *
 * Currently requires exact type match.
 */
class TypeCompatibilityRule : public IValidationRule {
public:
    ValidationResult check(const ValidationContext& ctx) const override;
    std::string name() const override { return "TypeCompatibility"; }
};

/**
 * @class SingleInputLinkRule
 * @brief Ensures input pins only have one incoming link.
 */
class SingleInputLinkRule : public IValidationRule {
public:
    ValidationResult check(const ValidationContext& ctx) const override;
    std::string name() const override { return "SingleInputLink"; }
};

/**
 * @class ImageFormatRule
 * @brief Validates that image formats are in the allowed list.
 */
class ImageFormatRule : public IValidationRule {
public:
    explicit ImageFormatRule(std::vector<VkFormat> allowed);
    ValidationResult check(const ValidationContext& ctx) const override;
    std::string name() const override { return "ImageFormat"; }

private:
    std::vector<VkFormat> allowedFormats;
};

/**
 * @class PipelineFormatRule
 * @brief Validates format compatibility between pipeline attachment outputs.
 */
class PipelineFormatRule : public IValidationRule {
public:
    ValidationResult check(const ValidationContext& ctx) const override;
    std::string name() const override { return "PipelineFormat"; }
};

// ============================================================================
// Allowed Image Formats (moved from UI)
// ============================================================================

/**
 * Get the list of allowed image formats for validation.
 * This is the canonical source - UI should use this list.
 */
const std::vector<VkFormat>& GetAllowedImageFormats();
