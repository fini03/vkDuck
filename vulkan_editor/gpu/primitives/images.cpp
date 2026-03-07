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

// ============================================================================
// Image - Code Generation
// ============================================================================

using std::print;

void Image::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    // If we have a swapchain image, the image is gonna be created by
    // the project skeleton and not generated by us, because there
    // is gonna be more than one frame in flight
    if (isSwapchainImage) {
        generateCreateSwapchain(store, out);
        return;
    }

    const auto& info = imageInfo;
    print(out, "// Image: {}\n", name);
    print(out, "{{\n");

    // Ensure usage includes at least one flag that allows image view creation
    VkImageUsageFlags usage = info.usage;
    constexpr VkImageUsageFlags validViewUsages =
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if ((usage & validViewUsages) == 0) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    std::string extent;
    if (extentType == ExtentType::SwapchainRelative) {
        extent = "swapChainExtent";
    } else {
        extent = std::format("{{ {}, {}, {} }}", info.extent.width,
                             info.extent.height, info.extent.depth);
    }

    // Generate image create info
    print(out,
        "    VkImageCreateInfo {}_info{{\n"
        "        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,\n"
        "        .imageType = VK_IMAGE_TYPE_2D,\n"
        "        .format = {},\n"
        "        .extent = {},\n"
        "        .mipLevels = {},\n"
        "        .arrayLayers = {},\n"
        "        .samples = {},\n"
        "        .tiling = {},\n"
        "        .usage = {},\n"
        "        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,\n"
        "        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED\n"
        "    }};\n\n",
        name,
        string_VkFormat(info.format),
        extent,
        info.mipLevels, info.arrayLayers,
        string_VkSampleCountFlagBits(info.samples),
        string_VkImageTiling(info.tiling),
        string_VkImageUsageFlags(usage)
    );

    // Generate VMA allocation info
    print(out,
        "    VmaAllocationCreateInfo {}_allocInfo{{\n"
        "        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE\n"
        "    }};\n\n",
        name
    );

    // Generate vmaCreateImage call
    print(out,
        "    vkchk(vmaCreateImage(allocator, &{0}_info, &{0}_allocInfo, &{0}, &{0}_alloc, nullptr));\n\n",
        name
    );

    // Generate image view create info
    bool isDepth = (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    print(out,
        "    VkImageViewCreateInfo {0}_viewInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,\n"
        "        .image = {0},\n"
        "        .viewType = VK_IMAGE_VIEW_TYPE_2D,\n"
        "        .format = {1},\n"
        "        .subresourceRange = {{\n"
        "            .aspectMask = {2},\n"
        "            .baseMipLevel = 0,\n"
        "            .levelCount = {3},\n"
        "            .baseArrayLayer = 0,\n"
        "            .layerCount = {4}\n"
        "        }}\n"
        "    }};\n\n",
        name,
        string_VkFormat(info.format),
        isDepth ? "VK_IMAGE_ASPECT_DEPTH_BIT" : "VK_IMAGE_ASPECT_COLOR_BIT",
        info.mipLevels,
        info.arrayLayers
    );

    // Generate vkCreateImageView call
    print(out,
        "    vkchk(vkCreateImageView(device, &{0}_viewInfo, nullptr, &{0}_view));\n",
        name
    );

    print(out, "}}\n\n");
}

void Image::generateCreateSwapchain(const Store& store, std::ostream& out) const {
    print(out,
        "// Swapchain image view: {0}\n"
        "{{\n"
        "    {0}_views.reserve(swapChainImages.size());\n"
        "    for (const auto& image : swapChainImages) {{\n"
        "        VkImageViewCreateInfo viewInfo{{\n"
        "            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,\n"
        "            .image = image,\n"
        "            .viewType = VK_IMAGE_VIEW_TYPE_2D,\n"
        "            .format = swapChainFormat,\n"
        "            .subresourceRange = {{\n"
        "                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,\n"
        "                .baseMipLevel = 0,\n"
        "                .levelCount = {1},\n"
        "                .baseArrayLayer = 0,\n"
        "                .layerCount = {2}\n"
        "            }}\n"
        "        }};\n"
        "\n"
        "        VkImageView view;\n"
        "        vkchk(vkCreateImageView(device, &viewInfo, nullptr, &view));\n"
        "        {0}_views.push_back(view);\n"
        "    }}\n"
        "}}\n\n",
        name,
        imageInfo.mipLevels,
        imageInfo.arrayLayers);
}

void Image::generateStage(const Store& store, std::ostream& out) const {
    // Skip swapchain images - they don't need staging
    if (isSwapchainImage) return;
    if (name.empty()) return;

    // Check if this image is used as a sampled texture
    bool isSampledTexture = (imageInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
                            (imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;

    if (!isSampledTexture) return;

    // If we have an original image path, use pre-loaded image data
    if (!originalImagePath.empty()) {
        print(out,
            "// Stage texture: {0}\n"
            "{{\n"
            "    // Use pre-loaded image data\n"
            "    auto& {0}_img = loadedImages[\"{1}\"];\n"
            "    if (!{0}_img.valid) {{\n"
            "        throw std::runtime_error(\"Failed to load image: {1}\");\n"
            "    }}\n"
            "    VkDeviceSize {0}_textureSize = {0}_img.width * {0}_img.height * 4;\n"
            "\n"
            "    // Create staging buffer\n"
            "    VkBuffer {0}_stagingBuffer;\n"
            "    VmaAllocation {0}_stagingAlloc;\n"
            "    VmaAllocationInfo {0}_stagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {0}_textureSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {0}_stagingBuffer, {0}_stagingAlloc, &{0}_stagingAllocInfo);\n"
            "    memcpy({0}_stagingAllocInfo.pMappedData, {0}_img.pixels, {0}_textureSize);\n"
            "\n"
            "    // Transition image to transfer destination layout\n"
            "    transitionImageLayout(device, graphicsQueue, commandPool, {0},\n"
            "        {2}, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);\n"
            "\n"
            "    // Copy staging buffer to image\n"
            "    copyBufferToImage(device, graphicsQueue, commandPool, {0}_stagingBuffer, {0},\n"
            "        {0}_img.width, {0}_img.height);\n"
            "\n"
            "    // Transition image to shader read-only layout\n"
            "    transitionImageLayout(device, graphicsQueue, commandPool, {0},\n"
            "        {2}, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);\n"
            "\n"
            "    // Cleanup staging buffer\n"
            "    vmaDestroyBuffer(allocator, {0}_stagingBuffer, {0}_stagingAlloc);\n"
            "}}\n\n",
            name,
            originalImagePath,
            string_VkFormat(imageInfo.format)
        );
    }
    // Fallback: load from binary file (legacy support)
    else if (!imageDataBinPath.empty()) {
        print(out,
            "// Stage texture: {0}\n"
            "{{\n"
            "    // Load texture data from binary file\n"
            "    auto {0}_textureData = readFile(\"{1}\");\n"
            "    VkDeviceSize {0}_textureSize = {0}_textureData.size();\n"
            "\n"
            "    // Create staging buffer\n"
            "    VkBuffer {0}_stagingBuffer;\n"
            "    VmaAllocation {0}_stagingAlloc;\n"
            "    VmaAllocationInfo {0}_stagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {0}_textureSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {0}_stagingBuffer, {0}_stagingAlloc, &{0}_stagingAllocInfo);\n"
            "    memcpy({0}_stagingAllocInfo.pMappedData, {0}_textureData.data(), {0}_textureSize);\n"
            "\n"
            "    // Transition image to transfer destination layout\n"
            "    transitionImageLayout(device, graphicsQueue, commandPool, {0},\n"
            "        {4}, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);\n"
            "\n"
            "    // Copy staging buffer to image\n"
            "    copyBufferToImage(device, graphicsQueue, commandPool, {0}_stagingBuffer, {0},\n"
            "        {2}, {3});\n"
            "\n"
            "    // Transition image to shader read-only layout\n"
            "    transitionImageLayout(device, graphicsQueue, commandPool, {0},\n"
            "        {4}, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);\n"
            "\n"
            "    // Cleanup staging buffer\n"
            "    vmaDestroyBuffer(allocator, {0}_stagingBuffer, {0}_stagingAlloc);\n"
            "}}\n\n",
            name,
            imageDataBinPath,
            imageInfo.extent.width,
            imageInfo.extent.height,
            string_VkFormat(imageInfo.format)
        );
    } else {
        // Even without texture data, we need to transition to shader read-only layout
        // to avoid validation errors when the image is used in a descriptor set
        print(out,
            "// Transition image to shader read-only layout: {0}\n"
            "{{\n"
            "    transitionImageLayout(device, graphicsQueue, commandPool, {0},\n"
            "        {1}, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);\n"
            "}}\n\n",
            name,
            string_VkFormat(imageInfo.format)
        );
    }
}

void Image::generateDestroy(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    // Swapchain images are managed externally in generated code
    if (isSwapchainImage) {
        print(out,
            "    // Destroy Image: {0}\n"
            "    for (auto view : {0}_views) {{\n"
            "        vkDestroyImageView(device, view, nullptr);\n"
            "    }}\n"
            "    {0}_views.clear();\n",
            name);
        return;
    }

    print(out,
        "   // Destroy Image: {0}\n"
        "   if ({0}_view != VK_NULL_HANDLE) {{\n"
        "       vkDestroyImageView(device, {0}_view, nullptr);\n"
        "       {0}_view = VK_NULL_HANDLE;\n"
        "   }}\n"
        "   if ({0} != VK_NULL_HANDLE) {{\n"
        "       vmaDestroyImage(allocator, {0}, {0}_alloc);\n"
        "       {0} = VK_NULL_HANDLE;\n"
        "       {0}_alloc = VK_NULL_HANDLE;\n"
        "   }}\n\n",
        name
    );
}

// ============================================================================
// Attachment - Code Generation
// ============================================================================

void Attachment::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());
    assert(image.isValid());

    const auto& backingImage = store.images[image.handle];
    VkFormat format = backingImage.imageInfo.format;
    VkImageUsageFlags imUsage = backingImage.imageInfo.usage;

    // Compute finalLayout based on backing image usage (mirrors runtime logic)
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    bool isSampled = (imUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
    if (imUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if (imUsage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if (backingImage.isSwapchainImage) {
        finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    } else if (isSampled) {
        finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // If we have a swapchain image, take the externally specified
    // image format
    std::string formatStr;
    if (backingImage.isSwapchainImage)
        formatStr = "swapChainFormat";
    else
        formatStr = string_VkFormat(format);

    print(out, "// Attachment: {}\n", name);
    print(out, "// Backing image: {}\n", backingImage.name);
    print(out,
        "VkAttachmentDescription {}_desc{{\n"
        "    .format = {},\n"
        "    .samples = {},\n"
        "    .loadOp = {},\n"
        "    .storeOp = {},\n"
        "    .stencilLoadOp = {},\n"
        "    .stencilStoreOp = {},\n"
        "    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,\n"
        "    .finalLayout = {}\n"
        "}};\n\n",
        name,
        formatStr,
        string_VkSampleCountFlagBits(desc.samples),
        string_VkAttachmentLoadOp(desc.loadOp),
        string_VkAttachmentStoreOp(desc.storeOp),
        string_VkAttachmentLoadOp(desc.stencilLoadOp),
        string_VkAttachmentStoreOp(desc.stencilStoreOp),
        string_VkImageLayout(finalLayout)
    );
}

} // namespace primitives
