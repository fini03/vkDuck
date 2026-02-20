cmake_minimum_required(VERSION 3.22)
project(vkDuck VERSION 0.1 LANGUAGES C CXX)

# ── C++23 standard ────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 16MB stack size (matches meson build)
if(MSVC)
    add_link_options(/STACK:16777216)
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────

# Vulkan (system)
find_package(Vulkan REQUIRED)

# SDL3 (subproject)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/SDL3-3.2.10 EXCLUDE_FROM_ALL)

# GLM (subproject)
add_subdirectory(subprojects/glm-1.0.1 EXCLUDE_FROM_ALL)
if(NOT TARGET glm::glm)
    if(TARGET glm)
        add_library(glm::glm ALIAS glm)
    endif()
endif()

# Vulkan Memory Allocator (subproject, header-only)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/vulkan-memory-allocator EXCLUDE_FROM_ALL)

# efsw - file system watcher (subproject)
set(EFSW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/efsw EXCLUDE_FROM_ALL)

# nlohmann JSON (header-only, single-include distribution)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/nlohmann_json-3.12.0/single_include
)

# Dear ImGui (static library with SDL3 + Vulkan backends)
add_library(imgui STATIC
    subprojects/imgui/imgui.cpp
    subprojects/imgui/imgui_draw.cpp
    subprojects/imgui/imgui_widgets.cpp
    subprojects/imgui/imgui_tables.cpp
    subprojects/imgui/backends/imgui_impl_sdl3.cpp
    subprojects/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/imgui/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)

# ImGui Node Editor (static library)
add_library(imgui_node_editor STATIC
    subprojects/imgui-node-editor/imgui_node_editor.cpp
    subprojects/imgui-node-editor/imgui_node_editor_api.cpp
    subprojects/imgui-node-editor/crude_json.cpp
    subprojects/imgui-node-editor/imgui_canvas.cpp
)
target_include_directories(imgui_node_editor SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/imgui-node-editor
)
target_link_libraries(imgui_node_editor PUBLIC imgui)

# Shader Slang (pre-built binaries)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SLANG_PLATFORM_DIR shader-slang-windows-x86_64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(SLANG_PLATFORM_DIR shader-slang-linux-x86_64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(SLANG_PLATFORM_DIR shader-slang-macos-aarch64)
else()
    message(FATAL_ERROR
        "Unsupported platform for shader-slang.\n"
        "Supported: Windows x64, Linux x64, macOS ARM64.\n"
        "Download the correct release from https://github.com/shader-slang/slang/releases "
        "and extract it to subprojects/shader-slang-<platform>/")
endif()

set(SLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/${SLANG_PLATFORM_DIR})
if(NOT EXISTS "${SLANG_DIR}")
    message(FATAL_ERROR
        "Shader-slang binaries not found at: ${SLANG_DIR}\n"
        "Download from https://github.com/shader-slang/slang/releases and extract there,\n"
        "or run 'meson subprojects download' to fetch all dependencies first.")
endif()

add_library(shader_slang INTERFACE)
target_include_directories(shader_slang INTERFACE ${SLANG_DIR}/include)
find_library(SLANG_LIBRARY slang PATHS ${SLANG_DIR}/lib NO_DEFAULT_PATH REQUIRED)
target_link_libraries(shader_slang INTERFACE ${SLANG_LIBRARY})

# vkDuck library (subproject)
add_subdirectory(subprojects/vkDuck)

# ── Main executable ───────────────────────────────────────────────────────────

add_executable(vkDuckEditor
    main.cpp

    # Vulkan base
    vulkan_base/vulkan_device.cpp

    # Editor core
    vulkan_editor/editor.cpp

    # Graph - node system
    vulkan_editor/graph/node.cpp
    vulkan_editor/graph/node_graph.cpp
    vulkan_editor/graph/link.cpp
    vulkan_editor/graph/pipeline_node.cpp
    vulkan_editor/graph/model_node.cpp
    vulkan_editor/graph/present_node.cpp
    vulkan_editor/graph/light_node.cpp
    vulkan_editor/graph/camera_node.cpp
    vulkan_editor/graph/fixed_camera_node.cpp
    vulkan_editor/graph/fps_camera_node.cpp
    vulkan_editor/graph/default_renderer.cpp

    # Shader management
    vulkan_editor/shader/shader_manager.cpp
    vulkan_editor/shader/shader_reflection.cpp
    vulkan_editor/shader/shader_watcher.cpp

    # I/O and serialization
    vulkan_editor/io/file_generator.cpp
    vulkan_editor/io/primitive_generator.cpp
    vulkan_editor/io/graph_serializer.cpp
    vulkan_editor/io/directory_watcher.cpp
    vulkan_editor/io/model_watcher.cpp

    # GPU resources
    vulkan_editor/gpu/primitives.cpp
    vulkan_editor/gpu/batched_stager.cpp
    vulkan_editor/gpu/staging_buffer_pool.cpp

    # Utilities
    vulkan_editor/util/logger.cpp

    # UI
    vulkan_editor/ui/pipeline_editor_ui.cpp
    vulkan_editor/ui/pipeline_settings_ui.cpp
    vulkan_editor/ui/model_settings_ui.cpp
    vulkan_editor/ui/camera_editor_ui.cpp
    vulkan_editor/ui/light_editor_ui.cpp
    vulkan_editor/ui/attachment_editor_ui.cpp
    vulkan_editor/ui/live_view.cpp
    vulkan_editor/ui/debug_console_ui.cpp
    vulkan_editor/ui/user_messages_ui.cpp

    # External utilities
    external/tinyfiledialogs.cpp
    external/utilities/builders.cpp
    external/utilities/drawing.cpp
    external/utilities/widgets.cpp
    external/SimpleFileDialog.cpp
)

target_compile_definitions(vkDuckEditor PRIVATE VKDUCK_NO_VMA_IMPLEMENTATION)

target_include_directories(vkDuckEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_link_libraries(vkDuckEditor PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
    imgui
    imgui_node_editor
    nlohmann_json
    shader_slang
    vkDuck
    efsw
)

# VMA - handle different target names across VMA versions
if(TARGET GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(vkDuckEditor PRIVATE GPUOpen::VulkanMemoryAllocator)
elseif(TARGET VulkanMemoryAllocator)
    target_link_libraries(vkDuckEditor PRIVATE VulkanMemoryAllocator)
endif()

# macOS: link Apple frameworks required by efsw
if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(CORESERVICES_LIBRARY CoreServices REQUIRED)
    target_link_libraries(vkDuckEditor PRIVATE ${COREFOUNDATION_LIBRARY} ${CORESERVICES_LIBRARY})
endif()

# Windows: copy shader-slang runtime DLLs next to executable
if(WIN32 AND EXISTS "${SLANG_DIR}/bin")
    file(GLOB SLANG_DLLS "${SLANG_DIR}/bin/*.dll")
    foreach(DLL ${SLANG_DLLS})
        add_custom_command(TARGET vkDuckEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:vkDuckEditor>"
        )
    endforeach()
endif()
