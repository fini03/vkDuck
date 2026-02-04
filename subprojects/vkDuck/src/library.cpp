// vim:foldmethod=marker
#include <vkDuck/library.h>
#include <vulkan/vk_enum_string_helper.h>

// Vulkan result checking {{{
void vkchk_impl(
    const VkResult r,
    const char* file,
    int line,
    const char* func
) {
    if (r != VK_SUCCESS) {
        std::ostringstream oss;
        oss << file << " (" << line << "), " << func << ": " << string_VkResult(r);
        throw std::runtime_error(oss.str());
    }
}
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
    VmaAllocationInfo* pAllocInfo) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = properties;
    allocInfo.flags = vmaFlags;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, pAllocInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }
}

void destroyBuffer(
    VkDevice device,
    VmaAllocator allocator,
    VkBuffer buffer,
    VmaAllocation allocation) {

    vmaDestroyBuffer(allocator, buffer, allocation);
}

void copyBuffer(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize size) {

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(device, graphicsQueue, commandPool, commandBuffer);
}

void MemCopy(
    VkDevice device,
    const void* src,
    VmaAllocationInfo allocInfo,
    VkDeviceSize size) {

    memcpy(allocInfo.pMappedData, src, static_cast<size_t>(size));
}
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
    VmaAllocation& allocation) {

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = properties;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }
}

VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags) {

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view!");
    }

    return imageView;
}

void transitionImageLayout(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout) {

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(device, graphicsQueue, commandPool, commandBuffer);
}

void copyBufferToImage(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height) {

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(device, graphicsQueue, commandPool, commandBuffer);
}
// }}}

// Command buffer utilities {{{
VkCommandBuffer beginSingleTimeCommands(
    VkDevice device,
    VkCommandPool commandPool) {

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(
    VkDevice device,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    VkCommandBuffer commandBuffer) {

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
// }}}

// Batched staging utilities {{{
BatchedBufferCopier::BatchedBufferCopier(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VmaAllocator allocator,
    VkQueue queue,
    VkCommandPool commandPool)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_allocator(allocator)
    , m_queue(queue)
    , m_commandPool(commandPool) {
}

BatchedBufferCopier::~BatchedBufferCopier() {
    // Clean up any pending operations (shouldn't happen normally)
    for (auto& op : m_operations) {
        vmaDestroyBuffer(m_allocator, op.stagingBuffer, op.stagingAlloc);
    }
}

void* BatchedBufferCopier::queueCopy(VkBuffer dstBuffer, VkDeviceSize size) {
    BufferCopyOp op{};
    op.dstBuffer = dstBuffer;
    op.size = size;

    // Create staging buffer
    VmaAllocationInfo allocInfo{};
    createBuffer(m_physicalDevice, m_device, m_allocator,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        op.stagingBuffer, op.stagingAlloc, &allocInfo);

    m_operations.push_back(op);
    return allocInfo.pMappedData;
}

void BatchedBufferCopier::flush() {
    if (m_operations.empty()) return;

    // Allocate command buffer
    VkCommandBuffer cmdBuffer = beginSingleTimeCommands(m_device, m_commandPool);

    // Record all copy commands
    for (const auto& op : m_operations) {
        VkBufferCopy copyRegion{};
        copyRegion.size = op.size;
        vkCmdCopyBuffer(cmdBuffer, op.stagingBuffer, op.dstBuffer, 1, &copyRegion);
    }

    // Single sync point for all copies
    endSingleTimeCommands(m_device, m_queue, m_commandPool, cmdBuffer);

    // Clean up staging buffers
    for (auto& op : m_operations) {
        vmaDestroyBuffer(m_allocator, op.stagingBuffer, op.stagingAlloc);
    }
    m_operations.clear();
}
// }}}

// Shader utilities {{{
std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule createShaderModule(
    VkDevice device,
    const std::vector<char>& code) {

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}
// }}}
