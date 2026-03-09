#pragma once

#include <string>
#include <vulkan/vulkan.h>

/**
 * @brief Vertex input attribute information from shader reflection.
 *
 * This struct is separate from shader_types.h to avoid circular dependencies
 * with primitives.h.
 */
struct VertexInputAttribute {
    std::string name;
    std::string semantic;
    std::string typeName;
    uint32_t location = 0;
    uint32_t binding = 0;
    uint32_t offset = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};
