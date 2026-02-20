cmake_minimum_required(VERSION 3.22)
project(vkDuck VERSION 1.0.0 LANGUAGES CXX)

# ── C++23 standard ────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_link_options(/STACK:16777216)
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────
# When used as a subdirectory of the editor, targets are already available.
# When used standalone (e.g. in generated projects), find_package is used.

if(NOT TARGET Vulkan::Vulkan)
    find_package(Vulkan REQUIRED)
endif()

if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 REQUIRED)
endif()

if(NOT TARGET glm::glm)
    find_package(glm REQUIRED)
endif()

# VMA - check common target names
if(NOT TARGET GPUOpen::VulkanMemoryAllocator AND NOT TARGET VulkanMemoryAllocator)
    find_package(VulkanMemoryAllocator REQUIRED CONFIG)
endif()

# ── Library ───────────────────────────────────────────────────────────────────

add_library(vkDuck STATIC
    src/vulkan_base.cpp
    src/library.cpp
    src/camera_controller.cpp
    src/model_loader.cpp
    src/image_loader.cpp
)

target_include_directories(vkDuck
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Platform-specific Vulkan surface defines
if(WIN32)
    target_compile_definitions(vkDuck PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(vkDuck PRIVATE VK_USE_PLATFORM_MACOS_MVK)
elseif(UNIX)
    target_compile_definitions(vkDuck PRIVATE VK_USE_PLATFORM_XCB_KHR)
endif()

target_link_libraries(vkDuck PUBLIC
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
)

if(TARGET GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(vkDuck PUBLIC GPUOpen::VulkanMemoryAllocator)
elseif(TARGET VulkanMemoryAllocator)
    target_link_libraries(vkDuck PUBLIC VulkanMemoryAllocator)
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS vkDuck
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/vkDuck
    DESTINATION include
)
