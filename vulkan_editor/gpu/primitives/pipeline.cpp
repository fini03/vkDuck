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

    if (!vertexDataHandle.isValid()) {
        vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmdBuffer);
        return;
    }

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
            vkCmdEndRenderPass(cmdBuffer);
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

    vkCmdEndRenderPass(cmdBuffer);
}

bool Pipeline::connectLink(
    const LinkSlot& slot,
    Store& store
) {
    if (!slot.handle.isValid()) {
        Log::error("Primitives", "Pipeline: Invalid slot handle");
        return false;
    }

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

        attachmentIndex++;
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

} // namespace primitives
