#pragma once

#include "link.h"
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
    NodeGraph() = default;

    bool isPinLinked(ax::NodeEditor::PinId id) const;
    PinLookupResult findPin(ax::NodeEditor::PinId id);

    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Link> links;
    std::unordered_map<Node*, std::vector<Node*>> dependencyGraph;

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

    bool hasShadowPipeline = false;
    bool hasDeferredPipeline = false;
};
