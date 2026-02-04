#pragma once
#include "../graph/node.h"
#include "../shader/shader_types.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

using namespace ShaderTypes;

class NodeGraph;
class ShaderManager;
struct GlobalSceneConfig;
class Node;

// Factory for creating nodes from JSON
class NodeFactory {
public:
    static std::unique_ptr<Node> createFromJson(
        const nlohmann::json& j,
        NodeGraph& graph,
        ShaderManager& shader_manager,
        int& maxId
    );
};

class PipelineState {
public:
    // Save both pipeline state AND global config
    bool save(
        const NodeGraph& graph,
        const std::string& filePath
    ) const;

    // Load both pipeline state AND global config
    bool load(
        NodeGraph& graph,
        const std::string& filePath,
        ShaderManager& shader_manager
    );

private:
    // Serialization helpers
    nlohmann::json serializeNodes(const NodeGraph& graph) const;
    nlohmann::json serializeLinks(const NodeGraph& graph) const;

    // Deserialization helpers
    void deserializeNodes(
        const nlohmann::json& j,
        NodeGraph& graph,
        ShaderManager& shader_manager,
        int& maxId
    );
    void deserializeLinks(
        const nlohmann::json& j,
        NodeGraph& graph,
        int& maxId
    );

    // Pin ID mapping for link restoration
    std::unordered_map<
        int,
        Pin*>
    buildPinIdMap(NodeGraph& graph) const;
};