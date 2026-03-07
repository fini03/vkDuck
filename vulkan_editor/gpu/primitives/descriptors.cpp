// DescriptorPool and DescriptorSet primitive implementations
#include "common.h"

namespace primitives {

// ============================================================================
// DescriptorPool
// ============================================================================

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

// ============================================================================
// DescriptorSet
// ============================================================================

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
    VkDescriptorPool vkPool = poolNode.getPool();

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
        .descriptorPool = vkPool,
        .descriptorSetCount = numSets,
        .pSetLayouts = layouts.data()
    };
    sets.resize(numSets, VK_NULL_HANDLE);
    vkchk(vkAllocateDescriptorSets(device, &setInfo, sets.data()));

    auto fullBindings = std::views::zip(expectedBindings, bindings);
    for (const auto& [info, handle] : fullBindings) {
        const Array& array = store.arrays[handle.handle];
        bool typeMatches = (array.type == info.type) ||
                           (array.type == Type::Camera && info.type == Type::UniformBuffer);
        if (!typeMatches) {
            Log::error("Primitives", "DescriptorSet: Array type mismatch (got {}, expected {})",
                       static_cast<uint32_t>(array.type), static_cast<uint32_t>(info.type));
            return false;
        }

        if (array.type == Type::Image) {
            samplers.emplace_back(VK_NULL_HANDLE);
            vkchk(vkCreateSampler(
                device, &info.samplerInfo, nullptr, &samplers.back()
            ));

            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;
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

    if (!slot.handle.isValid()) {
        Log::error("Primitives", "DescriptorSet: Invalid slot handle");
        return false;
    }

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

} // namespace primitives
