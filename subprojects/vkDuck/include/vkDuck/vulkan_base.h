// vim:foldmethod=marker
#pragma once

// Includes {{{
#include "vulkan/vulkan.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "vk_mem_alloc.h"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>
#include <functional>
// }}}

// Constants {{{
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif
// }}}

// Structs {{{
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct SwapChain {
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    VkFormat imageFormat;
    VkExtent2D extent;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
};

struct DepthImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct SyncObjects {
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;  // Track which fence is being used for each swapchain image
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct UniformBuffers {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<void*> mapped;
};

struct Material {
    Texture texture;
};

struct Geometry {
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
    int materialIndex = 0;
};

struct Object {
    std::vector<Material> materials;
    std::vector<Geometry> geometries;
    std::vector<VkDescriptorSet> descriptorSets;
    VkBuffer objectUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation objectUniformBufferAllocation = VK_NULL_HANDLE;
    void* objectUniformBufferMapped = nullptr;
};
// }}}

// VulkanBase class {{{
class VulkanBase {
public:
    VulkanBase() = default;
    virtual ~VulkanBase() = default;

    void initWindow(const char* title = "Vulkan Application");
    void initVulkan();
    void showWindow();
    void cleanup();

    bool shouldClose() const { return m_quit; }
    void pollEvents();

    // Getters
    SDL_Window* getWindow() const { return m_window; }
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VmaAllocator getAllocator() const { return m_allocator; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    const SwapChain& getSwapChain() const { return m_swapChain; }
    const DepthImage& getDepthImage() const { return m_depthImage; }
    const SyncObjects& getSyncObjects() const { return m_syncObjects; }
    const QueueFamilyIndices& getQueueFamilies() const { return m_queueFamilies; }
    uint32_t getCurrentFrame() const { return m_currentFrame; }
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    bool isMinimized() const { return m_isMinimized; }

    // Frame management
    uint32_t beginFrame();
    void endFrame(uint32_t imageIndex);
    void waitIdle();

    // Swapchain recreation
    void recreateSwapChain();

protected:
    // Overridable callbacks
    virtual void onWindowResized() {}
    virtual void onKeyEvent(const SDL_KeyboardEvent& event) {}
    virtual void onMouseMotion(float x, float y) {}
    virtual void onMouseButton(const SDL_MouseButtonEvent& event, Uint32 eventType) {}
    virtual void onMouseScroll(float delta) {}

    // Window
    SDL_Window* m_window = nullptr;
    bool m_quit = false;
    bool m_isMinimized = false;
    bool m_framebufferResized = false;

    // Vulkan core
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // Queues
    QueueFamilyIndices m_queueFamilies;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    // Swapchain
    SwapChain m_swapChain;
    DepthImage m_depthImage;

    // Render pass
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    // Command pool and buffers
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Descriptor pool
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    // Synchronization
    SyncObjects m_syncObjects;
    uint32_t m_currentFrame = 0;

private:
    // Initialization functions
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createDescriptorPool();
    void createSyncObjects();

    // Cleanup helpers
    void cleanupSwapChain();

    // Helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    // Validation layers
    static const std::vector<const char*> s_validationLayers;
    static const std::vector<const char*> s_deviceExtensions;
};
// }}}
