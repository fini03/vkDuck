#pragma once

#include "../shader/shader_types.h"
#include "pin_id_hash.h"  // Hash specializations for PinId/LinkId
#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

using namespace ShaderTypes;

class Node;
class NodeGraph;
class PipelineNode;

// ============================================================================
// Core Types
// ============================================================================

struct Link {
    ed::LinkId id;
    ed::PinId startPin;  // Always output pin
    ed::PinId endPin;    // Always input pin
};

enum class NodePinKind { Input, Output, None };

struct PinLookupResult {
    Node* node = nullptr;
    Pin* pin = nullptr;
    NodePinKind kind = NodePinKind::None;
};

using PinToLinksIndex =
    std::unordered_map<ed::PinId, std::unordered_set<ed::LinkId>>;

// ============================================================================
// Validation Result - carries success/failure with reason
// ============================================================================

struct ValidationResult {
    bool valid = false;
    std::string reason;

    // Implicit conversion to bool for if(result) checks
    operator bool() const { return valid; }

    // Factory methods for clarity
    static ValidationResult Ok() { return {true, ""}; }
    static ValidationResult Fail(std::string reason) {
        return {false, std::move(reason)};
    }
};

// ============================================================================
// Pin Pair - normalized output→input pair
// ============================================================================

struct PinPair {
    PinLookupResult output;
    PinLookupResult input;

    // Creates a normalized pin pair (output→input) from any two pins.
    // Returns nullopt if:
    // - Either pin doesn't exist
    // - Pins are on the same node
    // - Pins are both inputs or both outputs
    static std::optional<PinPair> create(
        NodeGraph& graph, ed::PinId a, ed::PinId b
    );
};

// ============================================================================
// Link Validation - clean public API
// ============================================================================

namespace LinkValidator {

// Check if an existing link is still valid (type + format compatibility).
// Use this when validating links after shader changes.
ValidationResult validate(NodeGraph& graph, ed::PinId startId, ed::PinId endId);

// Check if a new link can be created (validate + single-link constraint).
// Use this when user attempts to create a new connection.
ValidationResult canCreate(NodeGraph& graph, ed::PinId startId, ed::PinId endId);

// Check if two pin types are compatible for connection.
bool arePinTypesCompatible(PinType outputType, PinType inputType);

// Human-readable name for a pin type.
const char* pinTypeName(PinType type);

}  // namespace LinkValidator

// ============================================================================
// Link Storage Management
// ============================================================================

namespace LinkManager {

void addLink(
    std::vector<Link>& links, PinToLinksIndex& pinToLinks, const Link& link
);

void removeLink(
    std::vector<Link>& links, PinToLinksIndex& pinToLinks, ed::LinkId id
);

void removeLinksForPin(
    std::vector<Link>& links, PinToLinksIndex& pinToLinks, ed::PinId pinId
);

bool isPinLinked(const PinToLinksIndex& pinToLinks, ed::PinId id);

void removeInvalidLinks(
    NodeGraph& graph, std::vector<Link>& links, PinToLinksIndex& pinToLinks
);

void clearLinks(std::vector<Link>& links, PinToLinksIndex& pinToLinks);

}  // namespace LinkManager
