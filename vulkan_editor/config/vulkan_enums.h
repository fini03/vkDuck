// Centralized Vulkan enum configuration
// Eliminates duplication between pipeline_node.cpp and model_node.h
#pragma once

#include <array>
#include <vector>
#include <vulkan/vulkan.h>

namespace VkEnumConfig {

// Helper to create string lists from enum arrays
template <typename T, std::size_t N>
std::vector<const char*> createEnumStringList(
    const std::array<T, N>& enumValues,
    const char* (*stringFunc)(T)
) {
    std::vector<const char*> strings;
    strings.reserve(N);
    for (const auto& value : enumValues) {
        strings.push_back(stringFunc(value));
    }
    return strings;
}

// Primitive topology options (used by PipelineNode and ModelNode)
constexpr std::array<VkPrimitiveTopology, 6> topologyOptions{
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
};

// Polygon modes
constexpr std::array<VkPolygonMode, 4> polygonModes{
    VK_POLYGON_MODE_FILL,
    VK_POLYGON_MODE_LINE,
    VK_POLYGON_MODE_POINT,
    VK_POLYGON_MODE_FILL_RECTANGLE_NV
};

// Cull modes
constexpr std::array<VkCullModeFlagBits, 3> cullModes{
    VK_CULL_MODE_NONE,
    VK_CULL_MODE_BACK_BIT,
    VK_CULL_MODE_FRONT_BIT
};

// Front face winding
constexpr std::array<VkFrontFace, 2> frontFaceOptions{
    VK_FRONT_FACE_CLOCKWISE,
    VK_FRONT_FACE_COUNTER_CLOCKWISE
};

// Depth compare operations
constexpr std::array<VkCompareOp, 8> depthCompareOptions{
    VK_COMPARE_OP_NEVER,
    VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_EQUAL,
    VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_GREATER,
    VK_COMPARE_OP_NOT_EQUAL,
    VK_COMPARE_OP_GREATER_OR_EQUAL,
    VK_COMPARE_OP_ALWAYS
};

// Sample count options for multisampling
constexpr std::array<VkSampleCountFlagBits, 7> sampleCountOptions{
    VK_SAMPLE_COUNT_1_BIT,
    VK_SAMPLE_COUNT_2_BIT,
    VK_SAMPLE_COUNT_4_BIT,
    VK_SAMPLE_COUNT_8_BIT,
    VK_SAMPLE_COUNT_16_BIT,
    VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_64_BIT
};

// Logic operations for color blending
constexpr std::array<VkLogicOp, 16> logicOps{
    VK_LOGIC_OP_CLEAR,
    VK_LOGIC_OP_AND,
    VK_LOGIC_OP_AND_REVERSE,
    VK_LOGIC_OP_COPY,
    VK_LOGIC_OP_AND_INVERTED,
    VK_LOGIC_OP_NO_OP,
    VK_LOGIC_OP_XOR,
    VK_LOGIC_OP_OR,
    VK_LOGIC_OP_NOR,
    VK_LOGIC_OP_EQUIVALENT,
    VK_LOGIC_OP_INVERT,
    VK_LOGIC_OP_OR_REVERSE,
    VK_LOGIC_OP_COPY_INVERTED,
    VK_LOGIC_OP_OR_INVERTED,
    VK_LOGIC_OP_NAND,
    VK_LOGIC_OP_SET
};

// String list accessors (initialized once, reused)
const std::vector<const char*>& getTopologyStrings();
const std::vector<const char*>& getPolygonModeStrings();
const std::vector<const char*>& getCullModeStrings();
const std::vector<const char*>& getFrontFaceStrings();
const std::vector<const char*>& getDepthCompareStrings();
const std::vector<const char*>& getSampleCountStrings();
const std::vector<const char*>& getLogicOpStrings();

// Color write mask names (not a Vulkan enum, but commonly needed)
const std::vector<const char*>& getColorWriteMaskNames();

} // namespace VkEnumConfig
