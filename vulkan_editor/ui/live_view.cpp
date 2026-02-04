#include "live_view.h"
#include <vkDuck/library.h>
#include "vulkan_editor/util/logger.h"
#include "vulkan_editor/gpu/primitives.h"
#include <iostream>
#include <ranges>
#include <vector>
#include <vk_mem_alloc.h>

LiveView::LiveView(
    VkDevice device,
    VmaAllocator vma,
    uint32_t queueFamilyIndex,
    VkQueue queue
)
    : device{device}
    , vma{vma}
    , queueFamilyIndex{queueFamilyIndex}
    , queue{queue} {

    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex
    };
    vkchk(
        vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool)
    );

    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    vkchk(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkchk(vkCreateFence(device, &fenceInfo, nullptr, &renderFence));
}

LiveView::~LiveView() {
    vkDeviceWaitIdle(device);

    vkDestroyFence(device, renderFence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);

    destroyOut();
}

void LiveView::recordCommandBuffer() {
    vkchk(
        vkWaitForFences(device, 1, &renderFence, VK_TRUE, UINT64_MAX)
    );
    vkchk(vkResetFences(device, 1, &renderFence));

    vkchk(vkResetCommandBuffer(commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkchk(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    for (auto primitive : orderedPrimitives)
        primitive->recordCommands(store, commandBuffer);

    vkchk(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkchk(vkQueueSubmit(queue, 1, &submitInfo, renderFence));
}

bool LiveView::render(
    uint32_t width,
    uint32_t height
) {
    bool imageRecreated = false;

    if (store.getState() != primitives::StoreState::Linked) {
        return false;
    }

    // Validate we have primitives to render
    if (orderedPrimitives.empty()) {
        Log::warning("LiveView", "No primitives to render");
        return false;
    }

    // Validate we have a valid Present primitive for output
    if (!store.hasValidPresent()) {
        Log::warning("LiveView", "No valid Present primitive - cannot render live view");
        return false;
    }

    // NOTE: Currently we are using the width and height to
    // determine if the whole store was rebuilt, by setting it to 0
    // in that case.
    if (width != outExtent.width || height != outExtent.height) {
        outExtent.width = width;
        outExtent.height = height;
        outExtent.depth = 1;

        destroyOut();

        store.updateSwapchainExtent(outExtent);

        for (auto primitive : orderedPrimitives) {
            if (!primitive->create(store, device, vma)) {
                Log::error("LiveView", "Failed to create primitive - skipping render");
                return false;
            }
        }
        for (auto primitive : orderedPrimitives)
            primitive->stage(device, vma, queue, commandPool);

        imageRecreated = true;
    }

    recordCommandBuffer();
    return imageRecreated;
}

void LiveView::destroyOut() {
    using namespace std::ranges::views;

    // TODO: Other synchronization?
    vkDeviceWaitIdle(device);

    for (auto primitive : orderedPrimitives | reverse)
        primitive->destroy(store, device, vma);

    // Destroy all remaining store resources to prevent GPU memory leaks
    store.destroy(device, vma);
}

VkDescriptorSet LiveView::getImage() {
    if (store.getState() != primitives::StoreState::Linked)
        return VK_NULL_HANDLE;

    return store.getLiveViewImage();
}

primitives::Store& LiveView::getStore() {
    return store;
}