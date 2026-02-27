#pragma once

#include "link.h"
#include "pin_registry.h"
#include "validation_rules.h"
#include <memory>
#include <vector>

class Node;

/**
 * @class NodeGraph
 * @brief Container and manager for all nodes and links in the visual pipeline editor.
 *
 * Provides node/link CRUD operations, dependency resolution via topological sorting,
 * and fast pin-to-link lookup. UI-agnostic design allows independent testing.
 */
class NodeGraph {
public:
    NodeGraph();

    bool isPinLinked(ax::NodeEditor::PinId id) const;

    // Legacy pin lookup - iterates through all nodes (O(n))
    // TODO: Deprecate once all nodes use PinRegistry
    PinLookupResult findPin(ax::NodeEditor::PinId id);

    // New registry-based lookup - O(1) when pin is registered
    const PinEntry* findPinEntry(ax::NodeEditor::PinId id) const;
    PinEntry* findPinEntry(ax::NodeEditor::PinId id);

    // Validate a proposed link using the validation chain
    ValidationResult validateLink(ax::NodeEditor::PinId startId, ax::NodeEditor::PinId endId);

    // Check if a new link can be created (validate + constraints)
    ValidationResult canCreateLink(ax::NodeEditor::PinId startId, ax::NodeEditor::PinId endId);

    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Link> links;
    std::unordered_map<Node*, std::vector<Node*>> dependencyGraph;

    // Centralized pin registry - owns all pin data
    PinRegistry pinRegistry;

    // Extensible validation chain
    LinkValidationChain validationChain;

    Node* addNode(std::unique_ptr<Node> node);
    void removeNode(ed::NodeId nodeId);

    void addLink(const Link& link);
    void removeLink(ax::NodeEditor::LinkId id);
    void removeLinksForPin(ax::NodeEditor::PinId pinId);

    PinToLinksIndex pinToLinks;

    void removeInvalidLinks();

    void buildDependencies();
    std::vector<Node*> topologicalSort() const;

    void clear();

    // Initialize default validation rules
    void initializeValidation();

    bool hasShadowPipeline = false;
    bool hasDeferredPipeline = false;
};
