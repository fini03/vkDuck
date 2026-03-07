// Centralized Vulkan enum string list implementations
#include "vulkan_enums.h"
#include <vulkan/vk_enum_string_helper.h>

namespace VkEnumConfig {

const std::vector<const char*>& getTopologyStrings() {
    static const auto strings = createEnumStringList(
        topologyOptions, string_VkPrimitiveTopology
    );
    return strings;
}

const std::vector<const char*>& getPolygonModeStrings() {
    static const auto strings = createEnumStringList(
        polygonModes, string_VkPolygonMode
    );
    return strings;
}

const std::vector<const char*>& getCullModeStrings() {
    static const auto strings = createEnumStringList(
        cullModes, string_VkCullModeFlagBits
    );
    return strings;
}

const std::vector<const char*>& getFrontFaceStrings() {
    static const auto strings = createEnumStringList(
        frontFaceOptions, string_VkFrontFace
    );
    return strings;
}

const std::vector<const char*>& getDepthCompareStrings() {
    static const auto strings = createEnumStringList(
        depthCompareOptions, string_VkCompareOp
    );
    return strings;
}

const std::vector<const char*>& getSampleCountStrings() {
    static const auto strings = createEnumStringList(
        sampleCountOptions, string_VkSampleCountFlagBits
    );
    return strings;
}

const std::vector<const char*>& getLogicOpStrings() {
    static const auto strings = createEnumStringList(
        logicOps, string_VkLogicOp
    );
    return strings;
}

const std::vector<const char*>& getColorWriteMaskNames() {
    static const std::vector<const char*> names = {
        "Red", "Green", "Blue", "Alpha"
    };
    return names;
}

} // namespace VkEnumConfig
