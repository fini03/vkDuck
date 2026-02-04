#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

struct VulkanQueue {
    VkQueue queue;
    uint32_t familyIndex;
};

class VulkanContext {
public:
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkDevice device;
    VulkanQueue graphicsQueue;
    VkDebugUtilsMessengerEXT debugCallback = nullptr;

    // Methods
    static VulkanContext* initVulkan(
        uint32_t instanceExtensionCount,
        const std::vector<const char*> instanceExtensions,
        uint32_t deviceExtensionCount,
        const std::vector<const char*> deviceExtensions,
        bool enableValidationLayers
    );
    void exitVulkan();
};
