#include "node_graph.h"
#include "camera_node.h"
#include "fixed_camera_node.h"
#include "light_node.h"
// Singular model nodes deprecated - moved to graph/deprecated/
#include "multi_model_source_node.h"
#include "multi_vertex_data_node.h"
#include "multi_ubo_node.h"
#include "multi_material_node.h"
#include "node.h"
#include "pipeline_node.h"
#include "present_node.h"
#include <iostream>
#include <stdexcept>

// ============================================================================
// Construction / Initialization
// ============================================================================

NodeGraph::NodeGraph() {
    initializeValidation();
}

void NodeGraph::initializeValidation() {
    validationChain.clear();
    validationChain.addRule<TypeCompatibilityRule>();
    // Note: SingleInputLinkRule is checked separately in canCreateLink()
    // since it needs special handling for existing links
    validationChain.addRule<ImageFormatRule>(GetAllowedImageFormats());
    validationChain.addRule<PipelineFormatRule>();
}

// ============================================================================
// Pin Lookup
// ============================================================================

bool NodeGraph::isPinLinked(ax::NodeEditor::PinId id) const {
    return LinkManager::isPinLinked(pinToLinks, id);
}

const PinEntry* NodeGraph::findPinEntry(ax::NodeEditor::PinId id) const {
    return pinRegistry.findByEditorId(id);
}

PinEntry* NodeGraph::findPinEntry(ax::NodeEditor::PinId id) {
    return pinRegistry.findByEditorId(id);
}

// Helper to find a pin and its parent node.
// Uses virtual getPinById() dispatch (O(n) node iteration, no dynamic_cast overhead).
// For O(1) lookup when only PinEntry info is needed, see findPinEntry().
PinLookupResult NodeGraph::findPin(ax::NodeEditor::PinId id) {
    // Use virtual method dispatch to find the pin
    for (const auto& node : nodes) {
        if (auto lookup = node->getPinById(id); lookup) {
            return {
                node.get(),
                lookup.pin,
                lookup.isInput ? NodePinKind::Input : NodePinKind::Output
            };
        }
    }
    return {nullptr, nullptr, NodePinKind::None};
}

Node* NodeGraph::addNode(std::unique_ptr<Node> node) {
    Node* raw = node.get();
    nodes.emplace_back(std::move(node));
    return raw;
}

void NodeGraph::removeNode(ed::NodeId nodeId) {
    // First, find the node to get its pins
    Node* nodeToRemove = nullptr;
    for (const auto& n : nodes) {
        if (static_cast<uint64_t>(n->id) == nodeId.Get()) {
            nodeToRemove = n.get();
            break;
        }
    }

    if (!nodeToRemove)
        return;

    // Collect all pin IDs from this node
    std::unordered_set<ax::NodeEditor::PinId> pinsToRemove;

    // Handle different node types
    if (auto* camera = dynamic_cast<CameraNodeBase*>(nodeToRemove)) {
        pinsToRemove.insert(camera->cameraPin.id);
    } else if (auto* light = dynamic_cast<LightNode*>(nodeToRemove)) {
        pinsToRemove.insert(light->lightArrayPin.id);
    } else if (auto* multiSource = dynamic_cast<MultiModelSourceNode*>(nodeToRemove)) {
        pinsToRemove.insert(multiSource->modelSourcePin.id);
    } else if (auto* multiVertex = dynamic_cast<MultiVertexDataNode*>(nodeToRemove)) {
        pinsToRemove.insert(multiVertex->sourceInputPin.id);
        pinsToRemove.insert(multiVertex->vertexDataPin.id);
    } else if (auto* multiUbo = dynamic_cast<MultiUBONode*>(nodeToRemove)) {
        pinsToRemove.insert(multiUbo->sourceInputPin.id);
        pinsToRemove.insert(multiUbo->modelMatrixPin.id);
        pinsToRemove.insert(multiUbo->cameraPin.id);
        pinsToRemove.insert(multiUbo->lightPin.id);
    } else if (auto* multiMat = dynamic_cast<MultiMaterialNode*>(nodeToRemove)) {
        pinsToRemove.insert(multiMat->sourceInputPin.id);
        pinsToRemove.insert(multiMat->baseColorPin.id);
        pinsToRemove.insert(multiMat->emissivePin.id);
        pinsToRemove.insert(multiMat->metallicRoughnessPin.id);
        pinsToRemove.insert(multiMat->normalPin.id);
        pinsToRemove.insert(multiMat->materialParamsPin.id);
    } else if (auto* pipeline =
                   dynamic_cast<PipelineNode*>(nodeToRemove)) {
        if (pipeline->vertexDataPin.id.Get() != 0) {
            pinsToRemove.insert(pipeline->vertexDataPin.id);
        }
        for (auto& binding : pipeline->inputBindings) {
            pinsToRemove.insert(binding.pin.id);
        }
        for (auto& config :
             pipeline->shaderReflection.attachmentConfigs) {
            pinsToRemove.insert(config.pin.id);
        }

        // Collect single camera pin
        if (pipeline->hasCameraInput &&
            pipeline->cameraInput.pin.id.Get() != 0) {
            pinsToRemove.insert(pipeline->cameraInput.pin.id);
        }

        // Collect single light pin
        if (pipeline->hasLightInput &&
            pipeline->lightInput.pin.id.Get() != 0) {
            pinsToRemove.insert(pipeline->lightInput.pin.id);
        }
    } else if (auto* present =
                   dynamic_cast<PresentNode*>(nodeToRemove)) {
        pinsToRemove.insert(present->imagePin.id);
    }

    // Remove all links connected to this node's pins using the index
    for (const auto& pinId : pinsToRemove) {
        removeLinksForPin(pinId);
    }

    // Now remove the node itself
    std::erase_if(nodes, [&](const auto& n) {
        return static_cast<uint64_t>(n->id) == nodeId.Get();
    });
}

// --- Link management (delegates to LinkManager)
// ---------------------------------------------------------

void NodeGraph::addLink(const Link& link) {
    LinkManager::addLink(links, pinToLinks, link);
}

void NodeGraph::removeLink(ax::NodeEditor::LinkId id) {
    LinkManager::removeLink(links, pinToLinks, id);
}

void NodeGraph::removeLinksForPin(ax::NodeEditor::PinId pinId) {
    LinkManager::removeLinksForPin(links, pinToLinks, pinId);
}

void NodeGraph::removeInvalidLinks() {
    LinkManager::removeInvalidLinks(*this, links, pinToLinks);
}

// --- Dependency Graph
// --------------------------------------------------------

void NodeGraph::buildDependencies() {
    dependencyGraph.clear();
    std::unordered_map<ax::NodeEditor::PinId, Node*> pinInfo;

    for (const auto& nodePtr : nodes) {
        Node* node = nodePtr.get();

        if (auto* camera = dynamic_cast<CameraNodeBase*>(node)) {
            pinInfo[camera->cameraPin.id] = node;
        } else if (auto* light = dynamic_cast<LightNode*>(node)) {
            pinInfo[light->lightArrayPin.id] = node;
        } else if (auto* multiSource = dynamic_cast<MultiModelSourceNode*>(node)) {
            pinInfo[multiSource->modelSourcePin.id] = node;
        } else if (auto* multiVertex = dynamic_cast<MultiVertexDataNode*>(node)) {
            pinInfo[multiVertex->sourceInputPin.id] = node;
            pinInfo[multiVertex->vertexDataPin.id] = node;
        } else if (auto* multiUbo = dynamic_cast<MultiUBONode*>(node)) {
            pinInfo[multiUbo->sourceInputPin.id] = node;
            pinInfo[multiUbo->modelMatrixPin.id] = node;
            pinInfo[multiUbo->cameraPin.id] = node;
            pinInfo[multiUbo->lightPin.id] = node;
        } else if (auto* multiMat = dynamic_cast<MultiMaterialNode*>(node)) {
            pinInfo[multiMat->sourceInputPin.id] = node;
            pinInfo[multiMat->baseColorPin.id] = node;
            pinInfo[multiMat->emissivePin.id] = node;
            pinInfo[multiMat->metallicRoughnessPin.id] = node;
            pinInfo[multiMat->normalPin.id] = node;
            pinInfo[multiMat->materialParamsPin.id] = node;
        } else if (auto* pipeline = dynamic_cast<PipelineNode*>(node)) {
            // Input bindings
            for (auto& binding : pipeline->inputBindings)
                pinInfo[binding.pin.id] = node;

            // Output attachments
            for (auto& config :
                 pipeline->shaderReflection.attachmentConfigs)
                pinInfo[config.pin.id] = node;

            // Single camera pin
            if (pipeline->hasCameraInput &&
                pipeline->cameraInput.pin.id.Get() != 0) {
                pinInfo[pipeline->cameraInput.pin.id] = node;
            }

            // Single light pin
            if (pipeline->hasLightInput &&
                pipeline->lightInput.pin.id.Get() != 0) {
                pinInfo[pipeline->lightInput.pin.id] = node;
            }

            // Attachment input pins (for shared render pass chaining)
            for (const auto& attIn : pipeline->attachmentInputs) {
                if (attIn.pin.id.Get() != 0) {
                    pinInfo[attIn.pin.id] = node;
                }
            }
        }
    }

    // Build edges: A -> B means A must run before B
    for (const auto& link : links) {
        auto itA = pinInfo.find(link.startPin);
        auto itB = pinInfo.find(link.endPin);
        if (itA == pinInfo.end() || itB == pinInfo.end())
            continue;

        Node* nodeA = itA->second;
        Node* nodeB = itB->second;
        assert(nodeA != nodeB);

        dependencyGraph[nodeA].push_back(nodeB);
    }

    // Ensure all nodes appear in map
    for (const auto& n : nodes)
        if (!dependencyGraph.count(n.get()))
            dependencyGraph[n.get()] = {};
}

std::vector<Node*> NodeGraph::topologicalSort() const {
    std::vector<Node*> result;
    std::unordered_set<Node*> visited, visiting;

    std::function<void(Node*)> dfs = [&](Node* n) {
        if (visited.count(n))
            return;
        if (visiting.count(n))
            throw std::runtime_error("Cycle detected!");

        visiting.insert(n);
        auto it = dependencyGraph.find(n);
        if (it != dependencyGraph.end()) {
            for (Node* neighbor : it->second)
                dfs(neighbor);
        }
        visiting.erase(n);
        visited.insert(n);
        result.push_back(n);
    };

    for (auto& [node, _] : dependencyGraph)
        if (!visited.count(node))
            dfs(node);

    std::reverse(result.begin(), result.end());
    return result;
}

void NodeGraph::clear() {
    nodes.clear();
    LinkManager::clearLinks(links, pinToLinks);
    dependencyGraph.clear();
    pinRegistry.clear();
}

// ============================================================================
// Link Validation (using the new chain)
// ============================================================================

ValidationResult NodeGraph::validateLink(
    ax::NodeEditor::PinId startId, ax::NodeEditor::PinId endId
) {
    // First try registry-based lookup
    const PinEntry* startEntry = findPinEntry(startId);
    const PinEntry* endEntry = findPinEntry(endId);

    // If both pins are in the registry, use the new validation path
    if (startEntry && endEntry) {
        // Find owning nodes
        Node* startNode = nullptr;
        Node* endNode = nullptr;
        for (const auto& n : nodes) {
            if (n->getId() == startEntry->ownerNodeId) startNode = n.get();
            if (n->getId() == endEntry->ownerNodeId) endNode = n.get();
        }

        if (!startNode || !endNode) {
            return ValidationResult::Fail("Node not found");
        }

        // Normalize to output -> input
        const PinEntry* outputPin;
        const PinEntry* inputPin;
        Node* outputNode;
        Node* inputNode;

        if (startEntry->kind == PinKind::Output) {
            outputPin = startEntry;
            inputPin = endEntry;
            outputNode = startNode;
            inputNode = endNode;
        } else {
            outputPin = endEntry;
            inputPin = startEntry;
            outputNode = endNode;
            inputNode = startNode;
        }

        // Same node check
        if (outputNode == inputNode) {
            return ValidationResult::Fail("Cannot connect to same node");
        }

        // Must be output -> input
        if (outputPin->kind != PinKind::Output ||
            inputPin->kind != PinKind::Input) {
            return ValidationResult::Fail("Must connect output to input");
        }

        ValidationContext ctx{this, outputPin, inputPin, outputNode, inputNode};
        return validationChain.validate(ctx);
    }

    // Fall back to legacy validation
    return LinkValidator::validate(*this, startId, endId);
}

ValidationResult NodeGraph::canCreateLink(
    ax::NodeEditor::PinId startId, ax::NodeEditor::PinId endId
) {
    // First check if link would be valid
    auto result = validateLink(startId, endId);
    if (!result) {
        return result;
    }

    // Check single input link constraint
    // Determine which pin is the input
    const PinEntry* startEntry = findPinEntry(startId);
    const PinEntry* endEntry = findPinEntry(endId);

    ax::NodeEditor::PinId inputPinId;
    std::string inputLabel;

    if (startEntry && endEntry) {
        // Both in registry
        if (startEntry->kind == PinKind::Input) {
            inputPinId = startEntry->id;
            inputLabel = startEntry->label;
        } else {
            inputPinId = endEntry->id;
            inputLabel = endEntry->label;
        }
    } else {
        // Fall back to legacy check
        auto pins = PinPair::create(*this, startId, endId);
        if (!pins) {
            return ValidationResult::Fail("Invalid pins");
        }
        inputPinId = pins->input.pin->id;
        inputLabel = pins->input.pin->label;
    }

    if (isPinLinked(inputPinId)) {
        return ValidationResult::Fail(
            "Input pin '" + inputLabel + "' is already linked"
        );
    }

    return ValidationResult::Ok();
}
