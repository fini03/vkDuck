// Store class implementation
#include "common.h"

namespace primitives {

void Store::reset() {
    // Reset used primitives to default state. This properly cleans up
    // std::string/std::vector members via move assignment.
    // Note: destroy() must be called before reset() to release GPU resources.
    for (uint32_t i = 0; i < arrayCount; ++i)
        arrays[i] = Array{};
    for (uint32_t i = 0; i < vertexDataCount; ++i)
        vertexDatas[i] = VertexData{};
    for (uint32_t i = 0; i < uniformBufferCount; ++i)
        uniformBuffers[i] = UniformBuffer{};
    for (uint32_t i = 0; i < cameraCount; ++i)
        cameras[i] = Camera{};
    for (uint32_t i = 0; i < lightCount; ++i)
        lights[i] = Light{};
    for (uint32_t i = 0; i < descriptorPoolCount; ++i)
        descriptorPools[i] = DescriptorPool{};
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
        descriptorSets[i] = DescriptorSet{};
    for (uint32_t i = 0; i < renderPassCount; ++i)
        renderPasses[i] = RenderPass{};
    for (uint32_t i = 0; i < pipelineCount; ++i)
        pipelines[i] = Pipeline{};
    for (uint32_t i = 0; i < shaderCount; ++i)
        shaders[i] = Shader{};
    for (uint32_t i = 0; i < imageCount; ++i)
        images[i] = Image{};
    for (uint32_t i = 0; i < attachmentCount; ++i)
        attachments[i] = Attachment{};
    for (uint32_t i = 0; i < presentCount; ++i)
        presents[i] = Present{};

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
    state = StoreState::Linked;
}

std::vector<Node*> Store::getNodes() {
    using std::views::take;

    std::vector<Node*> nodes;
    nodes.reserve(
        descriptorPoolCount + imageCount + attachmentCount +
        renderPassCount + uniformBufferCount + cameraCount +
        lightCount + descriptorSetCount + vertexDataCount + shaderCount +
        pipelineCount + presentCount
    );

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
    using std::views::take;

    std::vector<const GenerateNode*> nodes;
    nodes.reserve(
        descriptorPoolCount + imageCount + attachmentCount +
        renderPassCount + uniformBufferCount + cameraCount +
        lightCount + descriptorSetCount + vertexDataCount + shaderCount +
        pipelineCount
    );

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
    if (!handle.isValid()) {
        Log::error("Store", "getNode called with invalid handle");
        return nullptr;
    }

    switch (handle.type) {
    case Type::Array:
        if (handle.handle >= arrayCount) {
            Log::error("Store", "Array handle {} out of bounds (count: {})", handle.handle, arrayCount);
            return nullptr;
        }
        return &arrays[handle.handle];
    case Type::VertexData:
        if (handle.handle >= vertexDataCount) {
            Log::error("Store", "VertexData handle {} out of bounds (count: {})", handle.handle, vertexDataCount);
            return nullptr;
        }
        return &vertexDatas[handle.handle];
    case Type::UniformBuffer:
        if (handle.handle >= uniformBufferCount) {
            Log::error("Store", "UniformBuffer handle {} out of bounds (count: {})", handle.handle, uniformBufferCount);
            return nullptr;
        }
        return &uniformBuffers[handle.handle];
    case Type::Camera:
        if (handle.handle >= cameraCount) {
            Log::error("Store", "Camera handle {} out of bounds (count: {})", handle.handle, cameraCount);
            return nullptr;
        }
        return &cameras[handle.handle];
    case Type::Light:
        if (handle.handle >= lightCount) {
            Log::error("Store", "Light handle {} out of bounds (count: {})", handle.handle, lightCount);
            return nullptr;
        }
        return &lights[handle.handle];
    case Type::DescriptorPool:
        if (handle.handle >= descriptorPoolCount) {
            Log::error("Store", "DescriptorPool handle {} out of bounds (count: {})", handle.handle, descriptorPoolCount);
            return nullptr;
        }
        return &descriptorPools[handle.handle];
    case Type::DescriptorSet:
        if (handle.handle >= descriptorSetCount) {
            Log::error("Store", "DescriptorSet handle {} out of bounds (count: {})", handle.handle, descriptorSetCount);
            return nullptr;
        }
        return &descriptorSets[handle.handle];
    case Type::RenderPass:
        if (handle.handle >= renderPassCount) {
            Log::error("Store", "RenderPass handle {} out of bounds (count: {})", handle.handle, renderPassCount);
            return nullptr;
        }
        return &renderPasses[handle.handle];
    case Type::Attachment:
        if (handle.handle >= attachmentCount) {
            Log::error("Store", "Attachment handle {} out of bounds (count: {})", handle.handle, attachmentCount);
            return nullptr;
        }
        return &attachments[handle.handle];
    case Type::Image:
        if (handle.handle >= imageCount) {
            Log::error("Store", "Image handle {} out of bounds (count: {})", handle.handle, imageCount);
            return nullptr;
        }
        return &images[handle.handle];
    case Type::Pipeline:
        if (handle.handle >= pipelineCount) {
            Log::error("Store", "Pipeline handle {} out of bounds (count: {})", handle.handle, pipelineCount);
            return nullptr;
        }
        return &pipelines[handle.handle];
    case Type::Shader:
        if (handle.handle >= shaderCount) {
            Log::error("Store", "Shader handle {} out of bounds (count: {})", handle.handle, shaderCount);
            return nullptr;
        }
        return &shaders[handle.handle];
    case Type::Present:
        if (handle.handle >= presentCount) {
            Log::error("Store", "Present handle {} out of bounds (count: {})", handle.handle, presentCount);
            return nullptr;
        }
        return &presents[handle.handle];
    case Type::Invalid:
        Log::error("Store", "getNode called with Invalid type");
        return nullptr;
    }

    Log::error("Store", "getNode: unknown handle type {}", static_cast<int>(handle.type));
    return nullptr;
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

} // namespace primitives
