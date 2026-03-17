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
        if (!extraData) {
            Log::error("Primitives", "UniformBuffer::recordCommands - Camera UBO missing extraData");
            return;
        }
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

    // Validate mapped pointer before use
    if (!mapped) {
        Log::error("Primitives", "UniformBuffer::recordCommands - buffer not mapped");
        return;
    }
    if (data.empty()) {
        Log::error("Primitives", "UniformBuffer::recordCommands - data is empty");
        return;
    }

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

    if (!ubo.isValid()) {
        Log::error("Primitives", "Camera::recordCommands - invalid UBO handle");
        return;
    }
    if (ubo.handle >= store.uniformBuffers.size()) {
        Log::error("Primitives", "Camera::recordCommands - UBO handle out of bounds");
        return;
    }

    auto& uniformBuffer = store.uniformBuffers[ubo.handle];
    if (!uniformBuffer.mapped) {
        Log::error("Primitives", "Camera::recordCommands - UBO not mapped");
        return;
    }
    if (uniformBuffer.data.empty()) {
        Log::error("Primitives", "Camera::recordCommands - UBO data is empty");
        return;
    }

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

    if (ubo.handle >= store.uniformBuffers.size()) {
        Log::error("Primitives", "Light::recordCommands - UBO handle out of bounds");
        return;
    }

    auto& uniformBuffer = store.uniformBuffers[ubo.handle];
    if (!uniformBuffer.mapped) {
        Log::error("Primitives", "Light::recordCommands - UBO not mapped");
        return;
    }
    if (uniformBuffer.data.empty()) {
        Log::error("Primitives", "Light::recordCommands - UBO data is empty");
        return;
    }

    memcpy(uniformBuffer.mapped, uniformBuffer.data.data(), uniformBuffer.data.size());
}

void Light::generateRecordCommands(
    const Store& store,
    std::ostream& out
) const {
    // Fixed lights don't need runtime updates - data is static
    // If we wanted dynamic lights, we'd generate update code here
}

// ============================================================================
// UniformBuffer - Code Generation
// ============================================================================

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

                // Generate light buffer initialization with header + array
                // Buffer format: LightsHeader (16 bytes) + Light[numLights]
                print(out, "\n    // Initialize light UBO with header + {} lights\n", light.numLights);

                // Generate the header struct (must match shader's Lights.numLights layout)
                print(out,
                    "    struct LightsHeader {{\n"
                    "        int32_t numLights;\n"
                    "        int32_t _pad[3];\n"
                    "    }};\n"
                );

                // Generate header initialization
                print(out, "    LightsHeader {}_header{{ .numLights = {} }};\n", name, light.numLights);

                // Generate light array initialization
                print(out, "    std::array<Light, {}> {}_lights{{{{\n", light.numLights, name);
                for (int i = 0; i < light.numLights && i < static_cast<int>(light.lights.size()); ++i) {
                    const auto& l = light.lights[i];
                    print(out,
                        "        Light{{\n"
                        "            .position = glm::vec3({}, {}, {}),\n"
                        "            .radius = {},\n"
                        "            .color = glm::vec3({}, {}, {}),\n"
                        "            .intensity = {}\n"
                        "        }}{}\n",
                        flt(l.position.x), flt(l.position.y), flt(l.position.z),
                        flt(l.radius),
                        flt(l.color.x), flt(l.color.y), flt(l.color.z),
                        flt(l.intensity),
                        (i < light.numLights - 1) ? "," : ""
                    );
                }
                print(out, "    }}}};\n");

                // Copy header first, then light array
                print(out, "    memcpy({}_mapped, &{}_header, sizeof(LightsHeader));\n", name, name);
                print(out, "    memcpy(static_cast<uint8_t*>({}_mapped) + sizeof(LightsHeader), {}_lights.data(), sizeof({}_lights));\n", name, name, name);
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

} // namespace primitives
