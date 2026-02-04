#include "node_graph.h"
#include "camera_node.h"
#include "fixed_camera_node.h"
#include "light_node.h"
#include "model_node.h"
#include "node.h"
#include "pipeline_node.h"
#include "present_node.h"
#include <iostream>
#include <stdexcept>

// --- Node management
// ---------------------------------------------------------

bool NodeGraph::isPinLinked(ax::NodeEditor::PinId id) const {
    return LinkManager::isPinLinked(pinToLinks, id);
}

// Helper to find a pin and its parent node
PinLookupResult NodeGraph::findPin(ax::NodeEditor::PinId id) {
    for (const auto& node : nodes) {
        // --- Handle ModelNode ---
        if (auto* model = dynamic_cast<ModelNode*>(node.get())) {
            if (model->modelMatrixPin.id == id)
                return {
                    model, &model->modelMatrixPin, NodePinKind::Output
                };
            if (model->texturePin.id == id)
                return {model, &model->texturePin, NodePinKind::Output};
            if (model->vertexDataPin.id == id)
                return {
                    model, &model->vertexDataPin, NodePinKind::Output
                };
            if (model->cameraPin.id == id)
                return {model, &model->cameraPin, NodePinKind::Output};
        }

        // --- Handle all Camera types (Orbital, Fixed) ---
        if (auto* camera = dynamic_cast<CameraNodeBase*>(node.get())) {
            if (camera->cameraPin.id == id)
                return {
                    camera, &camera->cameraPin, NodePinKind::Output
                };
        }

        // --- Handle LightNode ---
        if (auto* light = dynamic_cast<LightNode*>(node.get())) {
            if (light->lightArrayPin.id == id)
                return {
                    light, &light->lightArrayPin, NodePinKind::Output
                };
        }

        // --- Handle PipelineNode ---
        if (auto* pipeline = dynamic_cast<PipelineNode*>(node.get())) {
            if (pipeline->vertexDataPin.id.Get() != 0 &&
                pipeline->vertexDataPin.id == id) {
                return {
                    pipeline, &pipeline->vertexDataPin,
                    NodePinKind::Input
                };
            }

            // Check regular input bindings
            for (auto& binding : pipeline->inputBindings) {
                if (binding.pin.id == id)
                    return {pipeline, &binding.pin, NodePinKind::Input};
            }

            // Check output attachments
            for (auto& config :
                 pipeline->shaderReflection.attachmentConfigs) {
                if (config.pin.id == id)
                    return {pipeline, &config.pin, NodePinKind::Output};
            }

            // Check single camera pin
            if (pipeline->hasCameraInput &&
                pipeline->cameraInput.pin.id == id) {
                return {
                    pipeline, &pipeline->cameraInput.pin,
                    NodePinKind::Input
                };
            }

            // Check single light pin
            if (pipeline->hasLightInput &&
                pipeline->lightInput.pin.id == id) {
                return {
                    pipeline, &pipeline->lightInput.pin,
                    NodePinKind::Input
                };
            }
        }

        // --- Handle PresentNode ---
        if (auto* present = dynamic_cast<PresentNode*>(node.get())) {
            if (present->imagePin.id == id)
                return {
                    present, &present->imagePin, NodePinKind::Input
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
    if (auto* model = dynamic_cast<ModelNode*>(nodeToRemove)) {
        pinsToRemove.insert(model->modelMatrixPin.id);
        pinsToRemove.insert(model->texturePin.id);
        pinsToRemove.insert(model->vertexDataPin.id);
        pinsToRemove.insert(model->cameraPin.id);
    } else if (auto* camera =
                   dynamic_cast<CameraNodeBase*>(nodeToRemove)) {
        pinsToRemove.insert(camera->cameraPin.id);
    } else if (auto* light = dynamic_cast<LightNode*>(nodeToRemove)) {
        pinsToRemove.insert(light->lightArrayPin.id);
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

        if (auto* model = dynamic_cast<ModelNode*>(node)) {
            pinInfo[model->modelMatrixPin.id] = node;
            pinInfo[model->texturePin.id] = node;
            pinInfo[model->vertexDataPin.id] = node;
            pinInfo[model->cameraPin.id] = node;
        } else if (auto* camera = dynamic_cast<CameraNodeBase*>(node)) {
            pinInfo[camera->cameraPin.id] = node;
        } else if (auto* light = dynamic_cast<LightNode*>(node)) {
            pinInfo[light->lightArrayPin.id] = node;
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
}
