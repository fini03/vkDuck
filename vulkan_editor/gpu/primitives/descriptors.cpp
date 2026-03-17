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

// ============================================================================
// DescriptorPool - Code Generation
// ============================================================================

using std::print;

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

        auto contrib = ds.getPoolSizeContribution(store, 0);
        totalSets += contrib.setCount;
        imageCount += contrib.imageCount;
        uniformBufferCount += contrib.uniformBufferCount;
    }

    if (totalSets == 0) {
        print(out, "// {} has no valid descriptor sets\n\n", name);
        return;
    }

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

// ============================================================================
// DescriptorSet - Code Generation
// ============================================================================

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
        name, name, name, name, name
    );

    std::string poolName = pool.isValid() ? store.descriptorPools[pool.handle].name : "descriptorPool";
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
        name, numSetsNeeded, name, name, name, name,
        poolName, name, name, name, name, name, name
    );

    for (size_t idx = 0; idx < expectedBindings.size(); ++idx) {
        const auto& binding = expectedBindings[idx];

        std::string resourceName;
        std::string resourceSize;
        std::vector<std::string> imageViewNames;
        std::vector<std::string> uboNames;
        bool hasBinding = idx < bindings.size() && bindings[idx].isValid();

        if (hasBinding) {
            const Array& array = store.arrays[bindings[idx].handle];
            if (!array.handles.empty()) {
                if (array.type == Type::Image) {
                    for (uint32_t handle : array.handles) {
                        const Image& img = store.images[handle];
                        imageViewNames.push_back(img.name + "_view");
                    }
                    resourceName = imageViewNames[0];
                } else if (array.type == Type::UniformBuffer) {
                    // Collect all UBO names for per-object descriptor sets
                    for (uint32_t handle : array.handles) {
                        const UniformBuffer& ubo = store.uniformBuffers[handle];
                        uboNames.push_back(ubo.name);
                    }
                    resourceName = uboNames[0];
                    const UniformBuffer& firstUbo = store.uniformBuffers[array.handles[0]];
                    resourceSize = std::to_string(firstUbo.data.size());
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
                binding.binding, name, binding.binding,
                string_VkFilter(binding.samplerInfo.magFilter),
                string_VkFilter(binding.samplerInfo.minFilter),
                string_VkSamplerMipmapMode(binding.samplerInfo.mipmapMode),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeU),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeV),
                string_VkSamplerAddressMode(binding.samplerInfo.addressModeW),
                string_VkBorderColor(binding.samplerInfo.borderColor),
                name, binding.binding, name, binding.binding
            );

            if (imageViewNames.size() > 1) {
                print(out,
                    "    // Array of image views for per-object textures (binding {})\n"
                    "    std::array<VkImageView, {}> {}_imageViews_{} = {{{{\n",
                    binding.binding, imageViewNames.size(), name, binding.binding
                );
                for (size_t i = 0; i < imageViewNames.size(); ++i) {
                    print(out, "        {}", imageViewNames[i]);
                    if (i < imageViewNames.size() - 1) print(out, ",");
                    print(out, "\n");
                }
                print(out, "    }}}};\n\n");

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
                    binding.binding, name, name, binding.binding,
                    name, binding.binding, name, binding.binding,
                    name, binding.binding, name, binding.binding,
                    name, binding.binding, name, binding.binding
                );
            } else {
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
                    binding.binding, name, name, binding.binding,
                    name, binding.binding, imageViewExpr,
                    name, binding.binding, name, binding.binding,
                    name, binding.binding, name, binding.binding
                );
            }
        } else if (binding.type == Type::UniformBuffer) {
            std::string rangeExpr = resourceSize.empty()
                ? std::format("VK_WHOLE_SIZE /* TODO: set {}_binding{}_bufferSize */", name, binding.binding)
                : resourceSize;

            if (uboNames.size() > 1) {
                // Per-object UBOs: create an array of buffers and use indexed access
                print(out,
                    "    // Array of UBO buffers for per-object uniforms (binding {})\n"
                    "    std::array<VkBuffer, {}> {}_buffers_{} = {{{{\n",
                    binding.binding, uboNames.size(), name, binding.binding
                );
                for (size_t i = 0; i < uboNames.size(); ++i) {
                    print(out, "        {}", uboNames[i]);
                    if (i < uboNames.size() - 1) print(out, ",");
                    print(out, "\n");
                }
                print(out, "    }}}};\n\n");

                print(out,
                    "    // Write uniform buffer descriptor for binding {} (per-object UBOs)\n"
                    "    for (uint32_t i = 0; i < {}_numSets; ++i) {{\n"
                    "        VkDescriptorBufferInfo {}_bufferInfo_{}{{\n"
                    "            .buffer = {}_buffers_{}[i],\n"
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
                    binding.binding, name, name, binding.binding,
                    name, binding.binding, rangeExpr, name, binding.binding,
                    name, binding.binding, name, binding.binding,
                    name, binding.binding
                );
            } else {
                // Single UBO: use the same buffer for all descriptor sets
                std::string bufferExpr = resourceName.empty()
                    ? std::format("VK_NULL_HANDLE /* TODO: set {}_binding{}_buffer */", name, binding.binding)
                    : resourceName;

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
                    binding.binding, name, name, binding.binding,
                    bufferExpr, rangeExpr, name, binding.binding,
                    name, binding.binding, name, binding.binding,
                    name, binding.binding
                );
            }
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

} // namespace primitives
