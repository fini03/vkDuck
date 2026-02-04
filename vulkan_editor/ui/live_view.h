#pragma once
#include "vulkan_editor/gpu/primitives.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

/**
 * @class LiveView
 * @brief Real-time GPU render preview displayed in an ImGui image widget.
 *
 * Manages off-screen Vulkan rendering with synchronization, providing
 * a descriptor set that can be displayed in ImGui. Automatically handles
 * resize and fence-based GPU synchronization.
 */
class LiveView {
public:
    LiveView(VkDevice device, VmaAllocator vma, uint32_t queueFamilyIndex, VkQueue queue);
    ~LiveView();

    bool render(uint32_t width, uint32_t height);
    VkDescriptorSet getImage();
    primitives::Store& getStore();
    void destroyOut();

    VkExtent3D outExtent{};
    std::vector<primitives::Node*> orderedPrimitives{};

private:
    void recordCommandBuffer();

    VkDevice device;
    VmaAllocator vma;
    uint32_t queueFamilyIndex;
    VkQueue queue;

    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFence renderFence{VK_NULL_HANDLE};

    primitives::Store store{};
};