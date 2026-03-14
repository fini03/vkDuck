#include "graph_serializer.h"
#include "../graph/camera_node.h"
#include "../graph/fixed_camera_node.h"
#include "../graph/fps_camera_node.h"
#include "../graph/light_node.h"
#include "../graph/material_node.h"
// #include "../graph/model_node.h"  // DEPRECATED - replaced by split nodes
#include "../graph/node_graph.h"
#include "../graph/pipeline_node.h"
#include "../graph/present_node.h"
#include "../graph/ubo_node.h"
#include "../graph/vertex_data_node.h"
#include "../graph/multi_model_source_node.h"
#include "../graph/multi_model_consumer_base.h"
#include "../graph/multi_vertex_data_node.h"
#include "../graph/multi_ubo_node.h"
#include "../graph/multi_material_node.h"
#include "../shader/shader_manager.h"
#include "../util/logger.h"
#include <fstream>
#include <vulkan/vulkan.hpp>

namespace ed = ax::NodeEditor;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

namespace {
// Extract max pin ID from JSON pin arrays (checks all possible pin array names)
int getMaxPinId(const nlohmann::json& jNode) {
    int maxId = 0;

    // Check standard pin arrays
    const std::vector<std::string> pinArrayNames = {
        "inputPins", "outputPins", "extraPins"
    };

    for (const auto& arrayName : pinArrayNames) {
        if (jNode.contains(arrayName) && jNode[arrayName].is_array()) {
            for (const auto& pin : jNode[arrayName]) {
                if (pin.contains("id")) {
                    maxId = std::max(maxId, pin["id"].get<int>());
                }
            }
        }
    }

    return maxId;
}

// Scan entire JSON to find the maximum ID across all nodes, pins, and links
// This must be called BEFORE creating any nodes to avoid ID conflicts
int scanJsonForMaxId(const nlohmann::json& j) {
    int maxId = 0;

    // Scan all nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& jNode : j["nodes"]) {
            // Node ID
            if (jNode.contains("id")) {
                maxId = std::max(maxId, jNode["id"].get<int>());
            }
            // All pin IDs in this node
            maxId = std::max(maxId, getMaxPinId(jNode));
        }
    }

    // Scan all links
    if (j.contains("links") && j["links"].is_array()) {
        for (const auto& jLink : j["links"]) {
            if (jLink.contains("id")) {
                maxId = std::max(maxId, jLink["id"].get<int>());
            }
            // Also check pin references in links (defensive)
            if (jLink.contains("startPin")) {
                maxId = std::max(maxId, jLink["startPin"].get<int>());
            }
            if (jLink.contains("endPin")) {
                maxId = std::max(maxId, jLink["endPin"].get<int>());
            }
        }
    }

    return maxId;
}

// Build label->ID maps from JSON pin arrays
void buildPinIdMaps(
    const nlohmann::json& jNode,
    std::unordered_map<
        std::string,
        int>& inputPinIds,
    std::unordered_map<
        std::string,
        int>& outputPinIds
) {
    if (jNode.contains("inputPins") && jNode["inputPins"].is_array()) {
        for (const auto& pin : jNode["inputPins"]) {
            if (pin.contains("label") && pin.contains("id")) {
                inputPinIds[pin["label"].get<std::string>()] =
                    pin["id"].get<int>();
            }
        }
    }

    if (jNode.contains("outputPins") &&
        jNode["outputPins"].is_array()) {
        for (const auto& pin : jNode["outputPins"]) {
            if (pin.contains("label") && pin.contains("id")) {
                outputPinIds[pin["label"].get<std::string>()] =
                    pin["id"].get<int>();
            }
        }
    }

    // Also extract extra pins (vertexDataPin, cameraInput, lightInput)
    // These are treated as input pins for the purpose of ID restoration
    if (jNode.contains("extraPins") && jNode["extraPins"].is_array()) {
        for (const auto& pin : jNode["extraPins"]) {
            if (pin.contains("label") && pin.contains("id")) {
                inputPinIds[pin["label"].get<std::string>()] =
                    pin["id"].get<int>();
            }
        }
    }
}
} // namespace

// ============================================================================
// PIPELINE STATE - SERIALIZATION
// ============================================================================

nlohmann::json
PipelineState::serializeNodes(const NodeGraph& graph) const {
    nlohmann::json nodesJson = nlohmann::json::array();

    for (const auto& node : graph.nodes) {
        // Try to cast to ISerializable and use toJson()
        if (auto* serializable =
                dynamic_cast<ISerializable*>(node.get())) {
            nodesJson.push_back(serializable->toJson());
        } else {
            Log::warning(
                "PipelineState",
                "Node '{}' does not implement ISerializable", node->name
            );
        }
    }

    return nodesJson;
}

nlohmann::json
PipelineState::serializeLinks(const NodeGraph& graph) const {
    nlohmann::json linksJson = nlohmann::json::array();

    for (const auto& link : graph.links) {
        linksJson.push_back(
            {{"id", link.id.Get()},
             {"startPin", link.startPin.Get()},
             {"endPin", link.endPin.Get()}}
        );
    }

    return linksJson;
}

bool PipelineState::save(
    const NodeGraph& graph,
    const std::string& filePath
) const {
    try {
        nlohmann::json j;
        j["nodes"] = serializeNodes(graph);
        j["links"] = serializeLinks(graph);

        std::ofstream out(filePath);
        if (!out.is_open()) {
            Log::error(
                "PipelineState", "Failed to save pipeline state to: {}",
                filePath
            );
            return false;
        }

        out << j.dump(4);
        out.close();

        Log::info(
            "PipelineState", "Saved pipeline state to: {}", filePath
        );
        return true;
    } catch (const std::exception& e) {
        Log::error(
            "PipelineState", "Error saving pipeline state: {}", e.what()
        );
        return false;
    }
}

// ============================================================================
// NODE FACTORY
// ============================================================================

std::unique_ptr<Node> NodeFactory::createFromJson(
    const nlohmann::json& jNode,
    NodeGraph& graph,
    ShaderManager& shader_manager,
    int& maxId
) {
    int id = jNode["id"];
    maxId = std::max(maxId, id);
    maxId = std::max(maxId, getMaxPinId(jNode));

    std::string type = jNode["type"];
    Log::debug(
        "NodeFactory", "Creating node id={} type='{}'", id, type
    );

    std::unique_ptr<Node> node;

    if (type == "pipeline") {
        auto pipelineNode = std::make_unique<PipelineNode>(id);
        pipelineNode->fromJson(jNode);

        // Rebuild shader reflection (generates pins dynamically)
        shader_manager.reflectShader(pipelineNode.get(), graph);

        // Build label->ID maps from saved JSON
        std::unordered_map<std::string, int> inputPinIds, outputPinIds;
        buildPinIdMaps(jNode, inputPinIds, outputPinIds);

        // Restore pin IDs after reflection regenerated them
        pipelineNode->restorePinIds(inputPinIds, outputPinIds);

        // Restore attachment configs (blending settings, etc.)
        if (jNode.contains("attachmentConfigs") &&
            jNode["attachmentConfigs"].is_array()) {
            for (const auto& savedConfig : jNode["attachmentConfigs"]) {
                std::string attachmentName =
                    savedConfig.value("name", "");

                for (auto& config :
                     pipelineNode->shaderReflection.attachmentConfigs) {
                    if (config.name == attachmentName) {
                        auto originalHandle = config.handle;
                        auto originalPin = config.pin;

                        config.fromJson(savedConfig);

                        config.handle = originalHandle;
                        config.pin = originalPin;
                        config.initializeClearValue();
                        break;
                    }
                }
            }
        }

        node = std::move(pipelineNode);

    // NOTE: "model" type removed - ModelNode is deprecated
    // Old projects using ModelNode will need to be migrated to the new split nodes

    } else if (type == "vertex_data") {
        auto vertexDataNode = std::make_unique<VertexDataNode>(id);
        vertexDataNode->fromJson(jNode);

        // Load model via ModelManager if path is set
        if (vertexDataNode->modelPath[0] != '\0') {
            namespace fs = std::filesystem;
            fs::path relativePath = vertexDataNode->modelPath;
            ModelHandle handle = g_modelManager->loadModel(relativePath);
            if (g_modelManager->isLoaded(handle)) {
                vertexDataNode->setModel(handle);
            }
        }

        node = std::move(vertexDataNode);

    } else if (type == "ubo") {
        auto uboNode = std::make_unique<UBONode>(id);
        uboNode->fromJson(jNode);

        // Load model via ModelManager if path is set
        if (uboNode->modelPath[0] != '\0') {
            namespace fs = std::filesystem;
            fs::path relativePath = uboNode->modelPath;

            // Store camera/light selection before loading
            int savedCameraIndex = uboNode->selectedCameraIndex;
            int savedLightIndex = uboNode->selectedLightIndex;

            ModelHandle handle = g_modelManager->loadModel(relativePath);
            if (g_modelManager->isLoaded(handle)) {
                uboNode->setModel(handle);
            }

            // Restore selection after model load
            uboNode->selectedCameraIndex = savedCameraIndex;
            uboNode->selectedLightIndex = savedLightIndex;
        }

        node = std::move(uboNode);

    } else if (type == "material") {
        auto materialNode = std::make_unique<MaterialNode>(id);
        materialNode->fromJson(jNode);

        // Load model via ModelManager if path is set
        if (materialNode->modelPath[0] != '\0') {
            namespace fs = std::filesystem;
            fs::path relativePath = materialNode->modelPath;
            ModelHandle handle = g_modelManager->loadModel(relativePath);
            if (g_modelManager->isLoaded(handle)) {
                materialNode->setModel(handle);
            }
        }

        node = std::move(materialNode);

    } else if (type == "present") {
        auto presentNode = std::make_unique<PresentNode>(id);
        presentNode->fromJson(jNode);
        node = std::move(presentNode);

    } else if (type == "orbital_camera") {
        auto cameraNode = std::make_unique<OrbitalCameraNode>(id);
        cameraNode->fromJson(jNode);
        node = std::move(cameraNode);

    } else if (type == "fps_camera") {
        auto cameraNode = std::make_unique<FPSCameraNode>(id);
        cameraNode->fromJson(jNode);
        node = std::move(cameraNode);

    } else if (type == "fixed_camera") {
        auto cameraNode = std::make_unique<FixedCameraNode>(id);
        cameraNode->fromJson(jNode);
        node = std::move(cameraNode);

    } else if (type == "light") {
        auto lightNode = std::make_unique<LightNode>(id);
        lightNode->fromJson(jNode);
        node = std::move(lightNode);

    } else if (type == "multi_model_source") {
        auto sourceNode = std::make_unique<MultiModelSourceNode>(id);
        sourceNode->fromJson(jNode);

        // Load all models via ModelManager
        namespace fs = std::filesystem;
        std::vector<std::pair<std::string, bool>> entriesToLoad;
        for (size_t i = 0; i < sourceNode->getModelCount(); ++i) {
            const ModelEntry& entry = sourceNode->getModel(i);
            if (entry.path[0] != '\0') {
                entriesToLoad.emplace_back(entry.path, entry.enabled);
            }
        }
        // Clear placeholder entries and re-add with loaded handles
        while (sourceNode->getModelCount() > 0) {
            sourceNode->removeModel(0);
        }
        for (const auto& [path, enabled] : entriesToLoad) {
            ModelHandle handle = g_modelManager->loadModel(fs::path(path));
            if (g_modelManager->isLoaded(handle)) {
                sourceNode->addModel(handle);
                sourceNode->setModelEnabled(
                    sourceNode->getModelCount() - 1, enabled);
            }
        }

        node = std::move(sourceNode);

    } else if (type == "multi_vertex_data") {
        // Consumer node - gets data from connected source, no model list
        auto multiVertexNode = std::make_unique<MultiVertexDataNode>(id);
        multiVertexNode->fromJson(jNode);
        node = std::move(multiVertexNode);

    } else if (type == "multi_ubo") {
        // Consumer node - gets data from connected source, no model list
        auto multiUboNode = std::make_unique<MultiUBONode>(id);
        multiUboNode->fromJson(jNode);
        node = std::move(multiUboNode);

    } else if (type == "multi_material") {
        // Consumer node - gets data from connected source, no model list
        auto multiMaterialNode = std::make_unique<MultiMaterialNode>(id);
        multiMaterialNode->fromJson(jNode);
        node = std::move(multiMaterialNode);

    } else {
        Log::warning("NodeFactory", "Unknown node type: {}", type);
    }

    return node;
}

// ============================================================================
// PIPELINE STATE - DESERIALIZATION
// ============================================================================

std::unordered_map<
    int,
    Pin*>
PipelineState::buildPinIdMap(NodeGraph& graph) const {
    std::unordered_map<int, Pin*> pinIdMap;

    for (auto& node : graph.nodes) {
        if (auto* pipeline = dynamic_cast<PipelineNode*>(node.get())) {
            for (auto& binding : pipeline->shaderReflection.bindings) {
                pinIdMap[binding.pin.id.Get()] = &binding.pin;
            }
            for (auto& config :
                 pipeline->shaderReflection.attachmentConfigs) {
                pinIdMap[config.pin.id.Get()] = &config.pin;
            }
            if (pipeline->vertexDataPin.id.Get() != 0) {
                pinIdMap[pipeline->vertexDataPin.id.Get()] =
                    &pipeline->vertexDataPin;
            }
            if (pipeline->hasCameraInput) {
                pinIdMap[pipeline->cameraInput.pin.id.Get()] =
                    &pipeline->cameraInput.pin;
            }
            if (pipeline->hasLightInput) {
                pinIdMap[pipeline->lightInput.pin.id.Get()] =
                    &pipeline->lightInput.pin;
            }
        // ModelNode removed - deprecated
        } else if (auto* vertexData =
                       dynamic_cast<VertexDataNode*>(node.get())) {
            pinIdMap[vertexData->vertexDataPin.id.Get()] =
                &vertexData->vertexDataPin;
        } else if (auto* ubo = dynamic_cast<UBONode*>(node.get())) {
            pinIdMap[ubo->modelMatrixPin.id.Get()] = &ubo->modelMatrixPin;
            pinIdMap[ubo->cameraPin.id.Get()] = &ubo->cameraPin;
            pinIdMap[ubo->lightPin.id.Get()] = &ubo->lightPin;
        } else if (auto* material =
                       dynamic_cast<MaterialNode*>(node.get())) {
            pinIdMap[material->baseColorPin.id.Get()] =
                &material->baseColorPin;
            pinIdMap[material->emissivePin.id.Get()] =
                &material->emissivePin;
            pinIdMap[material->metallicRoughnessPin.id.Get()] =
                &material->metallicRoughnessPin;
            pinIdMap[material->normalPin.id.Get()] = &material->normalPin;
            pinIdMap[material->materialParamsPin.id.Get()] =
                &material->materialParamsPin;
        } else if (auto* present =
                       dynamic_cast<PresentNode*>(node.get())) {
            pinIdMap[present->imagePin.id.Get()] = &present->imagePin;
        } else if (auto* camera =
                       dynamic_cast<CameraNodeBase*>(node.get())) {
            pinIdMap[camera->cameraPin.id.Get()] = &camera->cameraPin;
        } else if (auto* light = dynamic_cast<LightNode*>(node.get())) {
            pinIdMap[light->lightArrayPin.id.Get()] =
                &light->lightArrayPin;
        } else if (auto* source =
                       dynamic_cast<MultiModelSourceNode*>(node.get())) {
            pinIdMap[source->modelSourcePin.id.Get()] =
                &source->modelSourcePin;
        } else if (auto* multiVertex =
                       dynamic_cast<MultiVertexDataNode*>(node.get())) {
            pinIdMap[multiVertex->sourceInputPin.id.Get()] =
                &multiVertex->sourceInputPin;
            pinIdMap[multiVertex->vertexDataPin.id.Get()] =
                &multiVertex->vertexDataPin;
        } else if (auto* multiUbo =
                       dynamic_cast<MultiUBONode*>(node.get())) {
            pinIdMap[multiUbo->sourceInputPin.id.Get()] =
                &multiUbo->sourceInputPin;
            pinIdMap[multiUbo->modelMatrixPin.id.Get()] =
                &multiUbo->modelMatrixPin;
            pinIdMap[multiUbo->cameraPin.id.Get()] = &multiUbo->cameraPin;
            pinIdMap[multiUbo->lightPin.id.Get()] = &multiUbo->lightPin;
        } else if (auto* multiMaterial =
                       dynamic_cast<MultiMaterialNode*>(node.get())) {
            pinIdMap[multiMaterial->sourceInputPin.id.Get()] =
                &multiMaterial->sourceInputPin;
            pinIdMap[multiMaterial->baseColorPin.id.Get()] =
                &multiMaterial->baseColorPin;
            pinIdMap[multiMaterial->emissivePin.id.Get()] =
                &multiMaterial->emissivePin;
            pinIdMap[multiMaterial->metallicRoughnessPin.id.Get()] =
                &multiMaterial->metallicRoughnessPin;
            pinIdMap[multiMaterial->normalPin.id.Get()] =
                &multiMaterial->normalPin;
            pinIdMap[multiMaterial->materialParamsPin.id.Get()] =
                &multiMaterial->materialParamsPin;
        }
    }

    return pinIdMap;
}

void PipelineState::deserializeNodes(
    const nlohmann::json& jNodes,
    NodeGraph& graph,
    ShaderManager& shader_manager,
    int& maxId
) {
    for (const auto& jNode : jNodes) {
        auto node = NodeFactory::createFromJson(
            jNode, graph, shader_manager, maxId
        );
        if (node) {
            graph.nodes.push_back(std::move(node));
        }
    }
}

void PipelineState::deserializeLinks(
    const nlohmann::json& jLinks,
    NodeGraph& graph,
    int& maxId
) {
    auto pinIdMap = buildPinIdMap(graph);

    for (const auto& jLink : jLinks) {
        int linkId = jLink["id"];
        maxId = std::max(maxId, linkId);

        int startId = jLink["startPin"];
        int endId = jLink["endPin"];

        Pin* startPin =
            pinIdMap.count(startId) ? pinIdMap[startId] : nullptr;
        Pin* endPin = pinIdMap.count(endId) ? pinIdMap[endId] : nullptr;

        if (!startPin || !endPin) {
            Log::warning(
                "PipelineState",
                "Could not find pins for link {} (start:{}, end:{})",
                linkId, startId, endId
            );
            continue;
        }

        Link link;
        link.id = ed::LinkId(linkId);
        link.startPin = startPin->id;
        link.endPin = endPin->id;

        graph.addLink(link);
    }
}

bool PipelineState::load(
    NodeGraph& graph,
    const std::string& filePath,
    ShaderManager& shader_manager
) {
    try {
        Log::info("PipelineState", "Loading from: {}", filePath);

        std::ifstream in(filePath);
        if (!in.is_open()) {
            Log::error(
                "PipelineState", "Failed to open file: {}", filePath
            );
            return false;
        }

        nlohmann::json j;
        in >> j;
        in.close();

        graph.clear();

        // CRITICAL: Scan for max ID FIRST, before creating any nodes.
        // This prevents ID conflicts when nodes call GetNextGlobalId()
        // during construction (in createDefaultPins()).
        int maxId = scanJsonForMaxId(j);
        Node::SetNextGlobalId(maxId + 1);
        Log::debug(
            "PipelineState",
            "Pre-scanned JSON: maxId={}, setting global counter to {}",
            maxId, maxId + 1
        );

        if (j.contains("nodes")) {
            Log::debug(
                "PipelineState", "Deserializing {} nodes...",
                j["nodes"].size()
            );
            // Pass maxId for backwards compatibility, but it's already set
            int loadMaxId = maxId;
            deserializeNodes(j["nodes"], graph, shader_manager, loadMaxId);

            // Register all nodes' pins with the central registry
            Log::debug(
                "PipelineState", "Registering pins for {} nodes...",
                graph.nodes.size()
            );
            for (auto& node : graph.nodes) {
                node->registerPins(graph.pinRegistry);
            }
        }

        if (j.contains("links")) {
            Log::debug(
                "PipelineState", "Deserializing {} links...",
                j["links"].size()
            );
            int linkMaxId = maxId;
            deserializeLinks(j["links"], graph, linkMaxId);
        }

        Log::info(
            "PipelineState", "Loaded {} nodes and {} links from: {}",
            graph.nodes.size(), graph.links.size(), filePath
        );

        return true;
    } catch (const std::exception& e) {
        Log::error(
            "PipelineState", "Error loading pipeline state: {}",
            e.what()
        );
        return false;
    }
}
