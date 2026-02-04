#pragma once

#include "node_graph.h"
#include <filesystem>

class ModelNode;
class ShaderManager;

class DefaultRendererSetup {
public:
    static bool createForModel(
        NodeGraph& graph,
        ModelNode* modelNode,
        ShaderManager& shaderManager,
        const std::filesystem::path& projectRoot
    );

    // Default shader paths (relative to project root)
    static constexpr const char* DEFAULT_VERT_SHADER = "shaders/default_phong_vert.slang";
    static constexpr const char* DEFAULT_FRAG_SHADER = "shaders/default_phong_frag.slang";
};
