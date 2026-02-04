#pragma once

#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace StagingPool {
class StagingBufferPool;
struct StagingBuffer;
} // namespace StagingPool

namespace BatchedStaging {

struct StagingOperation {
    VkBuffer stagingBuffer{VK_NULL_HANDLE};
    VmaAllocation stagingAllocation{VK_NULL_HANDLE};
    VkCommandBuffer cmdBuffer{VK_NULL_HANDLE};
    StagingPool::StagingBuffer* pooledBuffer{nullptr}; // Non-null if from pool
};

class BatchedStager {
public:
    BatchedStager(VkDevice device, VmaAllocator allocator, VkQueue queue,
                  VkCommandPool cmdPool,
                  StagingPool::StagingBufferPool* pool = nullptr);
    ~BatchedStager();

    // Queue a buffer copy operation (returns staging buffer to copy data into)
    void* queueBufferCopy(VkBuffer dstBuffer, VkDeviceSize size);

    // Queue an image copy operation with layout transitions
    void* queueImageCopy(VkImage dstImage, VkDeviceSize size,
                         const VkExtent3D& extent,
                         const VkImageSubresourceRange& range,
                         VkImageLayout initialLayout);

    // Execute all queued operations with a single sync point
    void flush();

    // Cancel all operations (cleanup without executing)
    void cancel();

    // Get the number of queued operations
    size_t getPendingCount() const { return m_operations.size(); }

private:
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_queue;
    VkCommandPool m_cmdPool;
    StagingPool::StagingBufferPool* m_pool; // Optional staging buffer pool

    std::vector<StagingOperation> m_operations;
    VkFence m_fence{VK_NULL_HANDLE};

    // Helper to acquire staging buffer (from pool or create new)
    void* acquireStagingBuffer(StagingOperation& op, VkDeviceSize size);
    // Helper to release staging buffer (to pool or destroy)
    void releaseStagingBuffer(StagingOperation& op);
};

} // namespace BatchedStaging
