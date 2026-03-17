#pragma once

#include "node_graph.h"
#include <filesystem>

class VertexDataNode;
class UBONode;
class MaterialNode;
class ShaderManager;

class DefaultRendererSetup {
public:
    /**
     * @brief Creates a default Phong rendering setup for a model.
     *
     * Creates and connects: Camera -> Pipeline -> Present
     * with appropriate links for vertex data, UBO, and materials.
     *
     * @param graph The node graph to add nodes to
     * @param vertexDataNode The vertex data source node
     * @param uboNode Optional UBO node (for GLTF cameras/lights)
     * @param materialNode Optional material node (for textures)
     * @param shaderManager Shader manager for reflection
     * @param projectRoot Project root path
     * @return true if successful
     */
    static bool createForModel(
        NodeGraph& graph,
        VertexDataNode* vertexDataNode,
        UBONode* uboNode,
        MaterialNode* materialNode,
        ShaderManager& shaderManager,
        const std::filesystem::path& projectRoot
    );

    // Default shader directory and filenames (relative to project root)
    static constexpr const char* SHADER_DIR = "shaders";
    static constexpr const char* DEFAULT_VERT_SHADER = "default_phong_vert.slang";
    static constexpr const char* DEFAULT_FRAG_SHADER = "default_phong_frag.slang";
};
