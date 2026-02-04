#pragma once

#include <mutex>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace StagingPool {

struct StagingBuffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    void* mappedData{nullptr};
    VkDeviceSize capacity{0};
    bool inUse{false};
};

class StagingBufferPool {
public:
    StagingBufferPool(VkDevice device, VmaAllocator allocator);
    ~StagingBufferPool();

    // Acquire a staging buffer of at least the requested size
    // Returns a buffer with mappedData ready for writing
    StagingBuffer* acquire(VkDeviceSize minSize);

    // Release buffer back to pool (call after transfer completes)
    void release(StagingBuffer* buffer);

    // Pre-allocate buffers for common sizes
    void warmup();

    // Release unused buffers to reduce memory usage
    void trim();

    // Statistics
    size_t totalBufferCount() const;
    size_t availableBufferCount() const;
    VkDeviceSize totalMemoryUsage() const;

private:
    VkDevice m_device;
    VmaAllocator m_allocator;

    std::vector<StagingBuffer*> m_buffers;
    mutable std::mutex m_mutex;

    StagingBuffer* createBuffer(VkDeviceSize size);
    void destroyBuffer(StagingBuffer* buffer);
};

} // namespace StagingPool
