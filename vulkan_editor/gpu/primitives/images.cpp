// Image and Attachment primitive implementations
#include "common.h"

namespace primitives {

// ============================================================================
// Image
// ============================================================================

bool Image::create(
    const Store&,
    VkDevice device,
    VmaAllocator vma
) {
    vkchk(vmaCreateImage(
        vma, &imageInfo, &allocInfo, &image, &alloc, nullptr
    ));
    viewInfo.image = image;
    viewInfo.format = imageInfo.format;
    vkchk(vkCreateImageView(device, &viewInfo, nullptr, &view));
    return true;
}

void Image::stage(
    VkDevice device,
    VmaAllocator allocator,
    VkQueue queue,
    VkCommandPool cmdPool
) {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocInfoLocal{};
    VkCommandBuffer cmdBuffer{VK_NULL_HANDLE};

    if (!imageData)
        return;

    {
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = imageSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo allocCreateInfo = {
            .flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };

        vkchk(vmaCreateBuffer(
            allocator, &bufferInfo, &allocCreateInfo, &buffer,
            &allocation, &allocInfoLocal
        ));

        assert(allocInfoLocal.pMappedData != nullptr);
        memcpy(allocInfoLocal.pMappedData, imageData, imageSize);
    }

    {
        VkCommandBufferAllocateInfo cmdBufferAllocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        vkchk(vkAllocateCommandBuffers(
            device, &cmdBufferAllocInfo, &cmdBuffer
        ));

        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    }

    {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = imageInfo.initialLayout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image,
            .subresourceRange = viewInfo.subresourceRange
        };
        VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
        VkPipelineStageFlags dstStageMask =
            VK_PIPELINE_STAGE_TRANSFER_BIT;

        vkCmdPipelineBarrier(
            cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0,
            nullptr, 1, &barrier
        );
    }

    {
        VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {.aspectMask = viewInfo.subresourceRange.aspectMask,
                 .mipLevel = viewInfo.subresourceRange.baseMipLevel,
                 .baseArrayLayer =
                     viewInfo.subresourceRange.baseArrayLayer,
                 .layerCount = viewInfo.subresourceRange.layerCount},
            .imageOffset = {0, 0, 0},
            .imageExtent = imageInfo.extent
        };

        vkCmdCopyBufferToImage(
            cmdBuffer, buffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region
        );
    }

    {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image,
            .subresourceRange = viewInfo.subresourceRange
        };
        VkPipelineStageFlags srcStageMask =
            VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkPipelineStageFlags dstStageMask =
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        vkCmdPipelineBarrier(
            cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0,
            nullptr, 1, &barrier
        );
    }

    {
        vkchk(vkEndCommandBuffer(cmdBuffer));

        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdBuffer
        };

        vkchk(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        vkchk(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
    }

    vmaDestroyBuffer(allocator, buffer, allocation);
}

void Image::destroy(
    const Store&,
    VkDevice device,
    VmaAllocator allocator
) {
    vkDestroyImageView(device, view, nullptr);
    view = VK_NULL_HANDLE;
    vmaDestroyImage(allocator, image, alloc);
    image = VK_NULL_HANDLE;
    alloc = VK_NULL_HANDLE;
}

void Image::updateSwapchainExtent(const VkExtent3D& extent) {
    if (extentType == ExtentType::SwapchainRelative)
        imageInfo.extent = extent;
}

} // namespace primitives
