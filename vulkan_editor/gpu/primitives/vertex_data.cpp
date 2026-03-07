// VertexData primitive implementation
#include "common.h"

namespace primitives {

using std::print;

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

} // namespace primitives
