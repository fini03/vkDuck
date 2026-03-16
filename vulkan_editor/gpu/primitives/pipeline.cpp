// Shader, Pipeline, and RenderPass primitive implementations
#include "common.h"
#include <imgui_impl_vulkan.h>

namespace primitives {

// ============================================================================
// Shader
// ============================================================================

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
}

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

// ============================================================================
// Pipeline
// ============================================================================

bool Pipeline::create(
    const Store& store,
    VkDevice device,
    VmaAllocator allocator
) {
    // Use shared render pass if this pipeline continues another's render pass
    StoreHandle effectiveRenderPass = sharedRenderPass.isValid() ? sharedRenderPass : renderPass;

    if (!effectiveRenderPass.isValid()) {
        Log::error("Pipeline", "Invalid render pass handle");
        return false;
    }
    if (shaders.empty()) {
        Log::error("Pipeline", "No shaders");
        return false;
    }
    const RenderPass& rp = store.renderPasses[effectiveRenderPass.handle];

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
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr
    };

    std::vector<VkPipelineColorBlendAttachmentState> attachmentBlends;
    attachmentBlends.reserve(rp.attachments.size());
    bool hasDepth{false};
    for (StoreHandle hAttachment : rp.attachments) {
        assert(hAttachment.isValid());
        const Attachment& a = store.attachments[hAttachment.handle];
        assert(a.image.isValid());
        const Image& backingImage = store.images[a.image.handle];
        VkImageUsageFlags usage = backingImage.imageInfo.usage;

        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            hasDepth = true;

        if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            continue;

        attachmentBlends.push_back(a.colorBlending);
    }
    colorBlending.attachmentCount = attachmentBlends.size();
    colorBlending.pAttachments = attachmentBlends.data();

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

    for (; descriptorIt != descriptorSetHandles.end(); ++descriptorIt) {
        if (!descriptorIt->isValid()) {
            Log::warning("Pipeline", "Skipping invalid global descriptor set handle");
            continue;
        }

        if (descriptorIt->type != Type::DescriptorSet) {
            Log::warning("Pipeline", "Skipping descriptor with wrong type");
            continue;
        }
        const auto& ds = store.descriptorSets[descriptorIt->handle];

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

    auto numObjSets =
        std::distance(descriptorIt, descriptorSetHandles.end());
    if (numObjSets < 0) {
        Log::warning("Pipeline", "Invalid per-object descriptor set count");
        numObjSets = 0;
    }
    size_t numObj = 0;
    std::vector<VkDescriptorSet> allSets;
    for (; descriptorIt != descriptorSetHandles.end(); ++descriptorIt) {
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
    // Use shared render pass if this pipeline continues another's render pass
    StoreHandle effectiveRenderPass = sharedRenderPass.isValid() ? sharedRenderPass : renderPass;

    if (!effectiveRenderPass.isValid()) {
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

    const RenderPass& rp = store.renderPasses[effectiveRenderPass.handle];

    // Only begin render pass if this pipeline owns it (not continuing another's)
    if (beginsRenderPass) {
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
    }

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

    if (!vertexDataHandle.isValid()) {
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
        if (endsRenderPass) {
            vkCmdEndRenderPass(cmdBuffer);
        }
        return;
    }

    if (vertexDataHandle.type != Type::Array) {
        Log::warning("Pipeline", "Skipping render: vertex data handle is not an array");
        if (endsRenderPass) {
            vkCmdEndRenderPass(cmdBuffer);
        }
        return;
    }
    const Array& vertexArray = store.arrays[vertexDataHandle.handle];
    if (vertexArray.type != Type::VertexData) {
        Log::warning("Pipeline", "Skipping render: vertex array is not VertexData type");
        if (endsRenderPass) {
            vkCmdEndRenderPass(cmdBuffer);
        }
        return;
    }
    auto vertices =
        vertexArray.handles |
        std::views::transform([store](auto handle) -> const auto& {
            return store.vertexDatas[handle];
        });

    auto drawVertices = [cmdBuffer](const auto& vdata) {
        if (vdata.vertexBuffer == VK_NULL_HANDLE) {
            Log::warning("Pipeline", "Skipping draw: vertex buffer is null");
            return;
        }

        VkBuffer vertexBuffers[] = {vdata.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);

        if (vdata.indexBuffer == VK_NULL_HANDLE) {
            vkCmdDraw(cmdBuffer, vdata.vertexCount, 1, 0, 0);
            return;
        }

        vkCmdBindIndexBuffer(
            cmdBuffer, vdata.indexBuffer, 0, VK_INDEX_TYPE_UINT32
        );
        vkCmdDrawIndexed(cmdBuffer, vdata.indexCount, 1, 0, 0, 0);
    };

    if (perObjectDescriptorSets.empty()) {
        std::for_each(vertices.begin(), vertices.end(), drawVertices);
    } else {
        if (perObjectDescriptorSets.size() !=
            static_cast<size_t>(std::ranges::distance(vertices))) {
            Log::warning(
                "Pipeline",
                "Skipping render: per-object descriptor sets count mismatch"
            );
            if (endsRenderPass) {
                vkCmdEndRenderPass(cmdBuffer);
            }
            return;
        }
        auto combinedGeometry =
            std::views::zip(vertices, perObjectDescriptorSets);
        for (auto&& [vertexData, objSets] : combinedGeometry) {
            if (objSets.empty()) {
                Log::warning("Pipeline", "Skipping object: empty descriptor set");
                continue;
            }
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

    // Only end render pass if this pipeline is the final one in the chain
    if (endsRenderPass) {
        vkCmdEndRenderPass(cmdBuffer);
    }
}

bool Pipeline::connectLink(
    const LinkSlot& slot,
    Store& store
) {
    if (!slot.handle.isValid()) {
        Log::error("Primitives", "Pipeline: Invalid slot handle");
        return false;
    }

    // Slot 0 = vertex data
    if (slot.slot == 0) {
        if (slot.handle.type != Type::Array) {
            Log::error("Primitives", "Pipeline: Expected array type for vertex data");
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

    // Slot 1+ = attachment inputs (for shared render pass)
    if (slot.handle.type != Type::Array) {
        Log::error("Primitives", "Pipeline: Expected array type for attachment input");
        return false;
    }

    const Array& array = store.arrays[slot.handle.handle];
    if (array.type != Type::Image) {
        Log::error("Primitives", "Pipeline: Expected Image array for attachment input");
        return false;
    }

    // Store the received attachment handle for passthrough output
    // Slot 1 = first attachment, so index = slot - 1
    uint32_t attachmentIndex = slot.slot - 1;
    if (attachmentIndex >= receivedAttachmentHandles.size()) {
        receivedAttachmentHandles.resize(attachmentIndex + 1);
    }
    receivedAttachmentHandles[attachmentIndex] = slot.handle;

    Log::debug(
        "Primitives",
        "Pipeline: Stored attachment handle at index {} (slot {})",
        attachmentIndex, slot.slot
    );

    // Find which pipeline owns this attachment (has it in its render pass)
    // We need to get the source pipeline's render pass for compatibility
    for (uint32_t i = 0; i < store.pipelines.size(); ++i) {
        const Pipeline& srcPipeline = store.pipelines[i];
        if (!srcPipeline.renderPass.isValid()) continue;

        const RenderPass& rp = store.renderPasses[srcPipeline.renderPass.handle];
        for (const auto& attHandle : rp.attachments) {
            const Attachment& att = store.attachments[attHandle.handle];
            // Check if this attachment's image matches the input array's image
            if (array.handles.empty()) continue;
            if (att.image.handle == array.handles[0] ||
                (att.resolveImage.isValid() && att.resolveImage.handle == array.handles[0])) {
                // Found the source pipeline - use its render pass
                sharedRenderPass = srcPipeline.renderPass;
                beginsRenderPass = false;
                Log::debug(
                    "Primitives",
                    "Pipeline: Sharing render pass from another pipeline (attachment input slot {})",
                    slot.slot
                );
                return true;
            }
        }
    }

    Log::warning(
        "Primitives",
        "Pipeline: Could not find source render pass for attachment input slot {}",
        slot.slot
    );
    return true; // Still allow connection even if we can't find source
}

// ============================================================================
// RenderPass
// ============================================================================

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

    uint32_t minHeight = UINT32_MAX;
    uint32_t minWidth = UINT32_MAX;

    std::vector<VkAttachmentDescription> attachmentDescs{};
    attachmentDescs.reserve(attachments.size() * 2);
    std::vector<VkImageView> attachmentViews{};
    attachmentViews.reserve(attachments.size() * 2);
    std::vector<VkAttachmentReference> colorRefs{};
    colorRefs.reserve(attachments.size());
    std::vector<VkAttachmentReference> resolveRefs{};
    resolveRefs.reserve(attachments.size());
    std::vector<VkAttachmentReference> depthRefs{};
    depthRefs.reserve(1);
    clearValues.reserve(attachments.size() * 2);
    bool hasResolveAttachments = false;

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

        // Get current attachment index before adding
        uint32_t attachmentIndex = static_cast<uint32_t>(attachmentDescs.size());

        attachmentDescs.push_back(attachment.desc);
        attachmentViews.push_back(backingImage.view);
        clearValues.push_back(attachment.clearValue);

        VkAttachmentDescription& desc = attachmentDescs.back();
        desc.format = backingImage.imageInfo.format;
        desc.samples = backingImage.imageInfo.samples;
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

            if (attachment.resolveImage.isValid()) {
                hasResolveAttachments = true;
                const Image& resolveImg = store.images[attachment.resolveImage.handle];

                VkAttachmentDescription resolveDesc{};
                resolveDesc.format = resolveImg.imageInfo.format;
                resolveDesc.samples = VK_SAMPLE_COUNT_1_BIT;
                resolveDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolveDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                resolveDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolveDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                resolveDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                resolveDesc.finalLayout = isSampled
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                uint32_t resolveIndex = static_cast<uint32_t>(attachmentDescs.size());
                attachmentDescs.push_back(resolveDesc);
                attachmentViews.push_back(resolveImg.view);
                clearValues.push_back({});

                resolveRefs.emplace_back(
                    resolveIndex,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                );
            } else {
                resolveRefs.emplace_back(
                    VK_ATTACHMENT_UNUSED,
                    VK_IMAGE_LAYOUT_UNDEFINED
                );
            }
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
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pResolveAttachments = hasResolveAttachments ? resolveRefs.data() : nullptr;
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
        );

        dependencies.emplace_back(
            0, VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
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

// ============================================================================
// Present
// ============================================================================

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

    if (imageObj.imageInfo.format != VK_FORMAT_R8G8B8A8_UNORM) {
        Log::error(
            "Primitives",
            "Present: Expected image format VK_FORMAT_R8G8B8A8_UNORM"
        );
        return false;
    }

    if (imageObj.extentType != ExtentType::SwapchainRelative) {
        Log::error(
            "Primitives", "Present: Expected swapchain relative size"
        );
        return false;
    }

    imageObj.imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    imageObj.isSwapchainImage = true;

    image = {array.handles.front(), Type::Image};
    return true;
}

VkDescriptorSet Present::getLiveViewImage() const {
    return outDS;
}

// ============================================================================
// Shader - Code Generation
// ============================================================================

using std::print;

void Shader::generateCreate(const Store& store, std::ostream& out) const {
    assert(!name.empty());

    auto shaderPath = std::filesystem::path{"compiled_shaders"} / getSpirvPath();
    print(out,
        "// Shader: {0} (stage={1}, entryPoint={2})\n"
        "{{\n"
        "    auto {0}_path = std::filesystem::path{{\"{3}\"}}.string();\n"
        "    auto {0}_code = readFile({0}_path.c_str());\n"
        "    {0} = createShaderModule(device, {0}_code);\n"
        "}}\n\n",
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

// ============================================================================
// Pipeline - Code Generation
// ============================================================================

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
        print(out, "        }},\n");
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

    // Color blend attachments - count and check for depth
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
        print(out, "    // Color blend attachments\n    std::array {}_colorBlendAttachments = {{\n", name);
        for (StoreHandle hAttachment : rp.attachments) {
            const Attachment& a = store.attachments[hAttachment.handle];
            const Image& backingImage = store.images[a.image.handle];
            VkImageUsageFlags usage = backingImage.imageInfo.usage;
            if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) continue;

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
                "        }},\n",
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
            colorBlending.blendConstants[0], colorBlending.blendConstants[1],
            colorBlending.blendConstants[2], colorBlending.blendConstants[3]
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
            colorBlending.blendConstants[0], colorBlending.blendConstants[1],
            colorBlending.blendConstants[2], colorBlending.blendConstants[3]
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
        "    vkchk(vkCreatePipelineLayout(device, &{0}_layoutInfo, nullptr, &{0}_layout));\n\n",
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

    // Use shared render pass if available, otherwise use own
    StoreHandle effectiveRenderPass = sharedRenderPass.isValid() ? sharedRenderPass : renderPass;
    assert(effectiveRenderPass.isValid());
    const auto& rp{store.renderPasses[effectiveRenderPass.handle]};

    print(out, "    // Pipeline: {}\n", name);
    print(out, "    {{\n");

    // Begin render pass (only if this pipeline owns the render pass)
    if (beginsRenderPass) {
        print(out,
            "        VkRenderPassBeginInfo {0}_passInfo{{\n"
            "            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,\n"
            "            .renderPass = {1},\n"
            "            .framebuffer = {2},\n"
            "            .renderArea = {1}_renderArea,\n"
            "            .clearValueCount = static_cast<uint32_t>({1}_clearValues.size()),\n"
            "            .pClearValues = {1}_clearValues.data()\n"
            "        }};\n\n"
            "        vkCmdBeginRenderPass(cmdBuffer, &{0}_passInfo, VK_SUBPASS_CONTENTS_INLINE);\n\n",
            name, rp.name,
            rp.rendersToSwapchain(store) ? rp.name + "_framebuffers[imageInFlightIndex]" : rp.name + "_framebuffer"
        );
    }

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
        "        vkCmdSetViewport(cmdBuffer, 0, 1, &{0}_viewport);\n\n",
        name, rp.name
    );

    // Set scissor (dynamic)
    print(out,
        "        VkRect2D {0}_scissor{{\n"
        "            .offset = {{0, 0}},\n"
        "            .extent = {1}_renderArea.extent\n"
        "        }};\n"
        "        vkCmdSetScissor(cmdBuffer, 0, 1, &{0}_scissor);\n\n",
        name, rp.name
    );

    // Bind pipeline
    print(out, "        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, {});\n\n", name);

    // Find per-object descriptor set
    int perObjectDescSetIndex = -1;
    std::string perObjectDescSetName;
    for (size_t i = 0; i < descriptorSetHandles.size(); ++i) {
        if (!descriptorSetHandles[i].isValid()) continue;
        const auto& ds = store.descriptorSets[descriptorSetHandles[i].handle];
        const auto& dsBindings = ds.getBindings();
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

    // Bind descriptor sets
    if (!descriptorSetHandles.empty()) {
        print(out, "        std::array {}_descSets{{\n", name);
        for (const auto& dsHandle : descriptorSetHandles) {
            if (dsHandle.isValid()) {
                const auto& ds = store.descriptorSets[dsHandle.handle];
                print(out, "            {}_sets[imageInFlightIndex % {}_sets.size()],\n", ds.name, ds.name);
            }
        }
        print(out, "        }};\n");
        print(out,
            "        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,\n"
            "            {0}_layout, 0, static_cast<uint32_t>({0}_descSets.size()),\n"
            "            {0}_descSets.data(), 0, nullptr);\n\n",
            name
        );
    }

    // Draw
    if (vertexDataHandle.isValid()) {
        if (vertexDataHandle.type == Type::Array) {
            const auto& arr = store.arrays[vertexDataHandle.handle];
            if (!arr.handles.empty() && arr.type == Type::VertexData) {
                uint32_t geometryIndex = 0;
                for (uint32_t handle : arr.handles) {
                    const auto& vd = store.vertexDatas[handle];
                    if (vd.name.empty()) continue;

                    print(out, "        // Draw: {0}\n        {{\n", vd.name);

                    if (perObjectDescSetIndex >= 0) {
                        print(out,
                            "            // Rebind per-object descriptor set\n"
                            "            VkDescriptorSet {0}_perObjDescSet = {1}_sets[{2}];\n"
                            "            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,\n"
                            "                {0}_layout, {3}, 1, &{0}_perObjDescSet, 0, nullptr);\n",
                            name, perObjectDescSetName, geometryIndex, perObjectDescSetIndex
                        );
                    }

                    print(out,
                        "            VkBuffer vertexBuffers[] = {{{0}_vertexBuffer}};\n"
                        "            VkDeviceSize offsets[] = {{0}};\n"
                        "            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);\n",
                        vd.name
                    );

                    if (vd.indexCount > 0) {
                        print(out,
                            "            vkCmdBindIndexBuffer(cmdBuffer, {0}_indexBuffer, 0, VK_INDEX_TYPE_UINT32);\n"
                            "            vkCmdDrawIndexed(cmdBuffer, {0}_indexCount, 1, 0, 0, 0);\n",
                            vd.name
                        );
                    } else {
                        print(out, "            vkCmdDraw(cmdBuffer, {}_vertexCount, 1, 0, 0);\n", vd.name);
                    }
                    print(out, "        }}\n");
                    geometryIndex++;
                }
            }
        }
    } else {
        print(out, "        // Fullscreen triangle (no vertex buffer)\n");
        print(out, "        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);\n");
    }

    // End render pass (only if this pipeline ends the render pass)
    if (endsRenderPass) {
        print(out, "\n        vkCmdEndRenderPass(cmdBuffer);\n");
    }
    print(out, "    }}\n");
}

// ============================================================================
// RenderPass - Code Generation
// ============================================================================

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

    print(out, "// Render Pass: {}\n{{\n", name);
    print(out, "    std::array {}_attachmentDescs = {{\n", name);
    for (auto att: attachmentPtrs) print(out, "        {}_desc,\n", att->name);
    print(out, "    }};\n\n");

    bool depthInput{false}, colorInput{false}, swapChainInput{false}, swapChainRelativeExtent{false};
    uint32_t minHeight = UINT32_MAX, minWidth = UINT32_MAX;
    std::vector<VkAttachmentReference> colorRefs, depthRefs;
    colorRefs.reserve(attachments.size());
    depthRefs.reserve(1);

    auto attachmentsIdx = std::views::zip(std::views::iota(0), attachmentPtrs, imagePtrs);
    for (auto&& [binding, att, img] : attachmentsIdx) {
        const auto& usage = img->imageInfo.usage;
        bool isSampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
        const VkExtent3D& imageExtent = img->imageInfo.extent;
        if (imageExtent.height < minHeight) minHeight = imageExtent.height;
        if (imageExtent.width < minWidth) minWidth = imageExtent.width;
        if (img->extentType == ExtentType::SwapchainRelative) swapChainRelativeExtent = true;

        if (img->isSwapchainImage) {
            colorRefs.emplace_back(binding, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            swapChainInput = true;
            continue;
        }
        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            colorRefs.emplace_back(binding, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            colorInput |= isSampled;
        }
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthRefs.emplace_back(binding, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depthInput |= isSampled;
        }
    }

    if (!colorRefs.empty()) {
        print(out, "    std::array {}_colorRefs{{\n", name);
        for (const auto& ref : colorRefs)
            print(out, "        VkAttachmentReference{{.attachment = {}, .layout = {}}},\n",
                ref.attachment, string_VkImageLayout(ref.layout));
        print(out, "    }};\n\n");
    }

    if (!depthRefs.empty()) {
        print(out, "    std::array {}_depthRefs{{\n", name);
        for (const auto& ref : depthRefs)
            print(out, "        VkAttachmentReference{{.attachment = {}, .layout = {}}},\n",
                ref.attachment, string_VkImageLayout(ref.layout));
        print(out, "    }};\n\n");
    }

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

    print(out, "    std::array {}_subpassDeps{{\n", name);
    if (depthInput) {
        print(out,
            "        VkSubpassDependency{{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, "
            "VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}},\n"
            "        VkSubpassDependency{{0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, "
            "VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT}},\n");
    } else {
        print(out,
            "        VkSubpassDependency{{VK_SUBPASS_EXTERNAL, 0, "
            "VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, "
            "VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, "
            "VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, "
            "VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT}},\n");
    }
    if (colorInput) {
        print(out,
            "        VkSubpassDependency{{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, "
            "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_MEMORY_READ_BIT, "
            "VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT}},\n"
            "        VkSubpassDependency{{0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "
            "VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, "
            "VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT}},\n");
    } else {
        print(out,
            "        VkSubpassDependency{{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "
            "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, "
            "VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT}},\n");
    }
    print(out, "    }};\n");

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

    std::string extent_width = swapChainRelativeExtent ? "swapChainExtent.width" : std::format("{}", minWidth);
    std::string extent_height = swapChainRelativeExtent ? "swapChainExtent.height" : std::format("{}", minHeight);

    if (swapChainInput) {
        print(out, "    {0}_framebuffers.reserve(swapChainImages.size());\n"
                   "    for (size_t i = 0; i < swapChainImages.size(); i++) {{\n"
                   "        std::array views{{\n", name);
        for (auto img : imagePtrs) {
            if (img->isSwapchainImage) print(out, "            {}_views[i],\n", img->name);
            else print(out, "            {}_view,\n", img->name);
        }
        print(out,
            "        }};\n\n"
            "        VkFramebufferCreateInfo info{{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, "
            ".renderPass = {0}, .attachmentCount = views.size(), .pAttachments = views.data(), "
            ".width = {1}, .height = {2}, .layers = 1}};\n\n"
            "        VkFramebuffer framebuffer;\n"
            "        vkchk(vkCreateFramebuffer(device, &info, nullptr, &framebuffer));\n"
            "        {0}_framebuffers.push_back(framebuffer);\n"
            "    }}\n", name, extent_width, extent_height);
    } else {
        print(out, "    std::array {}_views{{\n", name);
        for (auto img : imagePtrs) print(out, "        {}_view,\n", img->name);
        print(out,
            "    }};\n\n"
            "    VkFramebufferCreateInfo fbufInfo{{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, "
            ".renderPass = {0}, .attachmentCount = {0}_views.size(), .pAttachments = {0}_views.data(), "
            ".width = {1}, .height = {2}, .layers = 1}};\n\n"
            "    vkchk(vkCreateFramebuffer(device, &fbufInfo, nullptr, &{0}_framebuffer));\n",
            name, extent_width, extent_height);
    }

    print(out, "    {0}_renderArea = VkRect2D{{.offset = {{0, 0}}, .extent = {{{1}, {2}}}}};\n", name, extent_width, extent_height);
    print(out, "    {}_clearValues = {{\n", name);
    for (const auto& attHandle : attachments) {
        const auto& att = store.attachments[attHandle.handle];
        const auto& img = store.images[att.image.handle];
        if (img.imageInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            print(out, "        VkClearValue{{.depthStencil = {{{:.6f}f, {}}}}},\n",
                att.clearValue.depthStencil.depth, att.clearValue.depthStencil.stencil);
        else
            print(out, "        VkClearValue{{.color = {{{{{:.6f}f, {:.6f}f, {:.6f}f, {:.6f}f}}}}}},\n",
                att.clearValue.color.float32[0], att.clearValue.color.float32[1],
                att.clearValue.color.float32[2], att.clearValue.color.float32[3]);
    }
    print(out, "    }};\n}}\n\n");
}

void RenderPass::generateDestroy(const Store& store, std::ostream& out) const {
    assert(!name.empty());
    assert(!attachments.empty());

    print(out, "    // Destroy RenderPass: {}\n", name);
    if (rendersToSwapchain(store)) {
        print(out,
            "    for (auto framebuffer : {0}_framebuffers)\n"
            "        vkDestroyFramebuffer(device, framebuffer, nullptr);\n"
            "    {0}_framebuffers.clear();\n", name);
    } else {
        print(out,
            "   if ({0}_framebuffer != VK_NULL_HANDLE) {{\n"
            "       vkDestroyFramebuffer(device, {0}_framebuffer, nullptr);\n"
            "       {0}_framebuffer = VK_NULL_HANDLE;\n"
            "   }}\n", name);
    }
    print(out,
        "   if ({0} != VK_NULL_HANDLE) {{\n"
        "       vkDestroyRenderPass(device, {0}, nullptr);\n"
        "       {0} = VK_NULL_HANDLE;\n"
        "   }}\n\n", name);
}

} // namespace primitives
