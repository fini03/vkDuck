#pragma once
#include "vulkan_editor/ui/pipeline_settings.h"
#include <array>
#include <glm/glm.hpp>
#include <span>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

/**
 * @namespace primitives
 * @brief GPU resource primitives for Vulkan rendering and code generation.
 *
 * Provides typed handles for GPU objects (buffers, images, pipelines) with
 * create/stage/destroy lifecycle and code generation support via GenerateNode.
 */
namespace primitives {

enum class Type : uint8_t {
    Array,
    VertexData,
    UniformBuffer,
    Camera,
    Light,
    DescriptorPool,
    DescriptorSet,
    RenderPass,
    Attachment,
    Image,
    Pipeline,
    Shader,
    Present,
    Invalid
};

enum class CameraType : uint8_t { Fixed, FPS, Orbital };

enum class UniformDataType : uint8_t {
    Camera,
    Light,
    Other
};

struct CameraData {
    alignas(16) glm::mat4 view{1.0f};
    alignas(16) glm::mat4 invView{1.0f};
    alignas(16) glm::mat4 proj{1.0f};
};

struct alignas(16) LightData {
    glm::vec3 position{0.0f, 2.0f, 0.0f};
    float radius{5.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct StoreHandle {
    uint32_t handle;
    Type type;

    StoreHandle() {
        handle = UINT32_MAX;
        type = Type::Invalid;
    }

    StoreHandle(
        uint32_t handle,
        Type type
    )
        : handle(handle)
        , type(type) {}

    bool operator==(const StoreHandle& o) const {
        return handle == o.handle && type == o.type;
    }

    bool isValid() const {
        return handle != UINT16_MAX && type != Type::Invalid;
    }
};

struct LinkSlot {
    StoreHandle handle;
    uint32_t slot;
};

struct DescriptorInfo {
    Type type;
    uint32_t binding;
    VkShaderStageFlags stages;
    VkSamplerCreateInfo samplerInfo;
    uint32_t arrayCount = 1;  // Number of descriptors (for arrays like lights[6])
};

struct Store;


// TODO: Add reasonable default constructors for all primitives?
//       Or is this handled by the UI generating the node data?
// TODO: Any way we can design this nicely so that we don't have
//       to recreate the full primitive structure if we update
//       a small thing in the UI?
// TODO: How do final renderpass extents/image extents correlate
//       and how should we update them properly?

class Node {
public:
    std::string name{};

    virtual ~Node() {}
    virtual bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) {
        return true;
    }
    virtual void stage(
        VkDevice device,
        VmaAllocator allocator,
        VkQueue queue,
        VkCommandPool cmdPool
    ) {}
    virtual void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) {}
    virtual void recordCommands(
        const Store& store,
        VkCommandBuffer cmdBuffer
    ) const {}
    virtual bool connectLink(
        const LinkSlot& slot,
        Store& store
    ) {
        return false;
    }
};

class GenerateNode {
public:
    virtual ~GenerateNode() {}
    virtual void generateCreate(const Store& store, std::ostream& out) const {}
    virtual void generateStage(const Store& store, std::ostream& out) const {}
    virtual void generateDestroy(const Store& store, std::ostream& out) const {}
    virtual void generateRecordCommands(const Store& store, std::ostream& out) const {}
};

class Array : public Node, public GenerateNode {
public:
    Type type;
    std::vector<uint32_t> handles;
};

class VertexData : public Node, public GenerateNode {
public:
    // CREATE
    std::span<uint8_t> vertexData{};
    std::span<uint32_t> indexData{};
    VkDeviceSize vertexDataSize{0};
    VkDeviceSize indexDataSize{0};

    // Vertex input description
    VkVertexInputBindingDescription bindingDescription{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

    // For code generation: path to exported binary model data files
    std::string vertexDataBinPath{};
    std::string indexDataBinPath{};

    // For code generation: original model file path and geometry index
    std::string modelFilePath{};
    uint32_t geometryIndex{0};

    // RECORD
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VmaAllocation vertexAllocation{VK_NULL_HANDLE};
    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VmaAllocation indexAllocation{VK_NULL_HANDLE};

    uint32_t vertexCount{0};
    uint32_t indexCount{0};

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void stage(
        VkDevice device,
        VmaAllocator allocator,
        VkQueue queue,
        VkCommandPool cmdPool
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;
};

class DescriptorPool : public Node, public GenerateNode {
public:
    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void registerSet(StoreHandle set);
    VkDescriptorPool getPool() const;
    const std::vector<StoreHandle>& getSets() const;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;

private:
    VkDescriptorPool pool{VK_NULL_HANDLE};
    std::vector<StoreHandle> sets;
};

/// The most interesting thing for when we create an image is probably
/// the extent and the image format, at least from the perspective of
/// frame buffer images.
class Image : public Node, public GenerateNode {
public:
    // CREATE
    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .priority = 1.0f
    };
    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange = {
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    ExtentType extentType{};
    bool isSwapchainImage{false};

    // If we have an externally provided image
    void* imageData{nullptr};
    VkDeviceSize imageSize{0};

    // For code generation: path to exported binary texture data file (legacy)
    std::string imageDataBinPath{};

    // For code generation: path to original image file (PNG, etc.) for wuffs loading
    std::string originalImagePath{};

    // RECORD
    VkImage image{VK_NULL_HANDLE};
    VmaAllocation alloc{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void stage(
        VkDevice device,
        VmaAllocator allocator,
        VkQueue queue,
        VkCommandPool cmdPool
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void updateSwapchainExtent(const VkExtent3D& extent);

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateCreateSwapchain(const Store& store, std::ostream& out) const;
    void generateStage(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;
};

class Attachment : public Node, public GenerateNode {
public:
    // CREATE
    VkAttachmentDescription desc{
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE
    };
    VkPipelineColorBlendAttachmentState colorBlending{
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    // RECORD
    VkClearValue clearValue{};


    // NOTE: The image format has to match the attachment format,
    // so technically it is specified twice since we also tie it to
    // the backing image here. We technically don't need the backing
    // image for creation so we are introducing stronger coupling than
    // we have to for the sake of simplicity.
    StoreHandle image{};

    void generateCreate(const Store& store, std::ostream& out) const override;
};

class UniformBuffer : public Node, public GenerateNode {
public:
    // CREATE
    UniformDataType dataType{UniformDataType::Other};
    std::span<uint8_t> data{};
    const void* extraData{nullptr};

    // RECORD
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    void* mapped{nullptr}; // Keep it mapped for updates

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void recordCommands(
        const Store& store,
        VkCommandBuffer cmdBuffer
    ) const override;

    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateRecordCommands(
        const Store& store,
        std::ostream& out
    ) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;
};

class Camera : public Node, public GenerateNode {
public:
    CameraType cameraType{CameraType::Fixed};
    StoreHandle ubo{};

    // Camera position/orientation (for code generation)
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    // FPS/Orbital parameters
    float yaw{0.0f};
    float pitch{0.0f};
    float distance{5.0f}; // Orbital only

    // Control speeds
    float moveSpeed{5.0f};
    float rotateSpeed{0.005f};
    float zoomSpeed{0.5f};

    // Projection parameters
    float fov{45.0f};
    float nearPlane{0.1f};
    float farPlane{1000.0f};

    bool isFixed() const {
        return cameraType == CameraType::Fixed;
    }
    bool isFPS() const {
        return cameraType == CameraType::FPS;
    }
    bool isOrbital() const {
        return cameraType == CameraType::Orbital;
    }

    void recordCommands(
        const Store& store,
        VkCommandBuffer cmdBuffer
    ) const override;
    void generateRecordCommands(
        const Store& store,
        std::ostream& out
    ) const override;
};

class Light : public Node, public GenerateNode {
public:
    StoreHandle ubo{};

    // Light parameters (for code generation)
    std::vector<LightData> lights;
    int numLights{1};

    void recordCommands(
        const Store& store,
        VkCommandBuffer cmdBuffer
    ) const override;
    void generateRecordCommands(
        const Store& store,
        std::ostream& out
    ) const override;
};

struct PoolSizeContribution {
    uint32_t imageCount{0};
    uint32_t uniformBufferCount{0};
    uint32_t setCount{0};
};

class DescriptorSet : public Node, public GenerateNode {
public:
    StoreHandle pool{};
    std::vector<DescriptorInfo> expectedBindings{};

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    bool connectLink(
        const LinkSlot& slot,
        Store& store
    ) override;

    const std::vector<VkDescriptorSet>& getSets() const;
    const std::vector<StoreHandle>& getBindings() const;
    uint32_t cardinality(const Store& store) const;
    VkDescriptorSetLayout getLayout() const;

    /// Calculate this set's contribution to a descriptor pool's size.
    /// Uses cardinality from store for runtime, or cardinalityOverride for code generation.
    PoolSizeContribution getPoolSizeContribution(
        const Store& store,
        uint32_t cardinalityOverride = 0
    ) const;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;

private:
    VkDescriptorSetLayout layout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> sets;

    std::vector<StoreHandle> bindings{};
    std::vector<VkSampler> samplers{};
    std::vector<VkBuffer> buffers{};
};

class Shader : public Node, public GenerateNode {
public:
    // RECORD
    std::span<uint32_t> code{};
    VkShaderStageFlagBits stage{VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM};
    VkShaderModule module{VK_NULL_HANDLE};
    std::string entryPoint{"main"}; // Shader entry point name

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;

    std::filesystem::path getSpirvPath() const;
};

class Pipeline : public Node, public GenerateNode {
public:
    // CREATE
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };
    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .blendConstants = {0.0, 0.0, 0.0, 0.0}
    };
    std::vector<StoreHandle> descriptorSetHandles{};
    std::vector<StoreHandle> shaders{};
    StoreHandle renderPass{};
    StoreHandle vertexDataHandle{};

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void recordCommands(
        const Store& store,
        VkCommandBuffer cmdBuffer
    ) const override;
    bool connectLink(
        const LinkSlot& slot,
        Store& store
    ) override;

    void generateCreate(const Store& store, std::ostream& out) const override;
    void generateRecordCommands(const Store& store, std::ostream& out) const override;
    void generateDestroy(const Store& store, std::ostream& out) const override;

private:
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> globalDescriptorSets{};
    std::vector<std::vector<VkDescriptorSet>> perObjectDescriptorSets{};
};

class RenderPass : public Node, public GenerateNode {
public:
    // CREATE, RECORD
    std::vector<StoreHandle> attachments;

    // RECORD
    VkRect2D renderArea{};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    std::vector<VkClearValue> clearValues{};

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;
    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    virtual void generateCreate(
        const Store& store,
        std::ostream& out
    ) const override;
    virtual void generateDestroy(
        const Store& store,
        std::ostream& out
    ) const override;

    bool rendersToSwapchain(const Store& store) const;
};

class Present : public Node {
public:
    // CREATE
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };

    bool create(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    void destroy(
        const Store& store,
        VkDevice device,
        VmaAllocator allocator
    ) override;

    bool connectLink(
        const LinkSlot& slot,
        Store& store
    ) override;

    VkDescriptorSet getLiveViewImage() const;

    /// Returns true if connectLink was successfully called and image is
    /// valid
    bool isReady() const {
        return image.isValid();
    }

private:
    StoreHandle image{};
    VkDescriptorSet outDS{VK_NULL_HANDLE};
    VkSampler outSampler{VK_NULL_HANDLE};
};

enum class StoreState { Empty, Created, Linked };

struct Store {
    std::array<Array, 1000> arrays;
    std::array<VertexData, 1000> vertexDatas;
    std::array<UniformBuffer, 1000> uniformBuffers;
    std::array<Camera, 10> cameras;
    std::array<Light, 10> lights;
    std::array<DescriptorPool, 5> descriptorPools;
    std::array<DescriptorSet, 1000> descriptorSets;
    std::array<RenderPass, 50> renderPasses;
    std::array<Pipeline, 50> pipelines;
    std::array<Shader, 100> shaders;
    std::array<Attachment, 100> attachments;
    std::array<Image, 1000> images;
    std::array<Present, 1> presents;

    void reset();
    void destroy(VkDevice device, VmaAllocator allocator);
    StoreHandle defaultDescriptorPool();
    StoreHandle newArray();
    StoreHandle newVertexData();
    StoreHandle newUniformBuffer();
    StoreHandle newCamera();
    StoreHandle newLight();
    StoreHandle newDescriptorPool();
    StoreHandle newDescriptorSet();
    StoreHandle newRenderPass();
    StoreHandle newPipeline();
    StoreHandle newShader();
    StoreHandle newAttachment();
    StoreHandle newImage();
    StoreHandle newPresent();

    uint32_t getShaderCount() const;
    StoreState getState() const;

    void link();
    std::vector<Node*> getNodes();
    std::vector<const GenerateNode*> getGenerateNodes() const;
    Node* getNode(StoreHandle handle);

    /// Get name of a primitive by handle
    std::string getName(StoreHandle handle) const;

    /// Validate that all primitive names are unique. Asserts on
    /// duplicates.
    void validateUniqueNames() const;

    void updateSwapchainExtent(const VkExtent3D& extent);
    VkDescriptorSet getLiveViewImage();

    /// Returns true if there is a Present primitive with a valid
    /// connected image
    bool hasValidPresent() const;

private:
    uint32_t arrayCount{0};
    uint32_t vertexDataCount{0};
    uint32_t uniformBufferCount{0};
    uint32_t cameraCount{0};
    uint32_t lightCount{0};
    uint32_t descriptorPoolCount{0};
    uint32_t descriptorSetCount{0};
    uint32_t renderPassCount{0};
    uint32_t attachmentCount{0};
    uint32_t imageCount{0};
    uint32_t presentCount{0};
    uint32_t pipelineCount{0};
    uint32_t shaderCount{0};

    StoreState state{StoreState::Empty};
}; // namespace primitives

} // namespace primitives