#include <SDL3/SDL_video.h>
#define IMGUI_IMPL_VULKAN
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#define VMA_IMPLEMENTATION
#define VMA_DEBUG_LOG_FORMAT(format, ...) \
    do {                                  \
        printf((format), __VA_ARGS__);    \
        printf("\n");                     \
    } while (false)
#include <vk_mem_alloc.h>

#include <vkDuck/library.h>
#include "vulkan_base/vulkan_base.h"
#include "vulkan_editor/editor.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

// Data
VkAllocationCallbacks* g_Allocator = nullptr;
VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

SDL_Window* window;
ImGui_ImplVulkanH_Window* wd;
ImGui_ImplVulkanH_Window g_MainWindowData;
uint32_t g_MinImageCount = 2;
bool g_SwapChainRebuild = false;

ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
bool done = false;

VulkanContext* context = nullptr;

#ifdef __APPLE__
const std::vector<const char*> enabledDeviceExtensions = {
    "VK_KHR_swapchain", "VK_KHR_portability_subset",
    "VK_KHR_shader_draw_parameters"
};
#else
const std::vector<const char*> enabledDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters"
};
#endif

const bool enableValidationLayers = true;

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

std::vector<const char*> getRequiredExtensions() {
    uint32_t instanceExtensionCount = 0;
    const char* const* instanceExtensions =
        SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount);
    std::vector<const char*> enabledInstanceExtensions(
        instanceExtensions, instanceExtensions + instanceExtensionCount
    );

    if (enableValidationLayers) {
        enabledInstanceExtensions.push_back(
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        );
    }

#ifdef __APPLE__
    enabledInstanceExtensions.push_back("VK_MVK_macos_surface");
    enabledInstanceExtensions.push_back(
        "VK_KHR_get_physical_device_properties2"
    );
    enabledInstanceExtensions.push_back(
        "VK_KHR_portability_enumeration"
    );
#endif

    return enabledInstanceExtensions;
}

void createDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         1 /* For live view */},
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 0
    };

    for (VkDescriptorPoolSize& pool_size : pool_sizes) {
        pool_info.maxSets += pool_size.descriptorCount;
    }

    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(
        context->device, &pool_info, g_Allocator, &g_DescriptorPool
    );
}

void initVulkan() {
    std::vector<const char*> enabledInstanceExtensions =
        getRequiredExtensions();
    context = VulkanContext::initVulkan(
        static_cast<uint32_t>(enabledInstanceExtensions.size()),
        enabledInstanceExtensions,
        static_cast<uint32_t>(enabledDeviceExtensions.size()),
        enabledDeviceExtensions, enableValidationLayers
    );
    // Create Descriptor Pool
    createDescriptorPool();
}

void initWindow() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    window =
        SDL_CreateWindow("vkDuck", 1920, 1080, window_flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError()
                  << std::endl;
    }
}

void createVulkanWindow(
    ImGui_ImplVulkanH_Window* wd,
    VkSurfaceKHR surface,
    int width,
    int height
) {
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(
        context->physicalDevice, context->graphicsQueue.familyIndex,
        wd->Surface, &presentSupport
    );
    if (!presentSupport) {
        std::cerr << "Error: No WSI support on physical device 0"
                  << std::endl;
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
    };
    const VkColorSpaceKHR requestSurfaceColorSpace =
        VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        context->physicalDevice, wd->Surface, requestSurfaceImageFormat,
        (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat),
        requestSurfaceColorSpace
    );

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = {
        VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_FIFO_KHR
    };
#else
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif

    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        context->physicalDevice, wd->Surface, &present_modes[0],
        IM_ARRAYSIZE(present_modes)
    );

    // Create SwapChain, RenderPass, Framebuffer, etc.
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        context->instance, context->physicalDevice, context->device, wd,
        context->graphicsQueue.familyIndex, g_Allocator, width, height,
        g_MinImageCount, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    );
}

void cleanup() {
    vkDeviceWaitIdle(context->device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    ImGui_ImplVulkanH_DestroyWindow(
        context->instance, context->device, &g_MainWindowData,
        g_Allocator
    );
    vkDestroyDescriptorPool(
        context->device, g_DescriptorPool, g_Allocator
    );
    context->exitVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();
}

void render(
    ImGui_ImplVulkanH_Window* wd,
    ImDrawData* draw_data
) {
    VkSemaphore image_acquired_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(
        context->device, wd->Swapchain, UINT64_MAX,
        image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex
    );
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(
            context->device, 1, &fd->Fence, VK_TRUE, UINT64_MAX
        ); // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(context->device, 1, &fd->Fence);
        check_vk_result(err);
    }

    {
        err = vkResetCommandPool(context->device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }

    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(
            fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE
        );
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(
            context->graphicsQueue.queue, 1, &info, fd->Fence
        );
        check_vk_result(err);
    }
}

void present(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err =
        vkQueuePresentKHR(context->graphicsQueue.queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);
    wd->SemaphoreIndex =
        (wd->SemaphoreIndex + 1) %
        wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void initSurface() {
    // Create Window Surface
    VkSurfaceKHR surface;

    if (SDL_Vulkan_CreateSurface(
            window, context->instance, g_Allocator, &surface
        ) == 0) {
        printf("Failed to create Vulkan surface.\n");
    }

    // Create Framebuffers
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    wd = &g_MainWindowData;
    createVulkanWindow(wd, surface, width, height);
    SDL_SetWindowPosition(
        window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED
    );
    SDL_ShowWindow(window);
}

void setupCustomTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Modern dark theme base colors
    ImVec4 bgDark = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);       // Blue accent
    ImVec4 accentHover = ImVec4(0.36f, 0.69f, 1.00f, 1.00f);
    ImVec4 textPrimary = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    ImVec4 textSecondary = ImVec4(0.60f, 0.60f, 0.65f, 1.00f);
    ImVec4 border = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);

    // Window
    colors[ImGuiCol_WindowBg] = bgDark;
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);

    // Borders
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Text
    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textSecondary;

    // Menu Bar - Colored accent bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.35f, 0.55f, 1.00f);

    // Headers (collapsing headers, tree nodes)
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive] = accent;

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonActive] = accent;

    // Frame BG (input fields, checkboxes, etc.)
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);

    // Title Bar
    colors[ImGuiCol_TitleBg] = bgDark;
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.30f, 0.48f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = bgDark;

    // Tabs - Modern styled tabs
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.50f, 0.75f, 0.90f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.42f, 0.65f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;

    // Checkmarks and sliders
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = accent;

    // Separator
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accentHover;
    colors[ImGuiCol_SeparatorActive] = accent;

    // Resize grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = accent;

    // Plot
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.80f, 0.00f, 1.00f);

    // Table
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Text selection
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

    // Drag/drop
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

    // Nav highlight
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    // Modal window dim
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

    // Style adjustments for modern look
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;

    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
}

void initImgui() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup custom modern theme
    ImGui::StyleColorsDark();  // Start with dark as base
    setupCustomTheme();        // Apply our customizations

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass
    // in your value of VkApplicationInfo::apiVersion, otherwise will
    // default to header version.
    init_info.Instance = context->instance;
    init_info.PhysicalDevice = context->physicalDevice;
    init_info.Device = context->device;
    init_info.QueueFamily = context->graphicsQueue.familyIndex;
    init_info.Queue = context->graphicsQueue.queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = g_Allocator;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);
}

void runEditor(Editor* editor) {
    int width, height;
    SDL_GetWindowSize(
        window, &width, &height
    ); // Get real-time window size

    ImGui::SetNextWindowSize(
        ImVec2((float)width, (float)height), ImGuiCond_Always
    );                                     // Always resize
    ImGui::SetNextWindowPos(ImVec2(0, 0)); // Lock to top-left corner

    ImGui::Begin(
        "Graphical Vulkan Editor", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar
    );

    editor->start();

    ImGui::Dummy(ImVec2(0, 0));

    ImGui::End();
}

void handleMessage() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
            done = true;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window))
            done = true;
    }

    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be
    // your SDL_AppIterate() function]
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        return;
    }

    // Resize swap chain?
    int fb_width, fb_height;
    SDL_GetWindowSize(window, &fb_width, &fb_height);
    if (fb_width > 0 && fb_height > 0 &&
        (g_SwapChainRebuild || g_MainWindowData.Width != fb_width ||
         g_MainWindowData.Height != fb_height)) {
        ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            context->instance, context->physicalDevice, context->device,
            &g_MainWindowData, context->graphicsQueue.familyIndex,
            g_Allocator, fb_width, fb_height, g_MinImageCount,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        );
        g_MainWindowData.FrameIndex = 0;
        g_SwapChainRebuild = false;
    }
}

void renderFrame() {
    // Rendering
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        (draw_data->DisplaySize.x <= 0.0f ||
         draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized) {
        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        render(wd, draw_data);
        present(wd);
    }
}

// Main code
int main() {
    initWindow();
    initVulkan();
    initSurface();
    initImgui();

    VmaAllocator vma{VK_NULL_HANDLE};

    {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.flags =
            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        allocatorCreateInfo.physicalDevice = context->physicalDevice;
        allocatorCreateInfo.device = context->device;
        allocatorCreateInfo.instance = context->instance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        vkchk(vmaCreateAllocator(&allocatorCreateInfo, &vma));
    }

    Editor* editor = new Editor{
        context->device, vma,
        context->graphicsQueue.familyIndex, context->graphicsQueue.queue
    };

    while (!done) {
        handleMessage();

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        runEditor(editor);
        renderFrame();
    }

    delete editor;
    editor = nullptr;
    vmaDestroyAllocator(vma);

    cleanup();

    return 0;
}