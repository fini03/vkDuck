#pragma once
#include <glm/glm.hpp>

// Shared data structures for model-related nodes
// Used by ModelNode (deprecated), VertexDataNode, UBONode, MaterialNode

struct ModelMatrices {
    alignas(16) glm::mat4 model{1.0f};
    alignas(16) glm::mat4 normalMatrix{1.0f};
};

struct ModelCameraData {
    alignas(16) glm::mat4 view{1.0f};
    alignas(16) glm::mat4 invView{1.0f};
    alignas(16) glm::mat4 proj{1.0f};
    alignas(16) glm::mat4 invProj{1.0f};
};
