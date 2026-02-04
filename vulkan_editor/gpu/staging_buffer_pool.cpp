#include "staging_buffer_pool.h"
#include "../util/logger.h"
#include <algorithm>

namespace StagingPool {

// Common buffer sizes for pre-allocation
constexpr VkDeviceSize SMALL_BUFFER_SIZE = 4 * 1024 * 1024;   // 4MB
constexpr VkDeviceSize MEDIUM_BUFFER_SIZE = 16 * 1024 * 1024; // 16MB

StagingBufferPool::StagingBufferPool(VkDevice device, VmaAllocator allocator)
    : m_device(device), m_allocator(allocator) {}

StagingBufferPool::~StagingBufferPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* buffer : m_buffers) {
        destroyBuffer(buffer);
    }
    m_buffers.clear();
}

StagingBuffer* StagingBufferPool::acquire(VkDeviceSize minSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Find an available buffer that's large enough
    for (auto* buffer : m_buffers) {
        if (!buffer->inUse && buffer->capacity >= minSize) {
            buffer->inUse = true;
            return buffer;
        }
    }

    // No suitable buffer found, create a new one
    // Round up to next power of 2 for better reuse
    VkDeviceSize size = minSize;
    if (size < SMALL_BUFFER_SIZE) {
        size = SMALL_BUFFER_SIZE;
    } else {
        // Round up to next power of 2
        size--;
        size |= size >> 1;
        size |= size >> 2;
        size |= size >> 4;
        size |= size >> 8;
        size |= size >> 16;
        size |= size >> 32;
        size++;
    }

    StagingBuffer* newBuffer = createBuffer(size);
    if (newBuffer) {
        newBuffer->inUse = true;
        m_buffers.push_back(newBuffer);
    }
    return newBuffer;
}

void StagingBufferPool::release(StagingBuffer* buffer) {
    if (!buffer)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    buffer->inUse = false;
}

void StagingBufferPool::warmup() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Pre-allocate 2 small and 1 medium buffer
    for (int i = 0; i < 2; ++i) {
        if (auto* buf = createBuffer(SMALL_BUFFER_SIZE)) {
            m_buffers.push_back(buf);
        }
    }
    if (auto* buf = createBuffer(MEDIUM_BUFFER_SIZE)) {
        m_buffers.push_back(buf);
    }

    Log::debug("StagingPool", "Warmed up with {} buffers", m_buffers.size());
}

void StagingBufferPool::trim() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove unused buffers, keeping at least 2
    size_t keepCount = 2;
    size_t available = 0;

    auto it = m_buffers.begin();
    while (it != m_buffers.end()) {
        if (!(*it)->inUse) {
            if (available >= keepCount) {
                destroyBuffer(*it);
                it = m_buffers.erase(it);
                continue;
            }
            ++available;
        }
        ++it;
    }
}

size_t StagingBufferPool::totalBufferCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffers.size();
}

size_t StagingBufferPool::availableBufferCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::count_if(m_buffers.begin(), m_buffers.end(),
                         [](const StagingBuffer* b) { return !b->inUse; });
}

VkDeviceSize StagingBufferPool::totalMemoryUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    VkDeviceSize total = 0;
    for (const auto* buffer : m_buffers) {
        total += buffer->capacity;
    }
    return total;
}

StagingBuffer* StagingBufferPool::createBuffer(VkDeviceSize size) {
    auto* buffer = new StagingBuffer();

    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO};

    VmaAllocationInfo allocationInfo{};
    VkResult result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo,
                                      &buffer->buffer, &buffer->allocation,
                                      &allocationInfo);

    if (result != VK_SUCCESS) {
        Log::error("StagingPool", "Failed to create staging buffer of size {}",
                   size);
        delete buffer;
        return nullptr;
    }

    buffer->mappedData = allocationInfo.pMappedData;
    buffer->capacity = size;
    buffer->inUse = false;

    return buffer;
}

void StagingBufferPool::destroyBuffer(StagingBuffer* buffer) {
    if (buffer) {
        if (buffer->buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, buffer->buffer, buffer->allocation);
        }
        delete buffer;
    }
}

} // namespace StagingPool
