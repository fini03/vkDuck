// vim:foldmethod=marker
#pragma once

#include <vkDuck/vulkan_base.h>
#include <sstream>

// Vulkan result checking {{{
void vkchk_impl(
    const VkResult r,
    const char* file,
    int line,
    const char* func
);

#define vkchk(r) vkchk_impl(r, __FILE__, __LINE__, __func__)
// }}}

// Buffer operations {{{
void createBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VmaAllocationCreateFlags vmaFlags,
    VkBuffer& buffer,
    VmaAllocation& allocation,
    VmaAllocationInfo* pAllocInfo = nullptr
);

void destroyBuffer(
    VkDevice device,
    VmaAllocator allocator,
    VkBuffer buffer,
    VmaAllocation allocation
);

void copyBuffer(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize size
);

void MemCopy(
    VkDevice device,
    const void* src,
    VmaAllocationInfo allocInfo,
    VkDeviceSize size
);
// }}}

// Image operations {{{
void createImage(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VmaAllocator allocator,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& image,
    VmaAllocation& allocation
);

VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags
);

void transitionImageLayout(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
);

void copyBufferToImage(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height
);
// }}}

// Command buffer utilities {{{
VkCommandBuffer beginSingleTimeCommands(
    VkDevice device,
    VkCommandPool commandPool
);

void endSingleTimeCommands(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkCommandBuffer commandBuffer
);
// }}}

// Batched staging utilities {{{
// Batches multiple buffer copies into a single GPU sync for better performance
struct BufferCopyOp {
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VkBuffer dstBuffer;
    VkDeviceSize size;
};

class BatchedBufferCopier {
public:
    BatchedBufferCopier(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        VkQueue queue,
        VkCommandPool commandPool
    );
    ~BatchedBufferCopier();

    // Queue a buffer copy - returns mapped pointer to write data to
    void* queueCopy(VkBuffer dstBuffer, VkDeviceSize size);

    // Execute all queued copies with a single GPU sync
    void flush();

private:
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_queue;
    VkCommandPool m_commandPool;
    std::vector<BufferCopyOp> m_operations;
};
// }}}

// Shader utilities {{{
std::vector<char> readFile(const std::string& filename);

VkShaderModule createShaderModule(
    VkDevice device,
    const std::vector<char>& code
);
// }}}
