// UniformBuffer, Camera, Light primitive implementations
#include "common.h"

namespace primitives {

using std::print;

// ============================================================================
// UniformBuffer
// ============================================================================

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

// ============================================================================
// Camera
// ============================================================================

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

// ============================================================================
// Light
// ============================================================================

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

} // namespace primitives
