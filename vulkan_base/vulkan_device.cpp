#include "vulkan/vulkan_core.h"
#include "vulkan_base.h"

VkBool32 VKAPI_CALL debugReportCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "ERROR: " << callbackData->pMessage << std::endl;
    } else {
        std::cout << "WARN: " << callbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerEXT registerDebugCallback(VkInstance instance) {
    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebutUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT"
        );

    VkDebugUtilsMessengerCreateInfoEXT callbackInfo = {
        .sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
        .pfnUserCallback = debugReportCallback
    };

    VkDebugUtilsMessengerEXT callback = 0;
    pfnCreateDebutUtilsMessengerEXT(
        instance, &callbackInfo, 0, &callback
    );

    return callback;
}

bool initVulkanInstance(
    VulkanContext* context,
    uint32_t instanceExtensionCount,
    const std::vector<const char*> instanceExtensions,
    bool enableValidationLayers
) {
    uint32_t layerPropertyCount = 0;
    vkEnumerateInstanceLayerProperties(&layerPropertyCount, 0);
    std::vector<VkLayerProperties> availableLayers(layerPropertyCount);
    vkEnumerateInstanceLayerProperties(
        &layerPropertyCount, availableLayers.data()
    );

    const std::vector<const char*> enabledLayers = {
        "VK_LAYER_KHRONOS_validation",
    };

    std::vector<VkValidationFeatureEnableEXT> enableValidationFeatures =
        {
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };
    VkValidationFeaturesEXT validationFeatures = {
        VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT
    };
    validationFeatures.enabledValidationFeatureCount =
        static_cast<uint32_t>(enableValidationFeatures.size());
    validationFeatures.pEnabledValidationFeatures =
        enableValidationFeatures.data();

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        // For Spir-V we need a version higher than 1_0
        // Newest vesion is 1_4 but sadly macbooki can only
        // handle 1_2 :(
        .apiVersion = VK_API_VERSION_1_4
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef __APPLE__
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
        .pNext = &validationFeatures,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = instanceExtensionCount,
        .ppEnabledExtensionNames = instanceExtensions.data()
    };

    if (enableValidationLayers) {
        createInfo.enabledLayerCount =
            static_cast<uint32_t>(enabledLayers.size());
        createInfo.ppEnabledLayerNames = enabledLayers.data();
    }

    if (vkCreateInstance(&createInfo, 0, &context->instance) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create vulkan instance");
    }

    if (enableValidationLayers) {
        context->debugCallback =
            registerDebugCallback(context->instance);
    }

    return true;
}

bool selectPhysicalDevice(VulkanContext* context) {
    uint32_t numDevices = 0;
    vkEnumeratePhysicalDevices(context->instance, &numDevices, 0);
    if (!numDevices) {
        throw std::runtime_error(
            "Failed to find GPUs with Vulkan support!"
        );
    }

    std::vector<VkPhysicalDevice> physicalDevices(numDevices);
    vkEnumeratePhysicalDevices(
        context->instance, &numDevices, physicalDevices.data()
    );

    // TODO: Is it okay to always pick the first one?
    // Picking first device should be fine for now
    // hopefully this doesn't bite us
    context->physicalDevice = physicalDevices[0];
    vkGetPhysicalDeviceProperties(
        context->physicalDevice, &context->physicalDeviceProperties
    );

    return true;
}

bool createLogicalDevice(
    VulkanContext* context,
    uint32_t deviceExtensionCount,
    const std::vector<const char*> deviceExtensions
) {
    // Queues
    uint32_t numQueueFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        context->physicalDevice, &numQueueFamilies, 0
    );

    std::vector<VkQueueFamilyProperties> queueFamilies(
        numQueueFamilies
    );
    vkGetPhysicalDeviceQueueFamilyProperties(
        context->physicalDevice, &numQueueFamilies, queueFamilies.data()
    );

    uint32_t graphicsQueueIndex = 0;
    for (uint32_t i = 0; i < numQueueFamilies; ++i) {
        VkQueueFamilyProperties queueFamily = queueFamilies[i];
        if (queueFamily.queueCount > 0) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueIndex = i;
                break;
            }
        }
    }

    float priorities[] = {1.0f};
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueIndex,
        .queueCount = 1,
        .pQueuePriorities = priorities
    };

    VkPhysicalDeviceFeatures enabledFeatures = {
        .samplerAnisotropy = VK_TRUE
    };

    // Enable Vulkan 1.1 features (shaderDrawParameters for gl_DrawID support)
    VkPhysicalDeviceVulkan11Features vulkan11Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = VK_TRUE
    };

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan11Features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = deviceExtensionCount,
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = &enabledFeatures
    };

    if (vkCreateDevice(
            context->physicalDevice, &createInfo, 0, &context->device
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    // Acquire queues
    context->graphicsQueue.familyIndex = graphicsQueueIndex;
    vkGetDeviceQueue(
        context->device, graphicsQueueIndex, 0,
        &context->graphicsQueue.queue
    );
    return true;
}

VulkanContext* VulkanContext::initVulkan(
    uint32_t instanceExtensionCount,
    const std::vector<const char*> instanceExtensions,
    uint32_t deviceExtensionCount,
    const std::vector<const char*> deviceExtensions,
    bool enableValidationLayers
) {
    VulkanContext* context = new VulkanContext;

    if (!initVulkanInstance(
            context, instanceExtensionCount, instanceExtensions,
            enableValidationLayers
        )) {
        return 0;
    }

    if (!selectPhysicalDevice(context)) {
        return 0;
    }

    if (!createLogicalDevice(
            context, deviceExtensionCount, deviceExtensions
        )) {
        return 0;
    }

    return context;
}

void VulkanContext::exitVulkan() {
    vkDeviceWaitIdle(this->device);
    vkDestroyDevice(this->device, 0);

    if (this->debugCallback != nullptr) {
        PFN_vkDestroyDebugUtilsMessengerEXT
            pfnDestroyDebugUtilsMessengerEXT;
        pfnDestroyDebugUtilsMessengerEXT =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                this->instance, "vkDestroyDebugUtilsMessengerEXT"
            );
        pfnDestroyDebugUtilsMessengerEXT(
            this->instance, this->debugCallback, 0
        );
        this->debugCallback = 0;
    }

    vkDestroyInstance(this->instance, 0);
}
