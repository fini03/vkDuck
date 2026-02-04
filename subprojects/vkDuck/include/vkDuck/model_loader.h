// vim:foldmethod=marker
#pragma once

#include <vkDuck/vulkan_base.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <future>
#include <unordered_map>

// Vertex structure for loaded models {{{
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(3);

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};
// }}}

// Model data structures {{{
struct GeometryRange {
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t firstIndex;
    uint32_t indexCount;
    int materialIndex;
};

struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<GeometryRange> ranges;
};
// }}}

// Model loading functions {{{
/// Load a GLTF/GLB model and return all geometry data
/// @param path Path to the model file (.gltf or .glb)
/// @return ModelData containing all vertices, indices, and geometry ranges
ModelData loadModel(const std::string& path);

/// Load multiple models asynchronously in parallel
/// @param paths Vector of paths to model files
/// @return Map of path to ModelData for each loaded model
std::unordered_map<std::string, ModelData> loadModelsAsync(const std::vector<std::string>& paths);

/// Load a specific geometry from pre-loaded model data
/// @param data The pre-loaded model data
/// @param geometryIndex Index of the geometry to load
/// @param outVertices Output vector for vertices
/// @param outIndices Output vector for indices
void loadModelGeometry(
    const ModelData& data,
    uint32_t geometryIndex,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
);
// }}}
