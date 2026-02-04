#include "primitive_generator.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <print>
#include <set>

using std::print;

// ============================================================================
// Struct generation helpers
// ============================================================================

std::string PrimitiveGenerator::shaderTypeToCpp(const std::string& typeName) const {
    // Handle common shader types -> GLM types
    if (typeName == "float" || typeName == "float1") return "float";
    if (typeName == "float2" || typeName == "vec2") return "glm::vec2";
    if (typeName == "float3" || typeName == "vec3") return "glm::vec3";
    if (typeName == "float4" || typeName == "vec4") return "glm::vec4";

    if (typeName == "int" || typeName == "int1") return "int32_t";
    if (typeName == "int2" || typeName == "ivec2") return "glm::ivec2";
    if (typeName == "int3" || typeName == "ivec3") return "glm::ivec3";
    if (typeName == "int4" || typeName == "ivec4") return "glm::ivec4";

    if (typeName == "uint" || typeName == "uint1") return "uint32_t";
    if (typeName == "uint2" || typeName == "uvec2") return "glm::uvec2";
    if (typeName == "uint3" || typeName == "uvec3") return "glm::uvec3";
    if (typeName == "uint4" || typeName == "uvec4") return "glm::uvec4";

    if (typeName == "bool") return "uint32_t"; // GLSL bools are 4 bytes

    // Matrices (column-major)
    if (typeName == "float2x2" || typeName == "mat2" || typeName == "mat2x2") return "glm::mat2";
    if (typeName == "float3x3" || typeName == "mat3" || typeName == "mat3x3") return "glm::mat3";
    if (typeName == "float4x4" || typeName == "mat4" || typeName == "mat4x4") return "glm::mat4";
    if (typeName == "float2x3" || typeName == "mat2x3") return "glm::mat2x3";
    if (typeName == "float2x4" || typeName == "mat2x4") return "glm::mat2x4";
    if (typeName == "float3x2" || typeName == "mat3x2") return "glm::mat3x2";
    if (typeName == "float3x4" || typeName == "mat3x4") return "glm::mat3x4";
    if (typeName == "float4x2" || typeName == "mat4x2") return "glm::mat4x2";
    if (typeName == "float4x3" || typeName == "mat4x3") return "glm::mat4x3";

    // If unknown, return as-is (might be a custom struct type)
    return typeName;
}

int PrimitiveGenerator::getTypeAlignment(const std::string& typeName) const {
    // std140 alignment rules
    if (typeName == "float" || typeName == "float1" ||
        typeName == "int" || typeName == "int1" ||
        typeName == "uint" || typeName == "uint1" ||
        typeName == "bool") {
        return 4;
    }
    if (typeName == "float2" || typeName == "vec2" ||
        typeName == "int2" || typeName == "ivec2" ||
        typeName == "uint2" || typeName == "uvec2") {
        return 8;
    }
    // vec3 and vec4 align to 16 in std140
    if (typeName == "float3" || typeName == "vec3" ||
        typeName == "float4" || typeName == "vec4" ||
        typeName == "int3" || typeName == "ivec3" ||
        typeName == "int4" || typeName == "ivec4" ||
        typeName == "uint3" || typeName == "uvec3" ||
        typeName == "uint4" || typeName == "uvec4") {
        return 16;
    }
    // Matrices align to vec4 (16 bytes)
    if (typeName.find("mat") != std::string::npos ||
        typeName.find("float2x") != std::string::npos ||
        typeName.find("float3x") != std::string::npos ||
        typeName.find("float4x") != std::string::npos) {
        return 16;
    }
    // Default to 16 for structs/unknown
    return 16;
}

int PrimitiveGenerator::getTypeSize(const std::string& typeName) const {
    if (typeName == "float" || typeName == "float1" ||
        typeName == "int" || typeName == "int1" ||
        typeName == "uint" || typeName == "uint1" ||
        typeName == "bool") {
        return 4;
    }
    if (typeName == "float2" || typeName == "vec2" ||
        typeName == "int2" || typeName == "ivec2" ||
        typeName == "uint2" || typeName == "uvec2") {
        return 8;
    }
    if (typeName == "float3" || typeName == "vec3" ||
        typeName == "int3" || typeName == "ivec3" ||
        typeName == "uint3" || typeName == "uvec3") {
        return 12;
    }
    if (typeName == "float4" || typeName == "vec4" ||
        typeName == "int4" || typeName == "ivec4" ||
        typeName == "uint4" || typeName == "uvec4") {
        return 16;
    }
    // Matrices
    if (typeName == "float2x2" || typeName == "mat2" || typeName == "mat2x2") return 32;  // 2 * vec4
    if (typeName == "float3x3" || typeName == "mat3" || typeName == "mat3x3") return 48;  // 3 * vec4
    if (typeName == "float4x4" || typeName == "mat4" || typeName == "mat4x4") return 64;  // 4 * vec4
    if (typeName == "float2x3" || typeName == "mat2x3") return 32;
    if (typeName == "float2x4" || typeName == "mat2x4") return 32;
    if (typeName == "float3x2" || typeName == "mat3x2") return 48;
    if (typeName == "float3x4" || typeName == "mat3x4") return 48;
    if (typeName == "float4x2" || typeName == "mat4x2") return 64;
    if (typeName == "float4x3" || typeName == "mat4x3") return 64;

    return 16; // Default for unknown
}

void PrimitiveGenerator::generateStructDefinition(
    const ShaderTypes::StructInfo& structInfo,
    std::ostream& out
) const {
    print(out, "struct {} {{\n", structInfo.structName);

    int currentOffset = 0;
    int padIndex = 0;

    for (const auto& member : structInfo.members) {
        std::string cppType = shaderTypeToCpp(member.typeName);
        int alignment = getTypeAlignment(member.typeName);
        int size = getTypeSize(member.typeName);

        if (member.offset > currentOffset) {
            int paddingBytes = member.offset - currentOffset;
            if (paddingBytes == 4) {
                print(out, "    float _pad{}{{0.0f}};\n", padIndex++);
            } else if (paddingBytes == 8) {
                print(out, "    glm::vec2 _pad{}{{0.0f}};\n", padIndex++);
            } else if (paddingBytes == 12) {
                print(out, "    glm::vec3 _pad{}{{0.0f}};\n", padIndex++);
            } else if (paddingBytes == 16) {
                print(out, "    glm::vec4 _pad{}{{0.0f}};\n", padIndex++);
            } else if (paddingBytes > 0) {
                print(out, "    uint8_t _pad{}[{}]{{}};\n", padIndex++, paddingBytes);
            }
            currentOffset = member.offset;
        }

        if (alignment >= 16) {
            print(out, "    alignas(16) ");
        } else {
            print(out, "    ");
        }

        if (member.arraySize > 0) {
            print(out, "{} {}[{}]{{}};\n", cppType, member.name, member.arraySize);
            currentOffset += size * member.arraySize;
        } else {
            print(out, "{} {}{{}};\n", cppType, member.name);
            currentOffset += size;
        }
    }

    print(out, "}};\n");
}

void PrimitiveGenerator::generateAllStructs(
    const ShaderTypes::ShaderParsedResult& parsed,
    std::ostream& out
) const {
    print(out, "#include <glm/glm.hpp>\n");
    print(out, "#include <cstdint>\n\n");

    for (const auto& s : parsed.cameraStructs) {
        generateStructDefinition(s, out);
        print(out, "\n");
    }
    for (const auto& s : parsed.lightStructs) {
        generateStructDefinition(s, out);
        print(out, "\n");
    }
    for (const auto& s : parsed.customStructs) {
        generateStructDefinition(s, out);
        print(out, "\n");
    }
}

// ============================================================================
// Main generation methods - delegate to GenerateNode virtual methods
// ============================================================================

void PrimitiveGenerator::generateAll(
    const primitives::Store& store,
    std::ostream& out
) const {
    // Collect all unique model paths for async loading
    std::set<std::string> uniqueModelPaths;
    for (const auto& vd : store.vertexDatas) {
        if (vd.name.empty() || vd.modelFilePath.empty())
            continue;
        uniqueModelPaths.insert(vd.modelFilePath);
    }

    // Collect all unique image paths for async loading
    std::set<std::string> uniqueImagePaths;
    for (const auto& img : store.images) {
        if (img.name.empty() || img.isSwapchainImage || img.originalImagePath.empty())
            continue;
        // Only collect images that will be staged (sampled textures)
        bool isSampledTexture = (img.imageInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
                                (img.imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
        if (isSampledTexture) {
            uniqueImagePaths.insert(img.originalImagePath);
        }
    }

    // Generate async model loading code if we have models (with caching for resize)
    if (!uniqueModelPaths.empty()) {
        print(out, "// Load all models asynchronously in parallel (cached for resize)\n");
        print(out, "static std::unordered_map<std::string, ModelData> cachedModels;\n");
        print(out, "if (cachedModels.empty()) {{\n");
        print(out, "    std::vector<std::string> modelPaths = {{\n");
        for (const auto& path : uniqueModelPaths) {
            print(out, "        \"{}\",\n", path);
        }
        print(out, "    }};\n");
        print(out, "    cachedModels = loadModelsAsync(modelPaths);\n");
        print(out, "}}\n");
        print(out, "auto& loadedModels = cachedModels;\n\n");

        // Create references to individual models for easier access
        for (const auto& path : uniqueModelPaths) {
            print(out, "ModelData& {} = loadedModels[\"{}\"];\n",
                  modelPathToVarName(path), path);
        }
        print(out, "\n");
    }

    // Generate async image loading code if we have images (with caching for resize)
    if (!uniqueImagePaths.empty()) {
        print(out, "// Load all images asynchronously in parallel (cached for resize)\n");
        print(out, "static std::unordered_map<std::string, LoadedImage> cachedImages;\n");
        print(out, "if (cachedImages.empty()) {{\n");
        print(out, "    std::vector<std::string> imagePaths = {{\n");
        for (const auto& path : uniqueImagePaths) {
            print(out, "        \"{}\",\n", path);
        }
        print(out, "    }};\n");
        print(out, "    cachedImages = loadImagesAsync(imagePaths);\n");
        print(out, "}}\n");
        print(out, "auto& loadedImages = cachedImages;\n\n");
    }

    // First pass: create all resources
    for (auto node : store.getGenerateNodes())
        node->generateCreate(store, out);

    // Second pass: stage resources that need data upload (textures, etc.)
    for (auto node : store.getGenerateNodes())
        node->generateStage(store, out);

    // Ensure all staging operations complete before rendering begins
    // This prevents partial rendering on first frames due to GPU lazy initialization
    print(out, "// Ensure all GPU operations complete before rendering\n");
    print(out, "vkDeviceWaitIdle(device);\n\n");

    // Note: Loaded images are cached in static variables and NOT freed here
    // They remain available for resize events that need to re-stage to GPU
}


void PrimitiveGenerator::generateAllRecordCommands(
    const primitives::Store& store,
    std::ostream& out
) const {
    for (auto node : store.getGenerateNodes())
        node->generateRecordCommands(store, out);
}

void PrimitiveGenerator::generateAllDestroy(
    const primitives::Store& store,
    std::ostream& out
) const {
    for (auto node : store.getGenerateNodes() | std::views::reverse)
        node->generateDestroy(store, out);
}

// ============================================================================
// Variable definitions generation
// ============================================================================

void PrimitiveGenerator::generateDefinitions(
    const primitives::Store& store,
    std::ostream& out
) const {
    // Images
    for (const auto& img : store.images) {
        if (img.name.empty())
            continue;

        if (img.isSwapchainImage) {
            print(out, "std::vector<VkImageView> {}_views{{}};\n", img.name);
        } else {
            print(out, "VkImage {} = VK_NULL_HANDLE;\n", img.name);
            print(out, "VkImageView {}_view = VK_NULL_HANDLE;\n", img.name);
            print(out, "VmaAllocation {}_alloc = VK_NULL_HANDLE;\n\n", img.name);
        }
    }

    // Vertex data
    for (const auto& vd : store.vertexDatas) {
        if (vd.name.empty())
            continue;

        print(out, "VkBuffer {}_vertexBuffer = VK_NULL_HANDLE;\n", vd.name);
        print(out, "VkBuffer {}_indexBuffer = VK_NULL_HANDLE;\n", vd.name);
        print(out, "VmaAllocation {}_vertexAlloc = VK_NULL_HANDLE;\n", vd.name);
        print(out, "VmaAllocation {}_indexAlloc = VK_NULL_HANDLE;\n", vd.name);
        // When using model files, counts are set at runtime during loading
        if (!vd.modelFilePath.empty()) {
            print(out, "uint32_t {}_vertexCount = 0;\n", vd.name);
            print(out, "uint32_t {}_indexCount = 0;\n", vd.name);
        } else {
            print(out, "uint32_t {}_vertexCount = {};\n", vd.name, vd.vertexCount);
            print(out, "uint32_t {}_indexCount = {};\n", vd.name, vd.indexCount);
        }
        print(out, "VkDeviceSize {}_vertexDataSize = {};\n", vd.name, vd.vertexDataSize);
        print(out, "VkDeviceSize {}_indexDataSize = {};\n\n", vd.name, vd.indexDataSize);
    }

    // Uniform buffers
    for (const auto& ub : store.uniformBuffers) {
        if (ub.name.empty())
            continue;

        print(out, "VkBuffer {} = VK_NULL_HANDLE;\n", ub.name);
        print(out, "VmaAllocation {}_alloc = VK_NULL_HANDLE;\n", ub.name);
        print(out, "void* {}_mapped = nullptr;\n", ub.name);
        print(out, "VkDeviceSize {}_size = {};\n\n", ub.name, ub.data.size());
    }

    // Shaders
    for (const auto& sh : store.shaders) {
        if (sh.name.empty())
            continue;

        print(out, "VkShaderModule {} = VK_NULL_HANDLE;\n", sh.name);
        print(out, "const VkShaderStageFlagBits {}_stage = {};\n", sh.name, string_VkShaderStageFlagBits(sh.stage));
        print(out, "const char* {}_entryPoint = \"{}\";\n\n", sh.name, sh.entryPoint.empty() ? "main" : sh.entryPoint);
    }

    // Descriptor pools
    for (const auto& dp : store.descriptorPools) {
        if (dp.name.empty())
            continue;

        print(out, "VkDescriptorPool {} = VK_NULL_HANDLE;\n", dp.name);
    }
    if (!store.descriptorPools.empty()) {
        print(out, "\n");
    }

    // Descriptor sets
    for (const auto& ds : store.descriptorSets) {
        if (ds.name.empty())
            continue;

        print(out, "VkDescriptorSetLayout {}_layout = VK_NULL_HANDLE;\n", ds.name);
        print(out, "std::vector<VkDescriptorSet> {}_sets;\n", ds.name);
        for (const auto& binding : ds.expectedBindings) {
            if (binding.type == primitives::Type::Image) {
                print(out, "VkSampler {}_sampler_{} = VK_NULL_HANDLE;\n", ds.name, binding.binding);
            }
        }
        print(out, "\n");
    }

    // Render passes
    for (const auto& rp : store.renderPasses) {
        if (rp.name.empty())
            continue;

        print(out, "VkRenderPass {} = VK_NULL_HANDLE;\n", rp.name);
        print(out, "VkExtent2D {}_extent{{}};\n", rp.name);
        print(out, "VkRect2D {}_renderArea{{}};\n", rp.name);
        print(out, "std::vector<VkClearValue> {}_clearValues{{}};\n\n", rp.name);

        if (rp.rendersToSwapchain(store))
            print(out, "std::vector<VkFramebuffer> {}_framebuffers{{}};\n", rp.name);
        else
            print(out, "VkFramebuffer {}_framebuffer = VK_NULL_HANDLE;\n", rp.name);
    }

    // Pipelines
    for (const auto& pl : store.pipelines) {
        if (pl.name.empty())
            continue;

        print(out, "VkPipeline {} = VK_NULL_HANDLE;\n", pl.name);
        print(out, "VkPipelineLayout {}_layout = VK_NULL_HANDLE;\n\n", pl.name);
    }
}
