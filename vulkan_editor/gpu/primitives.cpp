#include "primitives.h"
#include "../util/logger.h"
#include "../io/primitive_generator.h"
#include <vulkan/vk_enum_string_helper.h>
#include <vkDuck/library.h>
#include <algorithm>
#include <cassert>
#include <format>
#include <imgui_impl_vulkan.h>
#include <iterator>
#include <print>
#include <ranges>
#include <unordered_set>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace primitives {

namespace {
// Sanitize a name for use as a C++ identifier (replace spaces with underscores, etc.)
std::string sanitizeName(const std::string& name) {
    std::string result = name;
    std::replace(result.begin(), result.end(), ' ', '_');
    return result;
}
} // namespace

bool VertexData::create(
    const Store&,
    VkDevice device,
    VmaAllocator vma
) {
    if (!vertexData.data() || vertexDataSize == 0)
        return false;

    // Create vertex buffer
    {
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vertexDataSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo allocInfo{
            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .priority = 1.0f
        };

        vkchk(vmaCreateBuffer(
            vma, &bufferInfo, &allocInfo, &vertexBuffer,
            &vertexAllocation, nullptr
        ));
    }

    // Create index buffer if we have index data
    if (indexData.data() && indexDataSize > 0) {
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = indexDataSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo allocInfo{
            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .priority = 1.0f
        };

        vkchk(vmaCreateBuffer(
            vma, &bufferInfo, &allocInfo, &indexBuffer,
            &indexAllocation, nullptr
        ));
    }
    return true;
}

void VertexData::stage(
    VkDevice device,
    VmaAllocator allocator,
    VkQueue queue,
    VkCommandPool cmdPool
) {
    if (!vertexData.data() || vertexDataSize == 0)
        return;

    VkCommandBuffer cmdBuffer{VK_NULL_HANDLE};

    // Staging buffers - created upfront, destroyed after single sync
    VkBuffer vertexStagingBuffer{VK_NULL_HANDLE};
    VmaAllocation vertexStagingAllocation{VK_NULL_HANDLE};
    VkBuffer indexStagingBuffer{VK_NULL_HANDLE};
    VmaAllocation indexStagingAllocation{VK_NULL_HANDLE};

    // Allocate command buffer
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

    // Create and fill vertex staging buffer
    {
        VmaAllocationInfo allocInfo{};

        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vertexDataSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo allocCreateInfo{
            .flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };

        vkchk(vmaCreateBuffer(
            allocator, &bufferInfo, &allocCreateInfo, &vertexStagingBuffer,
            &vertexStagingAllocation, &allocInfo
        ));

        assert(allocInfo.pMappedData != nullptr);
        memcpy(allocInfo.pMappedData, vertexData.data(), vertexDataSize);

        VkBufferCopy copyRegion{
            .srcOffset = 0, .dstOffset = 0, .size = vertexDataSize
        };

        vkCmdCopyBuffer(
            cmdBuffer, vertexStagingBuffer, vertexBuffer, 1, &copyRegion
        );
    }

    // Create and fill index staging buffer (if needed)
    if (indexData.data() && indexDataSize > 0) {
        VmaAllocationInfo allocInfo{};

        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = indexDataSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo allocCreateInfo{
            .flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };

        vkchk(vmaCreateBuffer(
            allocator, &bufferInfo, &allocCreateInfo, &indexStagingBuffer,
            &indexStagingAllocation, &allocInfo
        ));

        assert(allocInfo.pMappedData != nullptr);
        memcpy(allocInfo.pMappedData, indexData.data(), indexDataSize);

        VkBufferCopy copyRegion{
            .srcOffset = 0, .dstOffset = 0, .size = indexDataSize
        };

        vkCmdCopyBuffer(
            cmdBuffer, indexStagingBuffer, indexBuffer, 1, &copyRegion
        );
    }

    // Single submit and wait for all transfers
    vkchk(vkEndCommandBuffer(cmdBuffer));

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuffer
    };

    vkchk(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    vkchk(vkQueueWaitIdle(queue));

    // Cleanup all staging buffers after transfer completes
    vmaDestroyBuffer(allocator, vertexStagingBuffer, vertexStagingAllocation);
    if (indexStagingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexStagingBuffer, indexStagingAllocation);
    }

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
}

void VertexData::destroy(
    const Store&,
    VkDevice device,
    VmaAllocator allocator
) {
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
        indexAllocation = VK_NULL_HANDLE;
    }

    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;
    }
}

bool UniformBuffer::create(
    const Store&,
    VkDevice device,
    VmaAllocator vma
) {
    if (!data.data() || data.size() == 0) {
        Log::error(
            "Primitives", "UniformBuffer::create - Invalid data or size"
        );
        return false;
    }

    Log::debug(
        "Primitives", "Creating UniformBuffer with size: {}",
        data.size()
    );

    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = data.size(),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocInfo{
        .flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .priority = 1.0f
    };

    VmaAllocationInfo mappedInfo{};
    vkchk(vmaCreateBuffer(
        vma, &bufferInfo, &allocInfo, &buffer, &allocation, &mappedInfo
    ));

    // Keep it mapped for easy updates
    mapped = mappedInfo.pMappedData;

    if (!mapped) {
        Log::error(
            "Primitives",
            "UniformBuffer::create - Failed to get mapped pointer"
        );
        return false;
    }

    // Initial data copy
    memcpy(mapped, data.data(), data.size());
    Log::debug(
        "Primitives", "UniformBuffer created successfully, buffer={}",
        (void*)buffer
    );
    return true;
}

void UniformBuffer::destroy(
    const Store&,
    VkDevice device,
    VmaAllocator allocator
) {
    if (buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        mapped = nullptr;
    }
}

void UniformBuffer::recordCommands(
    const Store& store,
    VkCommandBuffer cmdBuffer
) const {
    // Check if data actually needs updates
    switch (dataType) {
    case UniformDataType::Camera:
        assert(extraData != nullptr);
        if (auto type = reinterpret_cast<const CameraType*>(extraData);
            *type == CameraType::Fixed) {
            return;
        }
        break;
    case UniformDataType::Light:
        // Fixed lights don't need runtime updates
        return;
    case UniformDataType::Other:
        return;
    }

    // Assumes a mapped buffer, otherwise stage
    memcpy(mapped, data.data(), data.size());
}

void Camera::recordCommands(
    const Store& store,
    VkCommandBuffer cmdBuffer
) const {
    // Fixed cameras don't need runtime UBO updates
    if (isFixed())
        return;

    assert(ubo.isValid() && "Camera must have a valid UBO handle");
    assert(ubo.handle < store.uniformBuffers.size() && "UBO handle out of bounds");

    auto& uniformBuffer = store.uniformBuffers[ubo.handle];
    assert(uniformBuffer.mapped != nullptr && "Camera UBO must be mapped");
    assert(!uniformBuffer.data.empty() && "Camera UBO data must not be empty");

    memcpy(uniformBuffer.mapped, uniformBuffer.data.data(), uniformBuffer.data.size());
}

void Camera::generateRecordCommands(
    const Store& store,
    std::ostream& out
) const {
    // Fixed cameras don't need runtime UBO updates
    if (isFixed())
        return;

    // Validate camera state
    assert(!name.empty() && "Camera must have a name for code generation");
    assert(ubo.isValid() && "Camera must have a valid UBO handle");
    assert(ubo.handle < store.uniformBuffers.size() && "UBO handle out of bounds");

    const auto& uniformBuffer = store.uniformBuffers[ubo.handle];
    assert(!uniformBuffer.name.empty() && "Camera UBO must have a name for code generation");

    // Generate code to update the camera UBO
    std::string safeName = sanitizeName(name);
    print(out,
        "    // Update camera UBO: {}\n"
        "    updateCameraUBO({}_mapped, {});\n\n",
        name, uniformBuffer.name, safeName
    );
}

void Light::recordCommands(
    const Store& store,
    VkCommandBuffer cmdBuffer
) const {
    // Fixed lights - just update the UBO with current data
    if (!ubo.isValid())
        return;

    auto& uniformBuffer = store.uniformBuffers[ubo.handle];
    memcpy(uniformBuffer.mapped, uniformBuffer.data.data(), uniformBuffer.data.size());
}

void Light::generateRecordCommands(
    const Store& store,
    std::ostream& out
) const {
    // Fixed lights don't need runtime updates - data is static
    // If we wanted dynamic lights, we'd generate update code here
}

bool DescriptorPool::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (sets.empty()) {
        Log::error("Primitives", "DescriptorPool: Requested pool without sets");
        return false;
    }

    auto types = std::to_array<VkDescriptorPoolSize>({
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0},
    });

    uint32_t totalSets = 0;
    for (StoreHandle hSet : sets) {
        if (!hSet.isValid()) {
            Log::error("Primitives", "DescriptorPool: Invalid set handle");
            return false;
        }
        const DescriptorSet& set = store.descriptorSets[hSet.handle];
        auto contrib = set.getPoolSizeContribution(store);

        totalSets += contrib.setCount;
        types[0].descriptorCount += contrib.imageCount;
        types[1].descriptorCount += contrib.uniformBufferCount;
    }

    VkDescriptorPoolCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = totalSets,
        .poolSizeCount = types.size(),
        .pPoolSizes = types.data()
    };

    vkchk(vkCreateDescriptorPool(device, &info, nullptr, &pool));
    return true;
}

void DescriptorPool::destroy(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
}

void DescriptorPool::registerSet(StoreHandle set) {
    sets.push_back(set);
}

VkDescriptorPool DescriptorPool::getPool() const {
    return pool;
}

const std::vector<StoreHandle>& DescriptorPool::getSets() const {
    return sets;
}

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
    VmaAllocationInfo allocInfo{};
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
            &allocation, &allocInfo
        ));

        assert(allocInfo.pMappedData != nullptr);
        memcpy(allocInfo.pMappedData, imageData, imageSize);
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

bool DescriptorSet::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (!pool.isValid()) {
        Log::error("Primitives", "DescriptorSet: Invalid pool handle");
        return false;
    }
    if (bindings.size() != expectedBindings.size()) {
        Log::error("Primitives", "DescriptorSet: Bindings size mismatch (expected {}, got {})",
                   expectedBindings.size(), bindings.size());
        return false;
    }
    const DescriptorPool& poolNode = store.descriptorPools[pool.handle];
    VkDescriptorPool pool = poolNode.getPool();

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings{};
    layoutBindings.reserve(expectedBindings.size());

    for (const DescriptorInfo& info : expectedBindings) {
        switch (info.type) {
        case Type::Image:
            layoutBindings.push_back(
                {.binding = info.binding,
                 .descriptorType =
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .descriptorCount = info.arrayCount,
                 .stageFlags = info.stages}
            );
            break;
        case Type::UniformBuffer:
        case Type::Camera:
            layoutBindings.push_back(
                {.binding = info.binding,
                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .descriptorCount = info.arrayCount,
                 .stageFlags = info.stages}
            );
            break;
        default:
            Log::error(
                "Primitives", "Unsupported binding type {}",
                static_cast<unsigned int>(info.type)
            );
            return false;
        }
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(layoutBindings.size()),
        .pBindings = layoutBindings.data()
    };

    vkchk(vkCreateDescriptorSetLayout(
        device, &layoutInfo, nullptr, &layout
    ));

    uint32_t numSets = cardinality(store);
    if (numSets == 0) {
        Log::error("Primitives", "DescriptorSet: Zero cardinality");
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts{numSets, layout};
    VkDescriptorSetAllocateInfo setInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = numSets,
        .pSetLayouts = layouts.data()
    };
    sets.resize(numSets, VK_NULL_HANDLE);
    vkchk(vkAllocateDescriptorSets(device, &setInfo, sets.data()));

    auto fullBindings = std::views::zip(expectedBindings, bindings);
    for (const auto& [info, handle] : fullBindings) {
        // NOTE: We check (and assert) that the handle is an array
        // when we calculate the cardinality above. We also assert
        // the cardinality of the different arrays with the cardinality
        // calculation.
        const Array& array = store.arrays[handle.handle];
        // Camera arrays are compatible with UniformBuffer bindings
        bool typeMatches = (array.type == info.type) ||
                           (array.type == Type::Camera && info.type == Type::UniformBuffer);
        if (!typeMatches) {
            Log::error("Primitives", "DescriptorSet: Array type mismatch (got {}, expected {})",
                       static_cast<uint32_t>(array.type), static_cast<uint32_t>(info.type));
            return false;
        }

        if (array.type == Type::Image) {
            // Allocate samplers for image descriptors.
            // TODO: For the future, can we specify the samplers in
            //       the UI? Can we manage the samplers so that we
            //       don't have to create a new one for every
            //       descriptor?
            samplers.emplace_back(VK_NULL_HANDLE);
            vkchk(vkCreateSampler(
                device, &info.samplerInfo, nullptr, &samplers.back()
            ));

            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            // We NEED to reserve the imageInfos vector upfront,
            // because we will save pointers into it and we cannot
            // have it relocating memory under our butt
            imageInfos.reserve(numSets);
            descriptorWrites.reserve(numSets);

            auto handleSets = std::views::zip(array.handles, sets);
            for (const auto& [hImage, set] : handleSets) {
                const Image& image = store.images[hImage];
                imageInfos.push_back(
                    {.sampler = samplers.back(),
                     .imageView = image.view,
                     .imageLayout =
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                );
                descriptorWrites.push_back(
                    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                     .dstSet = set,
                     .dstBinding = info.binding,
                     .dstArrayElement = 0,
                     .descriptorCount = 1,
                     .descriptorType =
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                     .pImageInfo = &imageInfos.back()}
                );
            }

            vkUpdateDescriptorSets(
                device, static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(), 0, nullptr
            );
        } else if (array.type == Type::UniformBuffer) {
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            bufferInfos.reserve(numSets);
            descriptorWrites.reserve(numSets);

            auto handleSets = std::views::zip(array.handles, sets);
            for (const auto& [hUBO, set] : handleSets) {
                const UniformBuffer& ubo = store.uniformBuffers[hUBO];
                bufferInfos.push_back(
                    {.buffer = ubo.buffer,
                     .offset = 0,
                     .range = ubo.data.size()}
                );
                descriptorWrites.push_back(
                    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                     .dstSet = set,
                     .dstBinding = info.binding,
                     .dstArrayElement = 0,
                     .descriptorCount = 1,
                     .descriptorType =
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     .pBufferInfo = &bufferInfos.back()}
                );
            }

            vkUpdateDescriptorSets(
                device, static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(), 0, nullptr
            );
        } else if (array.type == Type::Camera) {
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            bufferInfos.reserve(numSets);
            descriptorWrites.reserve(numSets);

            auto handleSets = std::views::zip(array.handles, sets);
            for (const auto& [hCamera, set] : handleSets) {
                const Camera& camera = store.cameras[hCamera];
                const UniformBuffer& ubo = store.uniformBuffers[camera.ubo.handle];
                bufferInfos.push_back(
                    {.buffer = ubo.buffer,
                     .offset = 0,
                     .range = ubo.data.size()}
                );
                descriptorWrites.push_back(
                    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                     .dstSet = set,
                     .dstBinding = info.binding,
                     .dstArrayElement = 0,
                     .descriptorCount = 1,
                     .descriptorType =
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     .pBufferInfo = &bufferInfos.back()}
                );
            }

            vkUpdateDescriptorSets(
                device, static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(), 0, nullptr
            );
        } else {
            Log::error("Primitives", "DescriptorSet: Unsupported array type");
            return false;
        }
    }
    return true;
}

void DescriptorSet::destroy(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    // NOTE: We do not free the descriptor sets individually but we
    // rely on the pool to reset and release all allocated descriptor
    // sets
    //
    // assert(pool.isValid());
    // vkFreeDescriptorSets(
    //     device,
    //     store.descriptorPools[pool.handle].getPool(),
    //     1, &set
    // );

    for (VkSampler sampler : samplers)
        vkDestroySampler(device, sampler, nullptr);
    samplers.clear();
    buffers.clear();

    sets.clear();
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    layout = VK_NULL_HANDLE;
}

bool DescriptorSet::connectLink(
    const LinkSlot& slot,
    Store& store
) {
    size_t maxSlots = expectedBindings.size();

    // Validate slot handle is valid
    if (!slot.handle.isValid()) {
        Log::error("Primitives", "DescriptorSet: Invalid slot handle");
        return false;
    }

    // Resize the bindings if they are not yet the expected size
    if (bindings.size() != maxSlots)
        bindings.resize(maxSlots);

    if (slot.slot >= maxSlots) {
        Log::error(
            "Primitives", "DescriptorSet: Invalid slot {}", slot.slot
        );
        return false;
    }

    if (slot.handle.type != Type::Array) {
        Log::error("Primitives", "DescriptorSet: Expected array type");
        return false;
    }

    const Array& array = store.arrays[slot.handle.handle];
    primitives::Type expectedType = expectedBindings[slot.slot].type;

    // Camera arrays are compatible with UniformBuffer bindings
    // since Camera is essentially a specialized UniformBuffer
    bool typeMatches = (array.type == expectedType) ||
                       (array.type == Type::Camera && expectedType == Type::UniformBuffer);

    if (!typeMatches) {
        Log::error(
            "Primitives",
            "DescriptorSet: Unexpected type {} for slot {} (expected {})",
            static_cast<uint32_t>(array.type),
            slot.slot,
            static_cast<uint32_t>(expectedType)
        );
        return false;
    }

    if (array.handles.empty()) {
        Log::error(
            "Primitives",
            "DescriptorSet: Got empty array, did you load a model?"
        );
        return false;
    }

    bindings[slot.slot] = slot.handle;

    // Special case for image primitives: If these are passed to
    // another node as input, we add sampling to the image usage
    //
    // TODO: Do we really need to do this here or is there a
    //       better place to put this? It would be nice if we
    //       didn't need a mutable reference of store here
    if (array.type == Type::Image) {
        for (auto hImage : array.handles) {
            Image& imageObj = store.images[hImage];
            imageObj.imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
    }

    return true;
}

const std::vector<VkDescriptorSet>& DescriptorSet::getSets() const {
    return sets;
}

const std::vector<StoreHandle>& DescriptorSet::getBindings() const {
    return bindings;
}

/// All inputs to the descriptor set are arrays, and we need to
/// figure out the shared cardinality of these arrays. For
/// constant/global descriptor sets, the cardinality is 1,
/// otherwise it (currently) is the number of objects to be
/// rendered
uint32_t DescriptorSet::cardinality(const Store& store) const {
    uint32_t numSets = 0;
    for (StoreHandle handle : bindings) {
        assert(handle.isValid());
        assert(handle.type == Type::Array);

        const Array& array = store.arrays[handle.handle];
        assert(array.type != Type::Invalid);
        uint32_t size = array.handles.size();

        if (numSets != 0)
            assert(size == numSets);
        else
            numSets = size;
    }
    return numSets;
}

VkDescriptorSetLayout DescriptorSet::getLayout() const {
    return layout;
}

PoolSizeContribution DescriptorSet::getPoolSizeContribution(
    const Store& store,
    uint32_t cardinalityOverride
) const {
    PoolSizeContribution contrib{};
    uint32_t calculatedCardinality = cardinality(store);
    // Use override if provided, otherwise use calculated cardinality, minimum 1
    contrib.setCount = cardinalityOverride > 0 ? cardinalityOverride
                     : (calculatedCardinality > 0 ? calculatedCardinality : 1);

    for (const auto& binding : expectedBindings) {
        switch (binding.type) {
        case Type::Image:
            contrib.imageCount += contrib.setCount;
            break;
        case Type::UniformBuffer:
        case Type::Camera:
            contrib.uniformBufferCount += contrib.setCount;
            break;
        default:
            break;
        }
    }

    return contrib;
}

bool Shader::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (code.empty()) {
        Log::error("Primitives", "Shader: Empty shader code");
        return false;
    }
    if (stage == VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM) {
        Log::error("Primitives", "Shader: Invalid shader stage");
        return false;
    }

    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data(),
    };

    vkchk(vkCreateShaderModule(device, &createInfo, nullptr, &module));
    return true;
}

void Shader::destroy(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    vkDestroyShaderModule(device, module, nullptr);
    module = VK_NULL_HANDLE;
};

std::filesystem::path Shader::getSpirvPath() const {
    std::filesystem::path shaderPath{name};

    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        shaderPath.replace_extension(".vert.spv");
        break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        shaderPath.replace_extension(".frag.spv");
        break;
    default:
        std::unreachable();
    }

    return shaderPath;
}

bool Pipeline::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (!renderPass.isValid()) {
        Log::error("Pipeline", "Invalid render pass handle");
        return false;
    }
    if (shaders.empty()) {
        Log::error("Pipeline", "No shaders");
        return false;
    }
    const RenderPass& rp = store.renderPasses[renderPass.handle];

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(shaders.size());
    for (StoreHandle hShader : shaders) {
        if (!hShader.isValid()) {
            Log::error("Pipeline", "Invalid shader handle");
            return false;
        }
        const Shader& shader = store.shaders[hShader.handle];
        if (shader.module == VK_NULL_HANDLE) {
            Log::error("Pipeline", "Shader module not created");
            return false;
        }

        shaderStages.push_back(
            {.sType =
                 VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = shader.stage,
             .module = shader.module,
             .pName = shader.entryPoint.c_str()}
        );
    }

    VkVertexInputBindingDescription bindingDescription{};
    std::vector<VkVertexInputAttributeDescription>
        attributeDescriptions;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    // Check if we have vertex data connected
    if (vertexDataHandle.isValid()) {
        if (vertexDataHandle.type != Type::Array) {
            Log::error("Pipeline", "Vertex data is not an array");
            return false;
        }
        const Array& vertexArray =
            store.arrays[vertexDataHandle.handle];
        if (vertexArray.type != Type::VertexData) {
            Log::error("Pipeline", "Vertex array is not VertexData type");
            return false;
        }
        if (vertexArray.handles.empty()) {
            Log::error("Pipeline", "Vertex array is empty");
            return false;
        }

        // Get vertex input description from the first vertex data
        // (assuming all geometries have the same vertex format)
        const VertexData& vertexData =
            store.vertexDatas[vertexArray.handles[0]];

        bindingDescription = vertexData.bindingDescription;
        attributeDescriptions = vertexData.attributeDescriptions;

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions =
            &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions =
            attributeDescriptions.data();

        Log::debug(
            "Primitives",
            "Pipeline: Using vertex input with {} attributes",
            attributeDescriptions.size()
        );
    }

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr, // Set through dynamic state
        .scissorCount = 1,
        .pScissors = nullptr // Set through dynamic state
    };

    // Get color blending info for all attachments
    std::vector<VkPipelineColorBlendAttachmentState> attachmentBlends;
    attachmentBlends.reserve(rp.attachments.size());
    bool hasDepth{false};
    for (StoreHandle hAttachment : rp.attachments) {
        assert(hAttachment.isValid());
        const Attachment& a = store.attachments[hAttachment.handle];
        assert(a.image.isValid());
        const Image& backingImage = store.images[a.image.handle];
        VkImageUsageFlags usage = backingImage.imageInfo.usage;

        // Just check if we have a depth attachment so we can disable
        // setting the depth stencil creation state
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            hasDepth = true;

        // If this is not a color attachment, ignore
        if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            continue;

        attachmentBlends.push_back(a.colorBlending);
    }
    colorBlending.attachmentCount = attachmentBlends.size();
    colorBlending.pAttachments = attachmentBlends.data();

    // Currently we can't control these from the UI whatsoever
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount =
            static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    size_t numDescriptorSets = descriptorSetHandles.size();
    std::vector<VkDescriptorSetLayout> dsLayouts;
    dsLayouts.reserve(numDescriptorSets);
    auto descriptorIt = descriptorSetHandles.begin();

    // Collect global descriptor sets
    for (; descriptorIt != descriptorSetHandles.end(); ++descriptorIt) {
        if (!descriptorIt->isValid()) {
            // Skip invalid descriptor set handles - don't add null handles
            Log::warning("Pipeline", "Skipping invalid global descriptor set handle");
            continue;
        }

        if (descriptorIt->type != Type::DescriptorSet) {
            Log::warning("Pipeline", "Skipping descriptor with wrong type");
            continue;
        }
        const auto& ds = store.descriptorSets[descriptorIt->handle];

        // Global descriptor sets should only have one set
        const auto& sets = ds.getSets();
        if (sets.empty()) {
            Log::warning("Pipeline", "Skipping empty descriptor set");
            continue;
        }
        if (sets.size() != 1)
            break;

        dsLayouts.push_back(ds.getLayout());
        globalDescriptorSets.append_range(sets);
    }

    // Per-object descriptor sets. We first collect all descriptor sets
    // into one contiguous array so that we don't have to do the lookup
    // across store handles multiple times.
    auto numObjSets =
        std::distance(descriptorIt, descriptorSetHandles.end());
    if (numObjSets < 0) {
        Log::warning("Pipeline", "Invalid per-object descriptor set count");
        numObjSets = 0;
    }
    size_t numObj = 0;
    std::vector<VkDescriptorSet> allSets;
    for (; descriptorIt != descriptorSetHandles.end(); ++descriptorIt) {
        // Skip invalid descriptor set handles
        if (!descriptorIt->isValid()) {
            Log::warning("Pipeline", "Skipping invalid per-object descriptor set handle");
            continue;
        }

        if (descriptorIt->type != Type::DescriptorSet) {
            Log::warning("Pipeline", "Skipping per-object descriptor with wrong type");
            continue;
        }
        const auto& ds = store.descriptorSets[descriptorIt->handle];

        const auto& sets = ds.getSets();
        if (sets.size() <= 1) {
            Log::warning("Pipeline", "Skipping per-object descriptor set with insufficient sets");
            continue;
        }

        if (numObj == 0) {
            numObj = sets.size();
            allSets.reserve(numObj * numObjSets);
        }

        if (numObj != sets.size()) {
            Log::warning("Pipeline", "Per-object descriptor set size mismatch");
            continue;
        }
        allSets.append_range(sets);
        dsLayouts.push_back(ds.getLayout());
    }

    // Split the per-object descriptor set array into the dimensions
    // of the actual sets now.
    // NOTE: We can't use std::views::enumerate on Xcode clang atm,
    //       so we instead use zip and iota(0)
    perObjectDescriptorSets.resize(numObj);
    auto objSetEnum =
        std::views::zip(std::views::iota(0), perObjectDescriptorSets);
    for (auto&& [objIdx, objSetRange] : objSetEnum) {
        objSetRange.resize(numObjSets, VK_NULL_HANDLE);
        auto setEnum =
            std::views::zip(std::views::iota(0), objSetRange);
        for (auto&& [setIdx, set] : setEnum)
            set = allSets[setIdx * numObj + objIdx];
    }
    allSets.clear();

    VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(dsLayouts.size()),
        .pSetLayouts = dsLayouts.empty() ? nullptr : dsLayouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };

    vkchk(vkCreatePipelineLayout(
        device, &layoutInfo, nullptr, &pipelineLayout
    ));

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = hasDepth ? &depthStencil : nullptr,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = rp.renderPass,

        // NOTE: We currently only support one subpass per render pass,
        // so this is always subpass 0 for now
        .subpass = 0
    };

    vkchk(vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline
    ));
    return true;
}

void Pipeline::destroy(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
    globalDescriptorSets.clear();
    perObjectDescriptorSets.clear();
}

void Pipeline::recordCommands(
    const Store& store,
    VkCommandBuffer cmdBuffer
) const {
    // Skip rendering if pipeline is not properly initialized
    if (!renderPass.isValid()) {
        Log::warning("Pipeline", "Skipping render: invalid render pass handle");
        return;
    }
    if (pipeline == VK_NULL_HANDLE) {
        Log::warning("Pipeline", "Skipping render: pipeline not created");
        return;
    }
    if (pipelineLayout == VK_NULL_HANDLE) {
        Log::warning("Pipeline", "Skipping render: pipeline layout not created");
        return;
    }

    // Validate global descriptor sets don't contain null handles
    for (size_t i = 0; i < globalDescriptorSets.size(); ++i) {
        if (globalDescriptorSets[i] == VK_NULL_HANDLE) {
            Log::warning(
                "Pipeline",
                "Skipping render: global descriptor set {} is null",
                i
            );
            return;
        }
    }

    const RenderPass& rp = store.renderPasses[renderPass.handle];

    // NOTE: Because we only support to 1 : 1 pipeline renderpass
    // matching now and we don't support multiple pipelines/subpasses
    // right now at all, we are recording the render pass commands
    // here instead of in the renderpass primitive.

    // NOTE: In terms of render area, we don't really support anything
    // different then just having the same render area for everything
    // now.

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = rp.renderPass;
    renderPassInfo.framebuffer = rp.framebuffer;
    renderPassInfo.renderArea = rp.renderArea;
    renderPassInfo.clearValueCount = rp.clearValues.size();
    renderPassInfo.pClearValues = rp.clearValues.data();

    vkCmdBeginRenderPass(
        cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE
    );

    if (!globalDescriptorSets.empty()) {
        vkCmdBindDescriptorSets(
            cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            0, static_cast<uint32_t>(globalDescriptorSets.size()),
            globalDescriptorSets.data(), 0, nullptr
        );
    }

    VkViewport viewport{};
    viewport.x = rp.renderArea.offset.x;
    viewport.y = rp.renderArea.offset.y;
    viewport.width = rp.renderArea.extent.width;
    viewport.height = rp.renderArea.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {rp.renderArea.offset.x, rp.renderArea.offset.y};
    scissor.extent = {
        rp.renderArea.extent.width, rp.renderArea.extent.height
    };
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(
        cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline
    );

    // If we don't have vertex data, just draw a screen triangle
    if (!vertexDataHandle.isValid()) {
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmdBuffer);
        return;
    }

    // Validate vertex data handle type
    if (vertexDataHandle.type != Type::Array) {
        Log::warning("Pipeline", "Skipping render: vertex data handle is not an array");
        vkCmdEndRenderPass(cmdBuffer);
        return;
    }
    const Array& vertexArray = store.arrays[vertexDataHandle.handle];
    if (vertexArray.type != Type::VertexData) {
        Log::warning("Pipeline", "Skipping render: vertex array is not VertexData type");
        vkCmdEndRenderPass(cmdBuffer);
        return;
    }
    auto vertices =
        vertexArray.handles |
        std::views::transform([store](auto handle) -> const auto& {
            return store.vertexDatas[handle];
        });

    auto drawVertices = [cmdBuffer](const auto& vdata) {
        // Skip if vertex buffer is invalid
        if (vdata.vertexBuffer == VK_NULL_HANDLE) {
            Log::warning("Pipeline", "Skipping draw: vertex buffer is null");
            return;
        }

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {vdata.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);

        // Draw without indices
        if (vdata.indexBuffer == VK_NULL_HANDLE) {
            vkCmdDraw(cmdBuffer, vdata.vertexCount, 1, 0, 0);
            return;
        }

        // Draw indexed
        vkCmdBindIndexBuffer(
            cmdBuffer, vdata.indexBuffer, 0, VK_INDEX_TYPE_UINT32
        );
        vkCmdDrawIndexed(cmdBuffer, vdata.indexCount, 1, 0, 0, 0);
    };

    if (perObjectDescriptorSets.empty()) {
        std::for_each(vertices.begin(), vertices.end(), drawVertices);
    } else {
        // Validate per-object descriptor sets match vertex count
        if (perObjectDescriptorSets.size() !=
            static_cast<size_t>(std::ranges::distance(vertices))) {
            Log::warning(
                "Pipeline",
                "Skipping render: per-object descriptor sets count mismatch"
            );
            vkCmdEndRenderPass(cmdBuffer);
            return;
        }
        auto combinedGeometry =
            std::views::zip(vertices, perObjectDescriptorSets);
        for (auto&& [vertexData, objSets] : combinedGeometry) {
            // Skip if object has no descriptor sets
            if (objSets.empty()) {
                Log::warning("Pipeline", "Skipping object: empty descriptor set");
                continue;
            }
            // Validate per-object descriptor sets don't contain null handles
            bool hasNullDescriptor = false;
            for (size_t i = 0; i < objSets.size(); ++i) {
                if (objSets[i] == VK_NULL_HANDLE) {
                    Log::warning(
                        "Pipeline",
                        "Skipping object: per-object descriptor set {} is null",
                        i
                    );
                    hasNullDescriptor = true;
                    break;
                }
            }
            if (hasNullDescriptor) {
                continue;
            }

            vkCmdBindDescriptorSets(
                cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                static_cast<uint32_t>(globalDescriptorSets.size()),
                static_cast<uint32_t>(objSets.size()), objSets.data(),
                0, nullptr
            );

            drawVertices(vertexData);
        }
    }

    // NOTE: Do this in renderpass primitive in the future
    vkCmdEndRenderPass(cmdBuffer);
}

bool Pipeline::connectLink(
    const LinkSlot& slot,
    Store& store
) {
    // Validate slot handle is valid
    if (!slot.handle.isValid()) {
        Log::error("Primitives", "Pipeline: Invalid slot handle");
        return false;
    }

    // For now, we only handle vertex data input on slot 0
    if (slot.slot != 0) {
        Log::error(
            "Primitives", "Pipeline: Invalid slot {}", slot.slot
        );
        return false;
    }

    if (slot.handle.type != Type::Array) {
        Log::error("Primitives", "Pipeline: Expected array type");
        return false;
    }

    const Array& array = store.arrays[slot.handle.handle];
    if (array.type != Type::VertexData) {
        Log::error("Primitives", "Pipeline: Expected VertexData array");
        return false;
    }

    if (array.handles.empty()) {
        Log::error(
            "Primitives", "Pipeline: Got empty vertex data array"
        );
        return false;
    }

    vertexDataHandle = slot.handle;

    Log::debug(
        "Primitives",
        "Pipeline: Connected vertex data array with {} geometries",
        array.handles.size()
    );

    return true;
}

bool RenderPass::create(
    const Store& store,
    VkDevice device,
    VmaAllocator
) {
    if (attachments.empty()) {
        Log::error("RenderPass", "No attachments");
        return false;
    }

    bool depthInput{false};
    bool colorInput{false};

    // TODO: We should let the user decide if they want to specify a
    // specific width for the framebuffer/renderpass OR if they want
    // to relate the width/height to the images.
    uint32_t minHeight = UINT32_MAX;
    uint32_t minWidth = UINT32_MAX;

    std::vector<VkAttachmentDescription> attachmentDescs{};
    attachmentDescs.reserve(attachments.size());
    std::vector<VkImageView> attachmentViews{};
    attachmentViews.reserve(attachments.size());
    std::vector<VkAttachmentReference> colorRefs{};
    colorRefs.reserve(attachments.size());
    std::vector<VkAttachmentReference> depthRefs{};
    depthRefs.reserve(1);
    clearValues.reserve(attachments.size());

    uint32_t attachmentIndex = 0;
    for (StoreHandle hAttachment : attachments) {
        const Attachment& attachment =
            store.attachments[hAttachment.handle];
        if (!attachment.image.isValid()) {
            Log::error("RenderPass", "Attachment has invalid image");
            return false;
        }
        const Image& backingImage =
            store.images[attachment.image.handle];

        const VkExtent3D& imageExtent = backingImage.imageInfo.extent;
        if (imageExtent.height < minHeight)
            minHeight = imageExtent.height;
        if (imageExtent.width < minWidth)
            minWidth = imageExtent.width;

        attachmentDescs.push_back(attachment.desc);
        attachmentViews.push_back(backingImage.view);
        clearValues.push_back(attachment.clearValue);

        VkAttachmentDescription& desc = attachmentDescs.back();
        desc.format = backingImage.imageInfo.format;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageUsageFlags imUsage = backingImage.imageInfo.usage;
        bool isSampled = (imUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
        if (imUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            colorInput |= isSampled;
            colorRefs.emplace_back(
                attachmentIndex,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            );
            desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else if (imUsage &
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthInput |= isSampled;
            depthRefs.emplace_back(
                attachmentIndex,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            );
            desc.finalLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            std::unreachable();
        }

        if (isSampled)
            desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachmentIndex++;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorRefs.size();
    subpass.pColorAttachments = colorRefs.data();
    if (depthRefs.size() > 0)
        subpass.pDepthStencilAttachment = depthRefs.data();

    std::vector<VkSubpassDependency> dependencies{};
    dependencies.reserve(4);

    if (depthInput) {
        dependencies.emplace_back(
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            // TODO, what about VK_DEPENDENCY_BY_REGION_BIT?
        );

        dependencies.emplace_back(
            0, VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
            // TODO, what about VK_DEPENDENCY_BY_REGION_BIT?
        );
    } else if (depthRefs.size() > 0) {
        dependencies.emplace_back(
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
        );
    }

    if (colorInput) {
        dependencies.emplace_back(
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        );

        dependencies.emplace_back(
            0, VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT
        );
    } else if (colorRefs.size() > 0) {
        dependencies.emplace_back(
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
        );
    }

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = attachmentDescs.size();
    info.pAttachments = attachmentDescs.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = dependencies.size();
    info.pDependencies = dependencies.data();
    vkchk(vkCreateRenderPass(device, &info, nullptr, &renderPass));

    // Since the framebuffer is currently tightly coupled to the
    // renderpass we just create it here because we have all the
    // info we need here (unless we move the image date out of
    // the attachments)
    renderArea.extent = {minWidth, minHeight};
    VkFramebufferCreateInfo fbufInfo = {};
    fbufInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbufInfo.pNext = NULL;
    fbufInfo.renderPass = renderPass;
    fbufInfo.pAttachments = attachmentViews.data();
    fbufInfo.attachmentCount = attachmentViews.size();
    fbufInfo.width = renderArea.extent.width;
    fbufInfo.height = renderArea.extent.height;
    fbufInfo.layers = 1;
    vkchk(
        vkCreateFramebuffer(device, &fbufInfo, nullptr, &framebuffer)
    );
    return true;
}

void RenderPass::destroy(
    const Store&,
    VkDevice device,
    VmaAllocator
) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
    clearValues.clear();
}

bool RenderPass::rendersToSwapchain(const Store& store) const {
    for (auto attachHandle : attachments) {
        assert(attachHandle.isValid());
        auto attachment = &store.attachments[attachHandle.handle];
        auto imgHandle = attachment->image;
        assert(imgHandle.isValid());
        auto image = &store.images[imgHandle.handle];
        if (image->isSwapchainImage)
            return true;
    }
    return false;
}

bool Present::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    if (!image.isValid()) {
        Log::error("Present", "No image connected");
        return false;
    }
    vkchk(vkCreateSampler(device, &samplerInfo, nullptr, &outSampler));
    outDS = ImGui_ImplVulkan_AddTexture(
        outSampler, store.images[image.handle].view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    return true;
}

void Present::destroy(
    const Store&,
    VkDevice device,
    VmaAllocator allocator
) {
    if (outDS != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(outDS);
        outDS = VK_NULL_HANDLE;
    }
    if (outSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, outSampler, nullptr);
        outSampler = VK_NULL_HANDLE;
    }
}

bool Present::connectLink(
    const LinkSlot& slot,
    Store& store
) {
    // Validate slot handle is valid
    if (!slot.handle.isValid()) {
        Log::error("Primitives", "Present: Invalid slot handle");
        return false;
    }

    if (slot.slot != 0) {
        Log::error("Primitives", "Present: Invalid slot {}", slot.slot);
        return false;
    }

    if (slot.handle.type != Type::Array) {
        Log::error(
            "Primitives", "Present: Expected image array in slot 0"
        );
        return false;
    }

    const Array& array = store.arrays[slot.handle.handle];
    if (array.type != Type::Image) {
        Log::error(
            "Primitives", "Present: Expected image array in slot 0"
        );
        return false;
    }

    if (array.handles.empty()) {
        Log::error(
            "Primitives", "Present: Image array in slot 0 empty"
        );
        return false;
    }

    Image& imageObj = store.images[array.handles.front()];

    // For live view, image needs to have this format
    if (imageObj.imageInfo.format != VK_FORMAT_R8G8B8A8_UNORM) {
        Log::error(
            "Primitives",
            "Present: Expected image format VK_FORMAT_R8G8B8A8_UNORM"
        );
        return false;
    }

    // TODO: Check for swapchain extent
    if (imageObj.extentType != ExtentType::SwapchainRelative) {
        Log::error(
            "Primitives", "Present: Expected swapchain relative size"
        );
        return false;
    }

    // For live view, we need to add sampler usage for the image
    // if we want to pass it to imgui
    imageObj.imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    imageObj.isSwapchainImage = true;

    image = {array.handles.front(), Type::Image};
    return true;
}

VkDescriptorSet Present::getLiveViewImage() const {
    return outDS;
}

void Store::reset() {
    arrayCount = 0;
    vertexDataCount = 0;
    uniformBufferCount = 0;
    cameraCount = 0;
    lightCount = 0;
    descriptorPoolCount = 0;
    descriptorSetCount = 0;
    renderPassCount = 0;
    pipelineCount = 0;
    shaderCount = 0;
    imageCount = 0;
    attachmentCount = 0;
    presentCount = 0;
    state = StoreState::Empty;
}

void Store::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy in reverse order of dependencies
    for (uint32_t i = 0; i < presentCount; ++i)
        presents[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < renderPassCount; ++i)
        renderPasses[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < pipelineCount; ++i)
        pipelines[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < descriptorSetCount; ++i)
        descriptorSets[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < shaderCount; ++i)
        shaders[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < imageCount; ++i)
        images[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < uniformBufferCount; ++i)
        uniformBuffers[i].destroy(*this, device, allocator);

    for (uint32_t i = 0; i < vertexDataCount; ++i)
        vertexDatas[i].destroy(*this, device, allocator);

    // Descriptor pools last - they implicitly free descriptor sets
    for (uint32_t i = 0; i < descriptorPoolCount; ++i)
        descriptorPools[i].destroy(*this, device, allocator);
}

StoreHandle Store::defaultDescriptorPool() {
    // Only initialize the default descriptor pool if we need one
    if (descriptorPoolCount == 0)
        newDescriptorPool();

    assert(descriptorPoolCount > 0);
    return StoreHandle{0, Type::DescriptorPool};
}

StoreHandle Store::newArray() {
    assert(arrayCount < arrays.max_size());
    StoreHandle handle{arrayCount, Type::Array};

    Array* arr = new (arrays.data() + handle.handle) Array{};
    arr->name = std::format("array_{}", handle.handle);

    arrayCount += 1;
    return handle;
}

StoreHandle Store::newVertexData() {
    assert(vertexDataCount < vertexDatas.max_size());
    StoreHandle handle{vertexDataCount, Type::VertexData};

    VertexData* vd = new (vertexDatas.data() + handle.handle) VertexData{};
    vd->name = std::format("vertexData_{}", handle.handle);

    vertexDataCount += 1;
    return handle;
}

StoreHandle Store::newUniformBuffer() {
    assert(uniformBufferCount < uniformBuffers.max_size());
    StoreHandle handle{uniformBufferCount, Type::UniformBuffer};

    UniformBuffer* ub = new (uniformBuffers.data() + handle.handle) UniformBuffer{};
    ub->name = std::format("ubo_{}", handle.handle);

    uniformBufferCount += 1;
    return handle;
}

StoreHandle Store::newCamera() {
    assert(cameraCount < cameras.max_size());
    StoreHandle handle{cameraCount, Type::Camera};

    Camera* cam = new (cameras.data() + handle.handle) Camera{};
    cam->name = std::format("camera_{}", handle.handle);

    cameraCount += 1;
    return handle;
}

StoreHandle Store::newLight() {
    assert(lightCount < lights.max_size());
    StoreHandle handle{lightCount, Type::Light};

    Light* light = new (lights.data() + handle.handle) Light{};
    light->name = std::format("light_{}", handle.handle);

    lightCount += 1;
    return handle;
}

StoreHandle Store::newDescriptorPool() {
    assert(descriptorPoolCount < descriptorPools.max_size());
    StoreHandle handle{descriptorPoolCount, Type::DescriptorPool};

    DescriptorPool* dp = new (descriptorPools.data() + handle.handle) DescriptorPool{};
    dp->name = std::format("descriptorPool_{}", handle.handle);

    descriptorPoolCount += 1;
    return handle;
}

StoreHandle Store::newDescriptorSet() {
    assert(descriptorSetCount < descriptorSets.max_size());
    StoreHandle handle{descriptorSetCount, Type::DescriptorSet};

    DescriptorSet* ds = new (descriptorSets.data() + handle.handle) DescriptorSet{};
    ds->name = std::format("descriptorSet_{}", handle.handle);

    descriptorSetCount += 1;
    return handle;
}

StoreHandle Store::newRenderPass() {
    assert(renderPassCount < renderPasses.max_size());
    StoreHandle handle{renderPassCount, Type::RenderPass};

    RenderPass* rp = new (renderPasses.data() + handle.handle) RenderPass{};
    rp->name = std::format("renderPass_{}", handle.handle);

    renderPassCount += 1;
    return handle;
}

StoreHandle Store::newPipeline() {
    assert(pipelineCount < pipelines.max_size());
    StoreHandle handle{pipelineCount, Type::Pipeline};

    Pipeline* pl = new (pipelines.data() + handle.handle) Pipeline{};
    pl->name = std::format("pipeline_{}", handle.handle);

    pipelineCount += 1;
    return handle;
}

StoreHandle Store::newShader() {
    assert(shaderCount < shaders.max_size());
    StoreHandle handle{shaderCount, Type::Shader};

    Shader* sh = new (shaders.data() + handle.handle) Shader{};
    sh->name = std::format("shader_{}", handle.handle);

    shaderCount += 1;
    return handle;
}

StoreHandle Store::newAttachment() {
    assert(attachmentCount < attachments.max_size());
    StoreHandle handle = {attachmentCount, Type::Attachment};

    Attachment* att = new (attachments.data() + handle.handle) Attachment{};
    att->name = std::format("attachment_{}", handle.handle);

    attachmentCount += 1;
    return handle;
}

StoreHandle Store::newImage() {
    assert(imageCount < images.max_size());
    StoreHandle handle = {imageCount, Type::Image};

    Image* img = new (images.data() + handle.handle) Image{};
    img->name = std::format("image_{}", handle.handle);

    imageCount += 1;
    return handle;
}

StoreHandle Store::newPresent() {
    assert(presentCount < presents.max_size());
    StoreHandle handle = {presentCount, Type::Present};

    Present* pr = new (presents.data() + handle.handle) Present{};
    pr->name = std::format("present_{}", handle.handle);

    presentCount += 1;
    return handle;
}


uint32_t Store::getShaderCount() const {
    return shaderCount;
}

StoreState Store::getState() const {
    return state;
}

void Store::link() {
    // TODO: Actually check that linking was successful
    state = StoreState::Linked;
}

std::vector<Node*> Store::getNodes() {
    // TODO: Order the nodes (or preserve ordering?)
    // TODO: Keep control over nodes entirely to the store?
    // TODO: Skip nodes that are not linked
    //       and not marked for internal use?
    using std::views::take;

    std::vector<Node*> nodes;
    nodes.reserve(
        descriptorPoolCount + imageCount + attachmentCount +
        renderPassCount + uniformBufferCount + cameraCount +
        lightCount + descriptorSetCount + vertexDataCount + shaderCount +
        pipelineCount + presentCount
    );

    // For now this ordering is manual and the order is important.
    // This is also only really relevant for all nodes that implement
    // some sort of resource allocation in the create method.
    for (auto& pool : descriptorPools | take(descriptorPoolCount))
        nodes.push_back(&pool);
    for (auto& image : images | take(imageCount))
        nodes.push_back(&image);
    for (auto& attachment : attachments | take(attachmentCount))
        nodes.push_back(&attachment);
    for (auto& renderPass : renderPasses | take(renderPassCount))
        nodes.push_back(&renderPass);
    for (auto& ubo : uniformBuffers | take(uniformBufferCount))
        nodes.push_back(&ubo);
    for (auto& camera : cameras | take(cameraCount))
        nodes.push_back(&camera);
    for (auto& light : lights | take(lightCount))
        nodes.push_back(&light);
    for (auto& set : descriptorSets | take(descriptorSetCount))
        nodes.push_back(&set);
    for (auto& vertexData : vertexDatas | take(vertexDataCount))
        nodes.push_back(&vertexData);
    for (auto& shader : shaders | take(shaderCount))
        nodes.push_back(&shader);
    for (auto& pipeline : pipelines | take(pipelineCount))
        nodes.push_back(&pipeline);
    for (auto& present : presents | take(presentCount))
        nodes.push_back(&present);

    return nodes;
}

std::vector<const GenerateNode*> Store::getGenerateNodes() const {
    // TODO: Order the nodes (or preserve ordering?)
    // TODO: Keep control over nodes entirely to the store?
    // TODO: Skip nodes that are not linked
    //       and not marked for internal use?
    using std::views::take;

    std::vector<const GenerateNode*> nodes;
    nodes.reserve(
        descriptorPoolCount + imageCount + attachmentCount +
        renderPassCount + uniformBufferCount + cameraCount +
        lightCount + descriptorSetCount + vertexDataCount + shaderCount +
        pipelineCount
    );

    // For now this ordering is manual and the order is important.
    // This is also only really relevant for all nodes that implement
    // some sort of resource allocation in the create method.
    for (auto& pool : descriptorPools | take(descriptorPoolCount))
        nodes.push_back(&pool);
    for (auto& image : images | take(imageCount))
        nodes.push_back(&image);
    for (auto& attachment : attachments | take(attachmentCount))
        nodes.push_back(&attachment);
    for (auto& renderPass : renderPasses | take(renderPassCount))
        nodes.push_back(&renderPass);
    for (auto& ubo : uniformBuffers | take(uniformBufferCount))
        nodes.push_back(&ubo);
    for (auto& camera : cameras | take(cameraCount))
        nodes.push_back(&camera);
    for (auto& light : lights | take(lightCount))
        nodes.push_back(&light);
    for (auto& set : descriptorSets | take(descriptorSetCount))
        nodes.push_back(&set);
    for (auto& vertexData : vertexDatas | take(vertexDataCount))
        nodes.push_back(&vertexData);
    for (auto& shader : shaders | take(shaderCount))
        nodes.push_back(&shader);
    for (auto& pipeline : pipelines | take(pipelineCount))
        nodes.push_back(&pipeline);

    return nodes;
}

Node* Store::getNode(StoreHandle handle) {
    // TODO: This is not needed for all nodes actually, only the
    // ones that accept link connections hmm
    assert(handle.isValid());

    switch (handle.type) {
    case Type::Array:
        assert(handle.handle < arrayCount);
        return &arrays[handle.handle];
    case Type::VertexData:
        assert(handle.handle < vertexDataCount);
        return &vertexDatas[handle.handle];
    case Type::UniformBuffer:
        assert(handle.handle < uniformBufferCount);
        return &uniformBuffers[handle.handle];
    case Type::Camera:
        assert(handle.handle < cameraCount);
        return &cameras[handle.handle];
    case Type::Light:
        assert(handle.handle < lightCount);
        return &lights[handle.handle];
    case Type::DescriptorPool:
        assert(handle.handle < descriptorPoolCount);
        return &descriptorPools[handle.handle];
    case Type::DescriptorSet:
        assert(handle.handle < descriptorSetCount);
        return &descriptorSets[handle.handle];
    case Type::RenderPass:
        assert(handle.handle < renderPassCount);
        return &renderPasses[handle.handle];
    case Type::Attachment:
        assert(handle.handle < attachmentCount);
        return &attachments[handle.handle];
    case Type::Image:
        assert(handle.handle < imageCount);
        return &images[handle.handle];
    case Type::Pipeline:
        assert(handle.handle < pipelineCount);
        return &pipelines[handle.handle];
    case Type::Shader:
        assert(handle.handle < shaderCount);
        return &shaders[handle.handle];
    case Type::Present:
        assert(handle.handle < presentCount);
        return &presents[handle.handle];
    case Type::Invalid:
        std::unreachable();
    }

    std::unreachable();
}

void Store::updateSwapchainExtent(const VkExtent3D& extent) {
    auto allocImages = images | std::views::take(imageCount);
    for (Image& image : allocImages)
        image.updateSwapchainExtent(extent);
}

VkDescriptorSet Store::getLiveViewImage() {
    auto allocPresents = presents | std::views::take(presentCount);
    for (const auto& present : allocPresents)
        return present.getLiveViewImage();

    return VK_NULL_HANDLE;
}

bool Store::hasValidPresent() const {
    if (presentCount == 0)
        return false;

    auto allocPresents = presents | std::views::take(presentCount);
    for (const auto& present : allocPresents) {
        if (present.isReady())
            return true;
    }

    return false;
}

std::string Store::getName(StoreHandle handle) const {
    if (!handle.isValid())
        return "";

    Node* node = const_cast<Store*>(this)->getNode(handle);
    return node ? node->name : "";
}

void Store::validateUniqueNames() const {
    // Validate uniqueness within each primitive type separately.
    // Different types can share names (e.g., a Camera and Pipeline both named "main")
    // but two primitives of the same type cannot.

    auto validateType = [](const auto& primitives, uint32_t count, const char* typeName) {
        std::unordered_set<std::string> names;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& node = primitives[i];
            if (node.name.empty())
                continue;
            auto [it, inserted] = names.insert(node.name);
            if (!inserted) {
                Log::error(
                    "Store", "Duplicate name '{}' found in {} primitives",
                    node.name, typeName
                );
                assert(false && "Duplicate primitive name detected within type!");
            }
        }
    };

    validateType(arrays, arrayCount, "Array");
    validateType(vertexDatas, vertexDataCount, "VertexData");
    validateType(uniformBuffers, uniformBufferCount, "UniformBuffer");
    validateType(cameras, cameraCount, "Camera");
    validateType(lights, lightCount, "Light");
    validateType(descriptorPools, descriptorPoolCount, "DescriptorPool");
    validateType(descriptorSets, descriptorSetCount, "DescriptorSet");
    validateType(renderPasses, renderPassCount, "RenderPass");
    validateType(attachments, attachmentCount, "Attachment");
    validateType(images, imageCount, "Image");
    validateType(pipelines, pipelineCount, "Pipeline");
    validateType(shaders, shaderCount, "Shader");
    validateType(presents, presentCount, "Present");
}

// ============================================================================
// Code Generation Methods
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

void VertexData::generateCreate(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    print(out, "// VertexData: {} (vertexCount={}, indexCount={})\n", name, vertexCount, indexCount);
    print(out, "{{\n");

    // Check if we have a model file path for runtime loading
    if (!modelFilePath.empty()) {
        // Extract geometry from pre-loaded model
        print(out,
            "    // Extract geometry {} from pre-loaded model\n"
            "    std::vector<Vertex> {}_vertices;\n"
            "    std::vector<uint32_t> {}_indices;\n"
            "    loadModelGeometry({}, {}, {}_vertices, {}_indices);\n\n"
            "    {}_vertexCount = static_cast<uint32_t>({}_vertices.size());\n"
            "    {}_indexCount = static_cast<uint32_t>({}_indices.size());\n"
            "    VkDeviceSize {}_vertexSize = {}_vertices.size() * sizeof(Vertex);\n"
            "    VkDeviceSize {}_indexSize = {}_indices.size() * sizeof(uint32_t);\n\n",
            geometryIndex,
            name, name,
            modelPathToVarName(modelFilePath), geometryIndex, name, name,
            name, name,
            name, name,
            name, name,
            name, name
        );

        // Create both staging buffers upfront
        print(out,
            "    // Create staging buffers (batched for single GPU sync)\n"
            "    VkBuffer {}_vertexStagingBuffer;\n"
            "    VmaAllocation {}_vertexStagingAlloc;\n"
            "    VmaAllocationInfo {}_vertexStagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_vertexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {}_vertexStagingBuffer, {}_vertexStagingAlloc, &{}_vertexStagingAllocInfo);\n"
            "    memcpy({}_vertexStagingAllocInfo.pMappedData, {}_vertices.data(), {}_vertexSize);\n\n"
            "    VkBuffer {}_indexStagingBuffer;\n"
            "    VmaAllocation {}_indexStagingAlloc;\n"
            "    VmaAllocationInfo {}_indexStagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_indexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {}_indexStagingBuffer, {}_indexStagingAlloc, &{}_indexStagingAllocInfo);\n"
            "    memcpy({}_indexStagingAllocInfo.pMappedData, {}_indices.data(), {}_indexSize);\n\n",
            name, name, name, name, name, name, name, name, name, name,
            name, name, name, name, name, name, name, name, name, name
        );

        // Create device-local buffers
        print(out,
            "    // Create device-local buffers\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_vertexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,\n"
            "        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,\n"
            "        0,\n"
            "        {}_vertexBuffer, {}_vertexAlloc, nullptr);\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_indexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,\n"
            "        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,\n"
            "        0,\n"
            "        {}_indexBuffer, {}_indexAlloc, nullptr);\n\n",
            name, name, name, name, name, name
        );

        // Single batched copy for both buffers
        print(out,
            "    // Batched copy with single GPU sync\n"
            "    {{\n"
            "        VkCommandBuffer cmdBuffer = beginSingleTimeCommands(device, commandPool);\n"
            "        VkBufferCopy vertexCopy{{.size = {}_vertexSize}};\n"
            "        vkCmdCopyBuffer(cmdBuffer, {}_vertexStagingBuffer, {}_vertexBuffer, 1, &vertexCopy);\n"
            "        VkBufferCopy indexCopy{{.size = {}_indexSize}};\n"
            "        vkCmdCopyBuffer(cmdBuffer, {}_indexStagingBuffer, {}_indexBuffer, 1, &indexCopy);\n"
            "        endSingleTimeCommands(device, graphicsQueue, commandPool, cmdBuffer);\n"
            "    }}\n"
            "    vmaDestroyBuffer(allocator, {}_vertexStagingBuffer, {}_vertexStagingAlloc);\n"
            "    vmaDestroyBuffer(allocator, {}_indexStagingBuffer, {}_indexStagingAlloc);\n",
            name, name, name, name, name, name, name, name, name, name
        );
    } else if (!vertexDataBinPath.empty() && !indexDataBinPath.empty()) {
        // Load vertex/index data from binary files (legacy path)
        print(out,
            "    // Load data from binary files\n"
            "    auto {}_vertexFileData = readFile(\"{}\");\n"
            "    VkDeviceSize {}_vertexSize = {}_vertexFileData.size();\n"
            "    auto {}_indexFileData = readFile(\"{}\");\n"
            "    VkDeviceSize {}_indexSize = {}_indexFileData.size();\n\n",
            name, vertexDataBinPath, name, name,
            name, indexDataBinPath, name, name
        );

        // Create both staging buffers upfront
        print(out,
            "    // Create staging buffers (batched for single GPU sync)\n"
            "    VkBuffer {}_vertexStagingBuffer;\n"
            "    VmaAllocation {}_vertexStagingAlloc;\n"
            "    VmaAllocationInfo {}_vertexStagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_vertexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {}_vertexStagingBuffer, {}_vertexStagingAlloc, &{}_vertexStagingAllocInfo);\n"
            "    memcpy({}_vertexStagingAllocInfo.pMappedData, {}_vertexFileData.data(), {}_vertexSize);\n\n"
            "    VkBuffer {}_indexStagingBuffer;\n"
            "    VmaAllocation {}_indexStagingAlloc;\n"
            "    VmaAllocationInfo {}_indexStagingAllocInfo;\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_indexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,\n"
            "        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,\n"
            "        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
            "        {}_indexStagingBuffer, {}_indexStagingAlloc, &{}_indexStagingAllocInfo);\n"
            "    memcpy({}_indexStagingAllocInfo.pMappedData, {}_indexFileData.data(), {}_indexSize);\n\n",
            name, name, name, name, name, name, name, name, name, name,
            name, name, name, name, name, name, name, name, name, name
        );

        // Create device-local buffers
        print(out,
            "    // Create device-local buffers\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_vertexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,\n"
            "        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,\n"
            "        0,\n"
            "        {}_vertexBuffer, {}_vertexAlloc, nullptr);\n"
            "    createBuffer(physicalDevice, device, allocator,\n"
            "        {}_indexSize,\n"
            "        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,\n"
            "        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,\n"
            "        0,\n"
            "        {}_indexBuffer, {}_indexAlloc, nullptr);\n\n",
            name, name, name, name, name, name
        );

        // Single batched copy for both buffers
        print(out,
            "    // Batched copy with single GPU sync\n"
            "    {{\n"
            "        VkCommandBuffer cmdBuffer = beginSingleTimeCommands(device, commandPool);\n"
            "        VkBufferCopy vertexCopy{{.size = {}_vertexSize}};\n"
            "        vkCmdCopyBuffer(cmdBuffer, {}_vertexStagingBuffer, {}_vertexBuffer, 1, &vertexCopy);\n"
            "        VkBufferCopy indexCopy{{.size = {}_indexSize}};\n"
            "        vkCmdCopyBuffer(cmdBuffer, {}_indexStagingBuffer, {}_indexBuffer, 1, &indexCopy);\n"
            "        endSingleTimeCommands(device, graphicsQueue, commandPool, cmdBuffer);\n"
            "    }}\n"
            "    vmaDestroyBuffer(allocator, {}_vertexStagingBuffer, {}_vertexStagingAlloc);\n"
            "    vmaDestroyBuffer(allocator, {}_indexStagingBuffer, {}_indexStagingAlloc);\n",
            name, name, name, name, name, name, name, name, name, name
        );
    } else {
        // Fallback: generate placeholder comment for manual implementation
        print(out,
            "    // TODO: Load vertex/index data and create buffers\n"
            "    // Expected sizes: vertex={} bytes, index={} bytes\n",
            vertexDataSize, indexDataSize
        );
    }

    print(out, "}}\n\n");
}

void VertexData::generateDestroy(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    print(out,
        "   // Destroy VertexData: {}\n"
        "   if ({}_indexBuffer != VK_NULL_HANDLE) {{\n"
        "       vmaDestroyBuffer(allocator, {}_indexBuffer, {}_indexAlloc);\n"
        "       {}_indexBuffer = VK_NULL_HANDLE;\n"
        "       {}_indexAlloc = VK_NULL_HANDLE;\n"
        "   }}\n"
        "   if ({}_vertexBuffer != VK_NULL_HANDLE) {{\n"
        "       vmaDestroyBuffer(allocator, {}_vertexBuffer, {}_vertexAlloc);\n"
        "       {}_vertexBuffer = VK_NULL_HANDLE;\n"
        "       {}_vertexAlloc = VK_NULL_HANDLE;\n"
        "   }}\n\n",
        name,
        name, name, name, name, name,
        name, name, name, name, name
    );
}

void UniformBuffer::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    const auto size = data.size();
    print(out, "// UniformBuffer: {}\n", name);
    print(out, "{{\n");

    // Generate buffer create info
    print(out,
        "    VkBufferCreateInfo {}_info{{\n"
        "        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,\n"
        "        .size = {},\n"
        "        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,\n"
        "        .sharingMode = VK_SHARING_MODE_EXCLUSIVE\n"
        "    }};\n\n",
        name, size
    );

    // Generate VMA allocation info
    print(out,
        "    VmaAllocationCreateInfo {}_allocInfo{{\n"
        "        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,\n"
        "        .usage = VMA_MEMORY_USAGE_AUTO,\n"
        "        .priority = 1.0f\n"
        "    }};\n\n",
        name
    );

    // Generate vmaCreateBuffer call
    print(out,
        "    VmaAllocationInfo {}_mappedInfo{{}};\n"
        "    vkchk(vmaCreateBuffer(allocator, &{}_info, &{}_allocInfo, &{}, &{}_alloc, &{}_mappedInfo));\n"
        "    {}_mapped = {}_mappedInfo.pMappedData;\n",
        name, name, name, name, name, name, name, name
    );

    // Handle camera UBO initialization
    if (dataType == UniformDataType::Camera) {
        auto type = reinterpret_cast<const CameraType*>(extraData);
        assert(type != nullptr);

        if (*type == CameraType::Fixed) {
            // Fixed camera: Initialize with actual camera matrices from data
            assert(sizeof(CameraData) == data.size());
            auto cameraData = reinterpret_cast<const CameraData*>(data.data());

            // Helper to format float with guaranteed decimal point for valid C++ literal
            auto flt = [](float v) -> std::string {
                auto s = std::format("{:g}", v);
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                    s += ".0";
                return s + "f";
            };

            // Helper to format glm::mat4 as C++ initializer
            auto formatMat4 = [&flt](const glm::mat4& m) -> std::string {
                return std::format(
                    "glm::mat4({}, {}, {}, {}, "
                              "{}, {}, {}, {}, "
                              "{}, {}, {}, {}, "
                              "{}, {}, {}, {})",
                    flt(m[0][0]), flt(m[0][1]), flt(m[0][2]), flt(m[0][3]),
                    flt(m[1][0]), flt(m[1][1]), flt(m[1][2]), flt(m[1][3]),
                    flt(m[2][0]), flt(m[2][1]), flt(m[2][2]), flt(m[2][3]),
                    flt(m[3][0]), flt(m[3][1]), flt(m[3][2]), flt(m[3][3])
                );
            };

            print(out,
                "    Camera {0}_initData{{\n"
                "        .view = {1},\n"
                "        .invView = {2},\n"
                "        .proj = {3}\n"
                "    }};\n"
                "    memcpy({0}_mapped, &{0}_initData, sizeof(Camera));\n",
                name,
                formatMat4(cameraData->view),
                formatMat4(cameraData->invView),
                formatMat4(cameraData->proj)
            );
        } else {
            // FPS or Orbital camera: Initialize from CameraController
            // Find the camera that owns this UBO to get the controller name
            std::string cameraName;
            for (const auto& camera : store.cameras) {
                if (camera.ubo.isValid() && &store.uniformBuffers[camera.ubo.handle] == this) {
                    cameraName = sanitizeName(camera.name);
                    break;
                }
            }

            if (!cameraName.empty()) {
                print(out,
                    "    // Initialize FPS/Orbital camera UBO from controller\n"
                    "    Camera {0}_initData{{\n"
                    "        .view = {1}.getViewMatrix(),\n"
                    "        .invView = glm::inverse({1}.getViewMatrix()),\n"
                    "        .proj = {1}.getProjectionMatrix()\n"
                    "    }};\n"
                    "    memcpy({0}_mapped, &{0}_initData, sizeof(Camera));\n",
                    name, cameraName
                );
            }
        }
    }

    // Handle light UBO initialization
    if (dataType == UniformDataType::Light) {
        // Find the Light primitive that owns this UBO
        for (const auto& light : store.lights) {
            if (light.ubo.isValid() && &store.uniformBuffers[light.ubo.handle] == this) {
                // Helper to format float with guaranteed decimal point for valid C++ literal
                auto flt = [](float v) -> std::string {
                    auto s = std::format("{:g}", v);
                    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                        s += ".0";
                    return s + "f";
                };

                // Generate light array initialization
                print(out, "\n    // Initialize light UBO with {} lights\n", light.numLights);
                print(out, "    std::array<Light, {}> {}_initData{{{{\n", light.numLights, name);
                for (int i = 0; i < light.numLights && i < static_cast<int>(light.lights.size()); ++i) {
                    const auto& l = light.lights[i];
                    print(out,
                        "        Light{{\n"
                        "            .position = glm::vec3({}, {}, {}),\n"
                        "            .radius = {},\n"
                        "            .color = glm::vec3({}, {}, {})\n"
                        "        }}{}\n",
                        flt(l.position.x), flt(l.position.y), flt(l.position.z),
                        flt(l.radius),
                        flt(l.color.x), flt(l.color.y), flt(l.color.z),
                        (i < light.numLights - 1) ? "," : ""
                    );
                }
                print(out, "    }}}};\n");
                print(out, "    memcpy({}_mapped, {}_initData.data(), sizeof({}_initData));\n", name, name, name);
                break;
            }
        }
    }

    // Initialize model matrix UBOs with identity matrices
    // Mode matrix UBO size is 128 bytes (2 x mat4: model + normalMatrix)
    if (size == 128) {
        print(out,
            "\n    // Initialize model matrix UBO with identity matrices\n"
            "    struct ModelMatrices {{\n"
            "        alignas(16) glm::mat4 model{{1.0f}};\n"
            "        alignas(16) glm::mat4 normalMatrix{{1.0f}};\n"
            "    }};\n"
            "    ModelMatrices {}_initData;\n"
            "    memcpy({}_mapped, &{}_initData, sizeof(ModelMatrices));\n",
            name, name, name
        );
    }

    print(out, "}}\n\n");
}


void UniformBuffer::generateRecordCommands(
    const Store& store,
    std::ostream& out
) const {
    // Camera UBO updates are handled by Camera::generateRecordCommands()
    // to avoid duplicate code generation
}

void UniformBuffer::generateDestroy(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    print(out,
        "   // Destroy UniformBuffer: {}\n"
        "   if ({} != VK_NULL_HANDLE) {{\n"
        "       vmaDestroyBuffer(allocator, {}, {}_alloc);\n"
        "       {} = VK_NULL_HANDLE;\n"
        "       {}_alloc = VK_NULL_HANDLE;\n"
        "       {}_mapped = nullptr;\n"
        "   }}\n\n",
        name,
        name, name, name,
        name, name, name
    );
}

void Shader::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    auto shaderPath = std::filesystem::path{"compiled_shaders"} / getSpirvPath();
    print(out,
        "// Shader: {0} (stage={1}, entryPoint={2})\n"
        "{{\n"
	"    auto {0}_path = std::filesystem::path{{\"{3}\"}}.string();\n"
        "    auto {0}_code = readFile({0}_path.c_str());\n"
        "    {0} = createShaderModule(device, {0}_code);\n"
        "}}\n\n"
        ,
        name,
        string_VkShaderStageFlagBits(stage),
        entryPoint,
        shaderPath.generic_string());
}

void Shader::generateDestroy(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    print(out,
        "   // Destroy Shader: {0}\n"
        "   if ({0} != VK_NULL_HANDLE) {{\n"
        "       vkDestroyShaderModule(device, {0}, nullptr);\n"
        "       {0} = VK_NULL_HANDLE;\n"
        "   }}\n\n",
        name
    );
}

void DescriptorPool::generateCreate(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    const auto& poolSets = getSets();
    if (poolSets.empty()) {
        print(out, "// {} has no descriptor sets\n\n", name);
        return;
    }

    uint32_t imageCount = 0;
    uint32_t uniformBufferCount = 0;
    uint32_t totalSets = 0;

    for (const auto& hSet : poolSets) {
        if (!hSet.isValid()) continue;
        const auto& ds = store.descriptorSets[hSet.handle];
        if (ds.name.empty()) continue;

        // Use 0 to let cardinality() calculate the actual number of sets needed
        // This accounts for per-object textures when image arrays have multiple handles
        auto contrib = ds.getPoolSizeContribution(store, 0);
        totalSets += contrib.setCount;
        imageCount += contrib.imageCount;
        uniformBufferCount += contrib.uniformBufferCount;
    }

    if (totalSets == 0) {
        print(out, "// {} has no valid descriptor sets\n\n", name);
        return;
    }

    // Build pool sizes array - only include types with non-zero counts
    // Vulkan spec requires descriptorCount > 0 for each pool size
    std::vector<std::string> poolSizeEntries;
    if (imageCount > 0) {
        poolSizeEntries.push_back(std::format(
            "        {{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {} }}",
            imageCount));
    }
    if (uniformBufferCount > 0) {
        poolSizeEntries.push_back(std::format(
            "        {{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {} }}",
            uniformBufferCount));
    }

    if (poolSizeEntries.empty()) {
        print(out, "// {} has no descriptors\n\n", name);
        return;
    }

    std::string poolSizesStr;
    for (size_t i = 0; i < poolSizeEntries.size(); ++i) {
        poolSizesStr += poolSizeEntries[i];
        if (i < poolSizeEntries.size() - 1) poolSizesStr += ",\n";
    }

    print(out,
        "// Descriptor Pool: {}\n"
        "{{\n"
        "    std::array<VkDescriptorPoolSize, {}> poolSizes = {{{{\n"
        "{}\n"
        "    }}}};\n\n"
        "    VkDescriptorPoolCreateInfo poolInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,\n"
        "        .maxSets = {},\n"
        "        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),\n"
        "        .pPoolSizes = poolSizes.data()\n"
        "    }};\n\n"
        "    vkchk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &{}));\n"
        "}}\n\n",
        name,
        poolSizeEntries.size(),
        poolSizesStr,
        totalSets,
        name
    );
}

void DescriptorPool::generateDestroy(const Store& store, std::ostream& out) const {
    if (name.empty() || getSets().empty()) return;

    print(out,
        "    // Destroy Descriptor Pool: {}\n"
        "    if ({} != VK_NULL_HANDLE) {{\n"
        "        vkDestroyDescriptorPool(device, {}, nullptr);\n"
        "        {} = VK_NULL_HANDLE;\n"
        "    }}\n\n",
        name, name, name, name
    );
}

void DescriptorSet::generateCreate(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    if (expectedBindings.empty()) {
        print(out, "// Descriptor Set: {} (no bindings)\n\n", name);
        return;
    }

    print(out, "// Descriptor Set: {}\n", name);
    print(out, "{{\n");

    print(out, "    std::vector<VkDescriptorSetLayoutBinding> {}_layoutBindings = {{{{\n", name);
    for (size_t i = 0; i < expectedBindings.size(); ++i) {
        const auto& binding = expectedBindings[i];
        const char* typeStr = binding.type == Type::Image
            ? "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER"
            : "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";

        print(out, "        {{\n");
        print(out, "            .binding = {},\n", binding.binding);
        print(out, "            .descriptorType = {},\n", typeStr);
        print(out, "            .descriptorCount = {},\n", binding.arrayCount);
        print(out, "            .stageFlags = {}\n", string_VkShaderStageFlags(binding.stages));
        print(out, "        }}");
        if (i < expectedBindings.size() - 1) print(out, ",");
        print(out, "\n");
    }
    print(out, "    }}}};\n\n");

    print(out,
        "    VkDescriptorSetLayoutCreateInfo {}_layoutInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,\n"
        "        .bindingCount = static_cast<uint32_t>({}_layoutBindings.size()),\n"
        "        .pBindings = {}_layoutBindings.data()\n"
        "    }};\n\n"
        "    vkchk(vkCreateDescriptorSetLayout(device, &{}_layoutInfo, nullptr, &{}_layout));\n\n",
        name,
        name,
        name,
        name, name
    );

    // Get the pool name from the store
    std::string poolName = pool.isValid() ? store.descriptorPools[pool.handle].name : "descriptorPool";

    // Calculate how many descriptor sets we need based on bound arrays
    // Use the cardinality function for consistency with pool size calculation
    uint32_t calculatedCardinality = cardinality(store);
    uint32_t numSetsNeeded = calculatedCardinality > 0 ? calculatedCardinality : 1;

    print(out,
        "    uint32_t {}_numSets = {};\n"
        "    std::vector<VkDescriptorSetLayout> {}_layouts({}_numSets, {}_layout);\n"
        "    VkDescriptorSetAllocateInfo {}_allocInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,\n"
        "        .descriptorPool = {},\n"
        "        .descriptorSetCount = {}_numSets,\n"
        "        .pSetLayouts = {}_layouts.data()\n"
        "    }};\n"
        "    {}_sets.resize({}_numSets);\n"
        "    vkchk(vkAllocateDescriptorSets(device, &{}_allocInfo, {}_sets.data()));\n\n",
        name, numSetsNeeded,
        name, name, name,
        name,
        poolName,
        name,
        name,
        name, name,
        name, name
    );

    for (size_t idx = 0; idx < expectedBindings.size(); ++idx) {
        const auto& binding = expectedBindings[idx];

        // Resolve the actual resource name(s) if bindings are connected
        std::string resourceName;
        std::string resourceSize;
        std::vector<std::string> imageViewNames;  // For arrays of images
        bool hasBinding = idx < bindings.size() && bindings[idx].isValid();

        if (hasBinding) {
            const Array& array = store.arrays[bindings[idx].handle];
            if (!array.handles.empty()) {
                if (array.type == Type::Image) {
                    // Collect ALL image view names for arrays
                    for (uint32_t handle : array.handles) {
                        const Image& img = store.images[handle];
                        imageViewNames.push_back(img.name + "_view");
                    }
                    // Keep first one for backwards compatibility
                    resourceName = imageViewNames[0];
                } else if (array.type == Type::UniformBuffer) {
                    uint32_t resourceHandle = array.handles[0];
                    const UniformBuffer& ubo = store.uniformBuffers[resourceHandle];
                    resourceName = ubo.name;
                    resourceSize = std::to_string(ubo.data.size());
                } else if (array.type == Type::Camera) {
                    uint32_t resourceHandle = array.handles[0];
                    const Camera& cam = store.cameras[resourceHandle];
                    const UniformBuffer& ubo = store.uniformBuffers[cam.ubo.handle];
                    resourceName = ubo.name;
                    resourceSize = std::to_string(ubo.data.size());
                }
            }
        }

        if (binding.type == Type::Image) {
            print(out,
                "    // Sampler for binding {}\n"
                "    VkSamplerCreateInfo {}_samplerInfo_{}{{\n"
                "        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,\n"
                "        .magFilter = {},\n"
                "        .minFilter = {},\n"
                "        .mipmapMode = {},\n"
                "        .addressModeU = {},\n"
                "        .addressModeV = {},\n"
                "        .addressModeW = {},\n"
                "        .borderColor = {}\n"
                "    }};\n"
                "    vkchk(vkCreateSampler(device, &{}_samplerInfo_{}, nullptr, &{}_sampler_{}));\n\n",
                binding.binding,
                name, binding.binding,
                string_VkFilter(binding.samplerInfo.magFilter),
                string_VkFilter(binding.samplerInfo.minFilter),
                string_VkSamplerMipmapMode(binding.samplerInfo.mipmapMode),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeU),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeV),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeW),
                string_VkBorderColor(binding.samplerInfo.borderColor),
                name, binding.binding, name, binding.binding
            );

            // Generate code for multiple textures if we have an array of images
            if (imageViewNames.size() > 1) {
                // Generate an array of image views for per-object textures
                print(out,
                    "    // Array of image views for per-object textures (binding {})\n"
                    "    std::array<VkImageView, {}> {}_imageViews_{} = {{{{\n",
                    binding.binding,
                    imageViewNames.size(),
                    name, binding.binding
                );
                for (size_t i = 0; i < imageViewNames.size(); ++i) {
                    print(out, "        {}", imageViewNames[i]);
                    if (i < imageViewNames.size() - 1) print(out, ",");
                    print(out, "\n");
                }
                print(out, "    }}}};\n\n");

                // Write image descriptors using the array
                print(out,
                    "    // Write image descriptor for binding {} (per-object textures)\n"
                    "    for (uint32_t i = 0; i < {}_numSets; ++i) {{\n"
                    "        VkDescriptorImageInfo {}_imageInfo_{}{{\n"
                    "            .sampler = {}_sampler_{},\n"
                    "            .imageView = {}_imageViews_{}[i],\n"
                    "            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL\n"
                    "        }};\n"
                    "        VkWriteDescriptorSet {}_write_{}{{\n"
                    "            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,\n"
                    "            .dstSet = {}_sets[i],\n"
                    "            .dstBinding = {},\n"
                    "            .dstArrayElement = 0,\n"
                    "            .descriptorCount = 1,\n"
                    "            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,\n"
                    "            .pImageInfo = &{}_imageInfo_{}\n"
                    "        }};\n"
                    "        vkUpdateDescriptorSets(device, 1, &{}_write_{}, 0, nullptr);\n"
                    "    }}\n\n",
                    binding.binding,
                    name,
                    name, binding.binding,
                    name, binding.binding,
                    name, binding.binding,
                    name, binding.binding,
                    name,
                    binding.binding,
                    name, binding.binding,
                    name, binding.binding
                );
            } else {
                // Single image - use original code path
                std::string imageViewExpr = resourceName.empty()
                    ? std::format("VK_NULL_HANDLE /* TODO: set {}_binding{}_imageView */", name, binding.binding)
                    : resourceName;

                print(out,
                    "    // Write image descriptor for binding {}\n"
                    "    for (uint32_t i = 0; i < {}_numSets; ++i) {{\n"
                    "        VkDescriptorImageInfo {}_imageInfo_{}{{\n"
                    "            .sampler = {}_sampler_{},\n"
                    "            .imageView = {},\n"
                    "            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL\n"
                    "        }};\n"
                    "        VkWriteDescriptorSet {}_write_{}{{\n"
                    "            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,\n"
                    "            .dstSet = {}_sets[i],\n"
                    "            .dstBinding = {},\n"
                    "            .dstArrayElement = 0,\n"
                    "            .descriptorCount = 1,\n"
                    "            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,\n"
                    "            .pImageInfo = &{}_imageInfo_{}\n"
                    "        }};\n"
                    "        vkUpdateDescriptorSets(device, 1, &{}_write_{}, 0, nullptr);\n"
                    "    }}\n\n",
                    binding.binding,
                    name,
                    name, binding.binding,
                    name, binding.binding,
                    imageViewExpr,
                    name, binding.binding,
                    name,
                    binding.binding,
                    name, binding.binding,
                    name, binding.binding
                );
            }
        } else if (binding.type == Type::UniformBuffer) {
            std::string bufferExpr = resourceName.empty()
                ? std::format("VK_NULL_HANDLE /* TODO: set {}_binding{}_buffer */", name, binding.binding)
                : resourceName;
            std::string rangeExpr = resourceSize.empty()
                ? std::format("VK_WHOLE_SIZE /* TODO: set {}_binding{}_bufferSize */", name, binding.binding)
                : resourceSize;

            print(out,
                "    // Write uniform buffer descriptor for binding {}\n"
                "    for (uint32_t i = 0; i < {}_numSets; ++i) {{\n"
                "        VkDescriptorBufferInfo {}_bufferInfo_{}{{\n"
                "            .buffer = {},\n"
                "            .offset = 0,\n"
                "            .range = {}\n"
                "        }};\n"
                "        VkWriteDescriptorSet {}_write_{}{{\n"
                "            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,\n"
                "            .dstSet = {}_sets[i],\n"
                "            .dstBinding = {},\n"
                "            .dstArrayElement = 0,\n"
                "            .descriptorCount = 1,\n"
                "            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,\n"
                "            .pBufferInfo = &{}_bufferInfo_{}\n"
                "        }};\n"
                "        vkUpdateDescriptorSets(device, 1, &{}_write_{}, 0, nullptr);\n"
                "    }}\n\n",
                binding.binding,
                name,
                name, binding.binding,
                bufferExpr,
                rangeExpr,
                name, binding.binding,
                name,
                binding.binding,
                name, binding.binding,
                name, binding.binding
            );
        }
    }

    print(out, "}}\n\n");
}

void DescriptorSet::generateDestroy(const Store& store, std::ostream& out) const {
    if (name.empty() || expectedBindings.empty()) return;

    print(out, "    // Destroy DescriptorSet: {}\n", name);

    for (const auto& binding : expectedBindings) {
        if (binding.type == Type::Image) {
            print(out,
                "   if ({}_sampler_{} != VK_NULL_HANDLE) {{\n"
                "       vkDestroySampler(device, {}_sampler_{}, nullptr);\n"
                "       {}_sampler_{} = VK_NULL_HANDLE;\n"
                "   }}\n",
                name, binding.binding,
                name, binding.binding,
                name, binding.binding
            );
        }
    }

    print(out, "    {}_sets.clear();\n", name);

    print(out,
        "   if ({}_layout != VK_NULL_HANDLE) {{\n"
        "       vkDestroyDescriptorSetLayout(device, {}_layout, nullptr);\n"
        "       {}_layout = VK_NULL_HANDLE;\n"
        "   }}\n\n",
        name, name, name
    );
}

void Pipeline::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    assert(renderPass.isValid());
    const auto& rp{store.renderPasses[renderPass.handle]};

    print(out, "// Pipeline: {}\n", name);
    print(out, "{{\n");

    // Shader stages
    print(out, "    // Shader stages\n");
    print(out, "    std::array {}_shaderStages{{\n", name);
    for (const auto& shHandle : shaders) {
        assert(shHandle.isValid());
        const auto& shader = store.shaders[shHandle.handle];
        assert(!shader.name.empty());

        print(out, "        VkPipelineShaderStageCreateInfo{{\n");
        print(out, "            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,\n");
        print(out, "            .stage = {},\n", string_VkShaderStageFlagBits(shader.stage));
        print(out, "            .module = {},\n", shader.name);
        print(out, "            .pName = \"{}\"\n", shader.entryPoint.empty() ? "main": shader.entryPoint);
        print(out, "        }},");
        print(out, "\n");
    }
    print(out, "    }};\n\n");

    // Vertex input state
    print(out, "    // Vertex input state\n");
    if (vertexDataHandle.isValid()) {
        std::string vdName = "vertexData";
        if (vertexDataHandle.type == Type::Array) {
            const auto& arr = store.arrays[vertexDataHandle.handle];
            if (!arr.handles.empty() && arr.type == Type::VertexData) {
                const auto& vd = store.vertexDatas[arr.handles[0]];
                if (!vd.name.empty()) vdName = vd.name;

                print(out, "    VkVertexInputBindingDescription {}_bindingDesc{{\n", name);
                print(out, "        .binding = {},\n", vd.bindingDescription.binding);
                print(out, "        .stride = {},\n", vd.bindingDescription.stride);
                print(out, "        .inputRate = {}\n", string_VkVertexInputRate(vd.bindingDescription.inputRate));
                print(out, "    }};\n\n");

                print(out, "    std::vector<VkVertexInputAttributeDescription> {}_attribDescs = {{{{\n", name);
                for (size_t j = 0; j < vd.attributeDescriptions.size(); ++j) {
                    const auto& attr = vd.attributeDescriptions[j];
                    print(out, "        {{\n");
                    print(out, "            .location = {},\n", attr.location);
                    print(out, "            .binding = {},\n", attr.binding);
                    print(out, "            .format = {},\n", string_VkFormat(attr.format));
                    print(out, "            .offset = {}\n", attr.offset);
                    print(out, "        }}");
                    if (j < vd.attributeDescriptions.size() - 1) print(out, ",");
                    print(out, "\n");
                }
                print(out, "    }}}};\n\n");
            }
        }

        print(out, "    VkPipelineVertexInputStateCreateInfo {}_vertexInputInfo{{\n", name);
        print(out, "        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,\n");
        print(out, "        .vertexBindingDescriptionCount = 1,\n");
        print(out, "        .pVertexBindingDescriptions = &{}_bindingDesc,\n", name);
        print(out, "        .vertexAttributeDescriptionCount = static_cast<uint32_t>({}_attribDescs.size()),\n", name);
        print(out, "        .pVertexAttributeDescriptions = {}_attribDescs.data()\n", name);
        print(out, "    }};\n\n");
    } else {
        print(out, "    VkPipelineVertexInputStateCreateInfo {}_vertexInputInfo{{\n", name);
        print(out, "        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,\n");
        print(out, "        .vertexBindingDescriptionCount = 0,\n");
        print(out, "        .pVertexBindingDescriptions = nullptr,\n");
        print(out, "        .vertexAttributeDescriptionCount = 0,\n");
        print(out, "        .pVertexAttributeDescriptions = nullptr\n");
        print(out, "    }};\n\n");
    }

    // Input assembly state
    print(out,
        "    // Input assembly state\n"
        "    VkPipelineInputAssemblyStateCreateInfo {}_inputAssembly{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,\n"
        "        .topology = {},\n"
        "        .primitiveRestartEnable = {}\n"
        "    }};\n\n",
        name,
        string_VkPrimitiveTopology(inputAssembly.topology),
        inputAssembly.primitiveRestartEnable ? "VK_TRUE" : "VK_FALSE"
    );

    // Viewport state (dynamic)
    print(out,
        "    // Viewport state (dynamic)\n"
        "    VkPipelineViewportStateCreateInfo {}_viewportState{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,\n"
        "        .viewportCount = 1,\n"
        "        .pViewports = nullptr,\n"
        "        .scissorCount = 1,\n"
        "        .pScissors = nullptr\n"
        "    }};\n\n",
        name
    );

    // Rasterization state
    print(out,
        "    // Rasterization state\n"
        "    VkPipelineRasterizationStateCreateInfo {}_rasterizer{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,\n"
        "        .depthClampEnable = {},\n"
        "        .rasterizerDiscardEnable = {},\n"
        "        .polygonMode = {},\n"
        "        .cullMode = {},\n"
        "        .frontFace = {},\n"
        "        .depthBiasEnable = {},\n"
        "        .depthBiasConstantFactor = {},\n"
        "        .depthBiasClamp = {},\n"
        "        .depthBiasSlopeFactor = {},\n"
        "        .lineWidth = {}\n"
        "    }};\n\n",
        name,
        rasterizer.depthClampEnable ? "VK_TRUE" : "VK_FALSE",
        rasterizer.rasterizerDiscardEnable ? "VK_TRUE" : "VK_FALSE",
        string_VkPolygonMode(rasterizer.polygonMode),
        string_VkCullModeFlags(rasterizer.cullMode),
        string_VkFrontFace(rasterizer.frontFace),
        rasterizer.depthBiasEnable ? "VK_TRUE" : "VK_FALSE",
        rasterizer.depthBiasConstantFactor,
        rasterizer.depthBiasClamp,
        rasterizer.depthBiasSlopeFactor,
        rasterizer.lineWidth
    );

    // Multisample state
    print(out,
        "    // Multisample state\n"
        "    VkPipelineMultisampleStateCreateInfo {}_multisampling{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,\n"
        "        .rasterizationSamples = {},\n"
        "        .sampleShadingEnable = {},\n"
        "        .minSampleShading = {}\n"
        "    }};\n\n",
        name,
        string_VkSampleCountFlagBits(multisampling.rasterizationSamples),
        multisampling.sampleShadingEnable ? "VK_TRUE" : "VK_FALSE",
        multisampling.minSampleShading
    );

    // Color blend attachments
    // First, count color attachments and check for depth
    bool hasDepth{false};
    size_t colorAttachmentCount{0};
    for (StoreHandle hAttachment : rp.attachments) {
        assert(hAttachment.isValid());
        const Attachment& a = store.attachments[hAttachment.handle];
        assert(a.image.isValid());
        const Image& backingImage = store.images[a.image.handle];
        VkImageUsageFlags usage = backingImage.imageInfo.usage;

        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            hasDepth = true;
        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            colorAttachmentCount++;
    }

    // Generate color blend attachments array
    if (colorAttachmentCount > 0) {
        print(out,
            "    // Color blend attachments\n"
            "    std::array {}_colorBlendAttachments = {{\n"
            ,
            name);
        for (StoreHandle hAttachment : rp.attachments) {
            const Attachment& a = store.attachments[hAttachment.handle];
            const Image& backingImage = store.images[a.image.handle];
            VkImageUsageFlags usage = backingImage.imageInfo.usage;

            // If this is not a color attachment, ignore
            if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
                continue;

            print(out,
                "        VkPipelineColorBlendAttachmentState{{\n"
                "            .blendEnable = {},\n"
                "            .srcColorBlendFactor = {},\n"
                "            .dstColorBlendFactor = {},\n"
                "            .colorBlendOp = {},\n"
                "            .srcAlphaBlendFactor = {},\n"
                "            .dstAlphaBlendFactor = {},\n"
                "            .alphaBlendOp = {},\n"
                "            .colorWriteMask = {}\n"
                "        }},\n"
                ,
                a.colorBlending.blendEnable ? "VK_TRUE" : "VK_FALSE",
                string_VkBlendFactor(a.colorBlending.srcColorBlendFactor),
                string_VkBlendFactor(a.colorBlending.dstColorBlendFactor),
                string_VkBlendOp(a.colorBlending.colorBlendOp),
                string_VkBlendFactor(a.colorBlending.srcAlphaBlendFactor),
                string_VkBlendFactor(a.colorBlending.dstAlphaBlendFactor),
                string_VkBlendOp(a.colorBlending.alphaBlendOp),
                string_VkColorComponentFlags(a.colorBlending.colorWriteMask));
        }
        print(out, "    }};\n\n");
    }

    // Color blend state
    if (colorAttachmentCount > 0) {
        print(out,
            "    // Color blend state\n"
            "    VkPipelineColorBlendStateCreateInfo {0}_colorBlending{{\n"
            "        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,\n"
            "        .logicOpEnable = {1},\n"
            "        .logicOp = {2},\n"
            "        .attachmentCount = {0}_colorBlendAttachments.size(),\n"
            "        .pAttachments = {0}_colorBlendAttachments.data(),\n"
            "        .blendConstants = {{ {3}, {4}, {5}, {6} }}\n"
            "    }};\n\n",
            name,
            colorBlending.logicOpEnable ? "VK_TRUE" : "VK_FALSE",
            string_VkLogicOp(colorBlending.logicOp),
            colorBlending.blendConstants[0],
            colorBlending.blendConstants[1],
            colorBlending.blendConstants[2],
            colorBlending.blendConstants[3]
        );
    } else {
        print(out,
            "    // Color blend state (no color attachments)\n"
            "    VkPipelineColorBlendStateCreateInfo {0}_colorBlending{{\n"
            "        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,\n"
            "        .logicOpEnable = {1},\n"
            "        .logicOp = {2},\n"
            "        .attachmentCount = 0,\n"
            "        .pAttachments = nullptr,\n"
            "        .blendConstants = {{ {3}, {4}, {5}, {6} }}\n"
            "    }};\n\n",
            name,
            colorBlending.logicOpEnable ? "VK_TRUE" : "VK_FALSE",
            string_VkLogicOp(colorBlending.logicOp),
            colorBlending.blendConstants[0],
            colorBlending.blendConstants[1],
            colorBlending.blendConstants[2],
            colorBlending.blendConstants[3]
        );
    }

    // Depth/stencil state
    if (hasDepth) {
        print(out,
            "    // Depth/stencil state\n"
            "    VkPipelineDepthStencilStateCreateInfo {}_depthStencil{{\n"
            "        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,\n"
            "        .depthTestEnable = {},\n"
            "        .depthWriteEnable = {},\n"
            "        .depthCompareOp = {},\n"
            "        .depthBoundsTestEnable = {},\n"
            "        .stencilTestEnable = {}\n"
            "    }};\n\n",
            name,
            depthStencil.depthTestEnable ? "VK_TRUE" : "VK_FALSE",
            depthStencil.depthWriteEnable ? "VK_TRUE" : "VK_FALSE",
            string_VkCompareOp(depthStencil.depthCompareOp),
            depthStencil.depthBoundsTestEnable ? "VK_TRUE" : "VK_FALSE",
            depthStencil.stencilTestEnable ? "VK_TRUE" : "VK_FALSE"
        );
    }

    // Dynamic state
    print(out,
        "    // Dynamic state\n"
        "    std::array {0}_dynamicStates = {{\n"
        "        VK_DYNAMIC_STATE_VIEWPORT,\n"
        "        VK_DYNAMIC_STATE_SCISSOR\n"
        "    }};\n"
        "    VkPipelineDynamicStateCreateInfo {0}_dynamicState{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,\n"
        "        .dynamicStateCount = {0}_dynamicStates.size(),\n"
        "        .pDynamicStates = {0}_dynamicStates.data()\n"
        "    }};\n\n",
        name
    );

    // Descriptor set layouts
    if (!descriptorSetHandles.empty()) {
        print(out, "    // Descriptor set layouts\n");
        print(out, "    std::vector<VkDescriptorSetLayout> {}_dsLayouts = {{\n", name);
        for (size_t i = 0; i < descriptorSetHandles.size(); ++i) {
            const auto& dsHandle = descriptorSetHandles[i];
            std::string dsName = store.getName(dsHandle);
            if (dsName.empty()) dsName = std::format("descriptorSet_{}", dsHandle.handle);
            print(out, "        {}_layout", dsName);
            if (i < descriptorSetHandles.size() - 1) print(out, ",");
            print(out, "\n");
        }
        print(out, "    }};\n\n");
    }

    // Pipeline layout
    print(out,
        "    // Pipeline layout\n"
        "    VkPipelineLayoutCreateInfo {0}_layoutInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,\n"
        "        .setLayoutCount = static_cast<uint32_t>({1}),\n"
        "        .pSetLayouts = {2},\n"
        "        .pushConstantRangeCount = 0,\n"
        "        .pPushConstantRanges = nullptr\n"
        "    }};\n"
        "    vkchk(vkCreatePipelineLayout(device, &{0}_layoutInfo, nullptr, &{0}_layout));\n\n"
        ,
        name,
        descriptorSetHandles.empty() ? "0" : name + "_dsLayouts.size()",
        descriptorSetHandles.empty() ? "nullptr" : name + "_dsLayouts.data()"
    );

    // Graphics pipeline
    print(out,
        "    // Graphics pipeline\n"
        "    VkGraphicsPipelineCreateInfo {0}_pipelineInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,\n"
        "        .stageCount = {0}_shaderStages.size(),\n"
        "        .pStages = {0}_shaderStages.data(),\n"
        "        .pVertexInputState = &{0}_vertexInputInfo,\n"
        "        .pInputAssemblyState = &{0}_inputAssembly,\n"
        "        .pViewportState = &{0}_viewportState,\n"
        "        .pRasterizationState = &{0}_rasterizer,\n"
        "        .pMultisampleState = &{0}_multisampling,\n"
        "        .pDepthStencilState = {1},\n"
        "        .pColorBlendState = &{0}_colorBlending,\n"
        "        .pDynamicState = &{0}_dynamicState,\n"
        "        .layout = {0}_layout,\n"
        "        .renderPass = {2},\n"
        "        .subpass = 0\n"
        "    }};\n"
        "    vkchk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &{0}_pipelineInfo, nullptr, &{0}));\n",
        name,
        hasDepth ? std::format("&{}_depthStencil", name) : "nullptr",
        rp.name
    );

    print(out, "}}\n\n");
}

void Pipeline::generateDestroy(const Store& store, std::ostream& out) const {
    if (name.empty()) return;

    print(out,
        "   // Destroy Pipeline: {0}\n"
        "   if ({0} != VK_NULL_HANDLE) {{\n"
        "       vkDestroyPipeline(device, {0}, nullptr);\n"
        "       {0} = VK_NULL_HANDLE;\n"
        "   }}\n"
        "   if ({0}_layout != VK_NULL_HANDLE) {{\n"
        "       vkDestroyPipelineLayout(device, {0}_layout, nullptr);\n"
        "       {0}_layout = VK_NULL_HANDLE;\n"
        "   }}\n\n",
        name
    );
}

void Pipeline::generateRecordCommands(const Store& store, std::ostream& out) const {
    assert(!name.empty());
    assert(renderPass.isValid());

    const auto& rp{store.renderPasses[renderPass.handle]};

    print(out, "    // Pipeline: {}\n", name);
    print(out, "    {{\n");

    // Begin render pass
    print(out,
          "        VkRenderPassBeginInfo {0}_passInfo{{\n"
          "            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,\n"
          "            .renderPass = {1},\n"
          "            .framebuffer = {2},\n"
          "            .renderArea = {1}_renderArea,\n"
          "            .clearValueCount = static_cast<uint32_t>({1}_clearValues.size()),\n"
          "            .pClearValues = {1}_clearValues.data()\n"
          "        }};\n"
          "\n"
          "        vkCmdBeginRenderPass(cmdBuffer, &{0}_passInfo,\n"
          "            VK_SUBPASS_CONTENTS_INLINE);\n\n"
          ,
          name,
          rp.name,
          rp.rendersToSwapchain(store)
            ? rp.name + "_framebuffers[imageInFlightIndex]"
            : rp.name + "_framebuffer"
    );

    // Set viewport (dynamic)
    print(out,
          "        VkViewport {0}_viewport{{\n"
          "            .x = 0.0f,\n"
          "            .y = 0.0f,\n"
          "            .width = static_cast<float>({1}_renderArea.extent.width),\n"
          "            .height = static_cast<float>({1}_renderArea.extent.height),\n"
          "            .minDepth = 0.0f,\n"
          "            .maxDepth = 1.0f\n"
          "        }};\n"
          "        vkCmdSetViewport(cmdBuffer, 0, 1, &{0}_viewport);\n\n"
          ,
          name,
          rp.name
    );

    // Set scissor (dynamic)
    print(out,
          "        VkRect2D {0}_scissor{{\n"
          "            .offset = {{0, 0}},\n"
          "            .extent = {1}_renderArea.extent\n"
          "        }};\n"
          "        vkCmdSetScissor(cmdBuffer, 0, 1, &{0}_scissor);\n\n"
          ,
          name,
          rp.name
    );

    // Bind pipeline
    print(out,
          "        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, {});\n\n"
          ,
          name
    );

    // Find which descriptor set index has per-object textures (for rebinding in draw loop)
    int perObjectDescSetIndex = -1;
    std::string perObjectDescSetName;
    for (size_t i = 0; i < descriptorSetHandles.size(); ++i) {
        if (!descriptorSetHandles[i].isValid()) continue;
        const auto& ds = store.descriptorSets[descriptorSetHandles[i].handle];
        const auto& dsBindings = ds.getBindings();
        // Check if this descriptor set has image bindings with multiple textures
        for (size_t bindIdx = 0; bindIdx < dsBindings.size(); ++bindIdx) {
            if (!dsBindings[bindIdx].isValid()) continue;
            const Array& arr = store.arrays[dsBindings[bindIdx].handle];
            if (arr.type == Type::Image && arr.handles.size() > 1) {
                perObjectDescSetIndex = static_cast<int>(i);
                perObjectDescSetName = ds.name;
                break;
            }
        }
        if (perObjectDescSetIndex >= 0) break;
    }

    // Bind descriptor sets if any
    if (!descriptorSetHandles.empty()) {
        print(out, "        std::array {}_descSets{{\n", name);
        for (const auto& dsHandle : descriptorSetHandles) {
            if (dsHandle.isValid()) {
                const auto& ds = store.descriptorSets[dsHandle.handle];
                // Use the appropriate set based on frame index
                print(out, "            {}_sets[imageInFlightIndex % {}_sets.size()],\n",
                      ds.name, ds.name);
            }
        }
        print(out, "        }};\n");
        print(out,
              "        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,\n"
              "            {0}_layout, 0, static_cast<uint32_t>({0}_descSets.size()),\n"
              "            {0}_descSets.data(), 0, nullptr);\n\n"
              ,
              name
        );
    }

    // Draw - either with vertex buffer or fullscreen triangle
    if (vertexDataHandle.isValid()) {
        // Handle vertex data array
        if (vertexDataHandle.type == Type::Array) {
            const auto& arr = store.arrays[vertexDataHandle.handle];
            if (!arr.handles.empty() && arr.type == Type::VertexData) {
                uint32_t geometryIndex = 0;
                for (uint32_t handle : arr.handles) {
                    const auto& vd = store.vertexDatas[handle];
                    if (vd.name.empty()) continue;

                    print(out,
                          "        // Draw: {0}\n"
                          "        {{\n"
                          ,
                          vd.name
                    );

                    // Rebind per-object descriptor set if needed
                    if (perObjectDescSetIndex >= 0) {
                        print(out,
                              "            // Rebind per-object descriptor set for this geometry\n"
                              "            VkDescriptorSet {0}_perObjDescSet = {1}_sets[{2}];\n"
                              "            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,\n"
                              "                {0}_layout, {3}, 1, &{0}_perObjDescSet, 0, nullptr);\n"
                              ,
                              name,
                              perObjectDescSetName,
                              geometryIndex,
                              perObjectDescSetIndex
                        );
                    }

                    print(out,
                          "            VkBuffer vertexBuffers[] = {{{0}_vertexBuffer}};\n"
                          "            VkDeviceSize offsets[] = {{0}};\n"
                          "            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);\n"
                          ,
                          vd.name
                    );

                    if (vd.indexCount > 0) {
                        print(out,
                              "            vkCmdBindIndexBuffer(cmdBuffer, {0}_indexBuffer, 0, VK_INDEX_TYPE_UINT32);\n"
                              "            vkCmdDrawIndexed(cmdBuffer, {0}_indexCount, 1, 0, 0, 0);\n"
                              ,
                              vd.name
                        );
                    } else {
                        print(out,
                              "            vkCmdDraw(cmdBuffer, {}_vertexCount, 1, 0, 0);\n"
                              ,
                              vd.name
                        );
                    }
                    print(out, "        }}\n");
                    geometryIndex++;
                }
            }
        }
    } else {
        // No vertex data - draw fullscreen triangle (3 vertices)
        print(out, "        // Fullscreen triangle (no vertex buffer)\n");
        print(out, "        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);\n");
    }

    print(out, "\n        vkCmdEndRenderPass(cmdBuffer);\n");
    print(out, "    }}\n");
}

void RenderPass::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());
    assert(!attachments.empty());

    std::vector<const Attachment *> attachmentPtrs;
    std::vector<const Image *> imagePtrs;
    attachmentPtrs.reserve(attachments.size());
    imagePtrs.reserve(attachments.size());
    for (const auto& attHandle : attachments) {
        assert(attHandle.isValid());
        auto att = &store.attachments[attHandle.handle];
        attachmentPtrs.push_back(att);
        assert(att->image.isValid());
        auto img = &store.images[att->image.handle];
        imagePtrs.push_back(img);
    }

    print(out, "// Render Pass: {}\n", name);
    print(out, "{{\n");

    // Collect attachment descriptions
    print(out, "    std::array {}_attachmentDescs = {{\n", name);
    for (auto att: attachmentPtrs)
        print(out, "        {}_desc,\n", att->name);
    print(out, "    }};\n\n");


    // First collect all the attachment references before we print them,
    // so that we can skip them if there are none
    bool depthInput{false};
    bool colorInput{false};
    bool swapChainInput{false};
    bool swapChainRelativeExtent{false};
    uint32_t minHeight = UINT32_MAX;
    uint32_t minWidth = UINT32_MAX;
    std::vector<VkAttachmentReference> colorRefs;
    colorRefs.reserve(attachments.size());
    std::vector<VkAttachmentReference> depthRefs;
    depthRefs.reserve(1);

    auto attachmentsIdx = std::views::zip(
        std::views::iota(0), attachmentPtrs, imagePtrs);
    for (auto&& [binding, att, img] : attachmentsIdx) {
        const auto& usage = img->imageInfo.usage;
        bool isSampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;

        const VkExtent3D& imageExtent = img->imageInfo.extent;
        if (imageExtent.height < minHeight)
            minHeight = imageExtent.height;
        if (imageExtent.width < minWidth)
            minWidth = imageExtent.width;

        if (img->extentType == ExtentType::SwapchainRelative)
            swapChainRelativeExtent = true;

        if (img->isSwapchainImage) {
            colorRefs.emplace_back(binding,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            swapChainInput = true;
            continue;
        }

        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            colorRefs.emplace_back(binding, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            colorInput |= isSampled;
        }

        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthRefs.emplace_back(binding,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depthInput |= isSampled;
        }
    }

    if (!colorRefs.empty()) {
        print(out, "    std::array {}_colorRefs{{\n", name);
        for (const auto& ref : colorRefs) {
            print(out,
                  "        VkAttachmentReference{{\n"
                  "            .attachment = {},\n"
                  "            .layout = {}\n"
                  "        }},\n",
                  ref.attachment,
                  string_VkImageLayout(ref.layout)
            );
        }
        print(out, "    }};\n\n");
    }

    if (!depthRefs.empty()) {
        print(out, "    std::array {}_depthRefs{{\n", name);
        for (const auto& ref : depthRefs) {
            print(out,
                  "        VkAttachmentReference{{\n"
                  "            .attachment = {},\n"
                  "            .layout = {}\n"
                  "        }},\n",
                  ref.attachment,
                  string_VkImageLayout(ref.layout)
            );
        }
        print(out, "    }};\n\n");
    }

    // Subpass
    print(out,
        "    VkSubpassDescription {}_subpass{{\n"
        "        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,\n"
        "        .colorAttachmentCount = {},\n"
        "        .pColorAttachments = {},\n"
        "        .pDepthStencilAttachment = {}\n"
        "    }};\n\n",
        name,
        colorRefs.empty() ? "0" : std::format("{}_colorRefs.size()", name),
        colorRefs.empty() ? "nullptr" : std::format("{}_colorRefs.data()", name),
        depthRefs.empty() ? "nullptr" : std::format("{}_depthRefs.data()", name)
    );

    // begin subpass deps
    print(out, "    std::array {}_subpassDeps{{\n", name);

    if (depthInput) {
        print(out,
              "        VkSubpassDependency{{\n"
              "            VK_SUBPASS_EXTERNAL, 0,\n"
              "            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,\n"
              "            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,\n"
              "            VK_ACCESS_SHADER_READ_BIT,\n"
              "            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT\n"
              "        }},\n"
              "        VkSubpassDependency{{\n"
              "            0, VK_SUBPASS_EXTERNAL,\n"
              "            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,\n"
              "            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,\n"
              "            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,\n"
              "            VK_ACCESS_SHADER_READ_BIT\n"
              "        }},\n");
    } else {
        print(out,
              "        VkSubpassDependency{{\n"
              "            VK_SUBPASS_EXTERNAL, 0,\n"
              "            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |\n"
              "                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,\n"
              "            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |\n"
              "                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,\n"
              "            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,\n"
              "            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |\n"
              "                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT\n"
              "        }},\n");
    }

    if (colorInput) {
        print(out,
              "        VkSubpassDependency{{\n"
              "            VK_SUBPASS_EXTERNAL, 0,\n"
              "            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,\n"
              "            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,\n"
              "            VK_ACCESS_MEMORY_READ_BIT,\n"
              "            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |\n"
              "                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT\n"
              "        }},\n"
              "        VkSubpassDependency{{\n"
              "            0, VK_SUBPASS_EXTERNAL,\n"
              "            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,\n"
              "            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,\n"
              "            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |\n"
              "                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,\n"
              "            VK_ACCESS_MEMORY_READ_BIT\n"
              "        }},\n");
    } else {
        print(out,
              "        VkSubpassDependency{{\n"
              "            VK_SUBPASS_EXTERNAL, 0,\n"
              "            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,\n"
              "            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,\n"
              "            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |\n"
              "                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT\n"
              "        }},\n");
    }

    // end subpass deps
    print(out, "    }};\n");

    // Render pass create info
    print(out,
        "    VkRenderPassCreateInfo {0}_rpInfo{{\n"
        "        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,\n"
        "        .attachmentCount = {0}_attachmentDescs.size(),\n"
        "        .pAttachments = {0}_attachmentDescs.data(),\n"
        "        .subpassCount = 1,\n"
        "        .pSubpasses = &{0}_subpass,\n"
        "        .dependencyCount = {0}_subpassDeps.size(),\n"
        "        .pDependencies = {0}_subpassDeps.data()\n"
        "    }};\n\n"
        "    vkchk(vkCreateRenderPass(device, &{0}_rpInfo, nullptr, &{0}));\n\n",
        name
    );

    std::string extent_width;
    std::string extent_height;
    if (swapChainRelativeExtent) {
        extent_width = "swapChainExtent.width";
        extent_height = "swapChainExtent.height";
    } else {
        extent_width = std::format("{}", minWidth);
        extent_height = std::format("{}", minHeight);
    }

    // If our render pass is being used to render to a swapchain image,
    // we need to make sure to create different framebuffers for the
    // different frames in flight.
    if (swapChainInput) {
        print(out,
            "    {0}_framebuffers.reserve(swapChainImages.size());\n"
            "    for (size_t i = 0; i < swapChainImages.size(); i++) {{\n"
            "        std::array views{{\n"
            ,
            name);

        for (auto img : imagePtrs) {
            if (img->isSwapchainImage)
                print(out, "            {}_views[i],\n", img->name);
            else
                print(out, "            {}_view,\n", img->name);
        }

        print(out,
            "        }};\n"
            "\n"
            "        VkFramebufferCreateInfo info{{\n"
            "            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,\n"
            "            .renderPass = {0},\n"
            "            .attachmentCount = views.size(),\n"
            "            .pAttachments = views.data(),\n"
            "            .width = {1},\n"
            "            .height = {2},\n"
            "            .layers = 1\n"
            "       }};\n"
            "\n"
            "       VkFramebuffer framebuffer;\n"
            "       vkchk(vkCreateFramebuffer(device, &info, nullptr, &framebuffer));\n"
            "       {0}_framebuffers.push_back(framebuffer);\n"
            "    }}\n"
            ,
            name,
            extent_width,
            extent_height);
    } else {
        print(out, "    std::array {}_views{{\n", name);
        for (auto img : imagePtrs)
             print(out, "        {}_view,\n", img->name);
        print(out,
            "    }};\n"
            "\n"
            "    VkFramebufferCreateInfo fbufInfo{{\n"
            "        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,\n"
            "        .renderPass = {0},\n"
            "        .attachmentCount = {0}_views.size(),\n"
            "        .pAttachments = {0}_views.data(),\n"
            "        .width = {1},\n"
            "        .height = {2},\n"
            "        .layers = 1\n"
            "   }};\n"
            "\n"
            "   vkchk(vkCreateFramebuffer(device, &fbufInfo, nullptr, &{0}_framebuffer));\n"
            ,
            name,
            extent_width,
            extent_height);
    }

    // Initialize renderArea and clearValues
    print(out,
        "    {0}_renderArea = VkRect2D{{\n"
        "        .offset = {{0, 0}},\n"
        "        .extent = {{{1}, {2}}}\n"
        "    }};\n"
        ,
        name,
        extent_width,
        extent_height);

    // Generate clear values for each attachment
    print(out, "    {}_clearValues = {{\n", name);
    for (const auto& attHandle : attachments) {
        const auto& att = store.attachments[attHandle.handle];
        const auto& img = store.images[att.image.handle];

        if (img.imageInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            print(out, "        VkClearValue{{.depthStencil = {{{:.6f}f, {}}}}},\n",
                att.clearValue.depthStencil.depth,
                att.clearValue.depthStencil.stencil);
        } else {
            print(out, "        VkClearValue{{.color = {{{{{:.6f}f, {:.6f}f, {:.6f}f, {:.6f}f}}}}}},\n",
                att.clearValue.color.float32[0],
                att.clearValue.color.float32[1],
                att.clearValue.color.float32[2],
                att.clearValue.color.float32[3]);
        }
    }
    print(out, "    }};\n");

    print(out, "}}\n\n");
}

void RenderPass::generateDestroy(const Store& store, std::ostream& out) const {
    assert(!name.empty());
    assert(!attachments.empty());

    print(out, "    // Destroy RenderPass: {}\n", name);

    if (rendersToSwapchain(store)) {
        print(out,
            "    for (auto framebuffer : {0}_framebuffers)\n"
            "        vkDestroyFramebuffer(device, framebuffer, nullptr);\n"
            "    {0}_framebuffers.clear();\n"
            ,
            name);
    } else {
        print(out,
            "   if ({0}_framebuffer != VK_NULL_HANDLE) {{\n"
            "       vkDestroyFramebuffer(device, {0}_framebuffer, nullptr);\n"
            "       {0}_framebuffer = VK_NULL_HANDLE;\n"
            "   }}\n"
            ,
            name);
    }

    print(out,
        "   if ({0} != VK_NULL_HANDLE) {{\n"
        "       vkDestroyRenderPass(device, {0}, nullptr);\n"
        "       {0} = VK_NULL_HANDLE;\n"
        "   }}\n\n"
        ,
        name);
}

} // namespace primitives
