#include "batched_stager.h"
#include "../util/logger.h"
#include "staging_buffer_pool.h"

#include <cassert>
#include <chrono>
#include <cstring>

#define vkchk(call) \
    do { \
        VkResult result = (call); \
        assert(result == VK_SUCCESS && #call); \
    } while (0)

namespace BatchedStaging {

BatchedStager::BatchedStager(VkDevice device, VmaAllocator allocator,
                             VkQueue queue, VkCommandPool cmdPool,
                             StagingPool::StagingBufferPool* pool)
    : m_device(device)
    , m_allocator(allocator)
    , m_queue(queue)
    , m_cmdPool(cmdPool)
    , m_pool(pool) {
    // Create fence for synchronization
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0
    };
    vkchk(vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence));
}

BatchedStager::~BatchedStager() {
    cancel();
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_fence, nullptr);
    }
}

void* BatchedStager::acquireStagingBuffer(StagingOperation& op, VkDeviceSize size) {
    // Try to get buffer from pool first
    if (m_pool) {
        auto* pooledBuf = m_pool->acquire(size);
        if (pooledBuf) {
            op.pooledBuffer = pooledBuf;
            op.stagingBuffer = pooledBuf->buffer;
            op.stagingAllocation = VK_NULL_HANDLE; // Managed by pool
            return pooledBuf->mappedData;
        }
    }

    // Fallback: create new staging buffer
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    VmaAllocationInfo allocInfo{};
    vkchk(vmaCreateBuffer(m_allocator, &bufferInfo, &allocCreateInfo,
                          &op.stagingBuffer, &op.stagingAllocation, &allocInfo));

    return allocInfo.pMappedData;
}

void BatchedStager::releaseStagingBuffer(StagingOperation& op) {
    if (op.pooledBuffer) {
        // Return to pool
        m_pool->release(op.pooledBuffer);
        op.pooledBuffer = nullptr;
    } else if (op.stagingBuffer != VK_NULL_HANDLE) {
        // Destroy manually created buffer
        vmaDestroyBuffer(m_allocator, op.stagingBuffer, op.stagingAllocation);
    }
    op.stagingBuffer = VK_NULL_HANDLE;
    op.stagingAllocation = VK_NULL_HANDLE;
}

void* BatchedStager::queueBufferCopy(VkBuffer dstBuffer, VkDeviceSize size) {
    StagingOperation op{};

    // Acquire staging buffer (from pool or create new)
    void* mappedData = acquireStagingBuffer(op, size);

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdBufferAllocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    vkchk(vkAllocateCommandBuffers(m_device, &cmdBufferAllocInfo, &op.cmdBuffer));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(op.cmdBuffer, &beginInfo);

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };
    vkCmdCopyBuffer(op.cmdBuffer, op.stagingBuffer, dstBuffer, 1, &copyRegion);

    vkchk(vkEndCommandBuffer(op.cmdBuffer));

    m_operations.push_back(op);

    return mappedData;
}

void* BatchedStager::queueImageCopy(VkImage dstImage, VkDeviceSize size,
                                    const VkExtent3D& extent,
                                    const VkImageSubresourceRange& range,
                                    VkImageLayout initialLayout) {
    StagingOperation op{};

    // Acquire staging buffer (from pool or create new)
    void* mappedData = acquireStagingBuffer(op, size);

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdBufferAllocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    vkchk(vkAllocateCommandBuffers(m_device, &cmdBufferAllocInfo, &op.cmdBuffer));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(op.cmdBuffer, &beginInfo);

    // Transition to transfer destination
    VkImageMemoryBarrier barrier1{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = initialLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dstImage,
        .subresourceRange = range
    };
    vkCmdPipelineBarrier(op.cmdBuffer, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier1);

    // Copy buffer to image
    VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = range.aspectMask,
            .mipLevel = range.baseMipLevel,
            .baseArrayLayer = range.baseArrayLayer,
            .layerCount = range.layerCount
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = extent
    };
    vkCmdCopyBufferToImage(op.cmdBuffer, op.stagingBuffer, dstImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read optimal
    VkImageMemoryBarrier barrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dstImage,
        .subresourceRange = range
    };
    vkCmdPipelineBarrier(op.cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier2);

    vkchk(vkEndCommandBuffer(op.cmdBuffer));

    m_operations.push_back(op);

    return mappedData;
}

void BatchedStager::flush() {
    if (m_operations.empty()) {
        return;
    }

    auto t1 = std::chrono::high_resolution_clock::now();

    // Collect all command buffers
    std::vector<VkCommandBuffer> cmdBuffers;
    cmdBuffers.reserve(m_operations.size());
    for (const auto& op : m_operations) {
        cmdBuffers.push_back(op.cmdBuffer);
    }

    // Submit all at once
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = static_cast<uint32_t>(cmdBuffers.size()),
        .pCommandBuffers = cmdBuffers.data()
    };

    vkResetFences(m_device, 1, &m_fence);
    vkchk(vkQueueSubmit(m_queue, 1, &submitInfo, m_fence));

    // Wait for completion
    vkchk(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX));

    // Cleanup staging resources (return to pool or destroy)
    for (auto& op : m_operations) {
        releaseStagingBuffer(op);
        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &op.cmdBuffer);
    }

    auto t2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = t2 - t1;
    Log::debug("BatchedStager", "Flushed {} operations in {:.1f}ms",
               m_operations.size(), ms.count());

    m_operations.clear();
}

void BatchedStager::cancel() {
    for (auto& op : m_operations) {
        releaseStagingBuffer(op);
        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &op.cmdBuffer);
    }
    m_operations.clear();
}

} // namespace BatchedStaging
