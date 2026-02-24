cmake_minimum_required(VERSION 3.22)
project(vkDuck VERSION 0.1 LANGUAGES C CXX)

# Avoid DOWNLOAD_EXTRACT_TIMESTAMP warning on CMake 3.24+
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Allow FetchContent_Populate for libs without CMakeLists.txt (CMake 4.x compat)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# ── C++23 standard ────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 16MB stack size (matches meson build)
if(MSVC)
    add_link_options(/STACK:16777216)
endif()

include(FetchContent)

# ── Dependencies (all fetched automatically – no meson required) ─────────────

# Vulkan (system)
find_package(Vulkan REQUIRED)

# SDL3
FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.2.10/SDL3-3.2.10.tar.gz
    URL_HASH SHA256=f87be7b4dec66db4098e9c167b2aa34e2ca10aeb5443bdde95ae03185ed513e0
)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# GLM
FetchContent_Declare(glm
    URL      https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
    URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
)
FetchContent_MakeAvailable(glm)
if(NOT TARGET glm::glm)
    if(TARGET glm)
        add_library(glm::glm ALIAS glm)
    endif()
endif()

# Vulkan Memory Allocator
FetchContent_Declare(VulkanMemoryAllocator
    URL      https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/e722e57c891a8fbe3cc73ca56c19dd76be242759.tar.gz
)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# efsw – file system watcher
FetchContent_Declare(efsw
    GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
    GIT_TAG        master
    GIT_SHALLOW    ON
)
set(EFSW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(efsw)

# nlohmann JSON (header-only, single-include distribution)
FetchContent_Declare(nlohmann_json_fetch
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip
    URL_HASH SHA256=b8cb0ef2dd7f57f18933997c9934bb1fa962594f701cd5a8d3c2c80541559372
)
FetchContent_GetProperties(nlohmann_json_fetch)
if(NOT nlohmann_json_fetch_POPULATED)
    FetchContent_Populate(nlohmann_json_fetch)
endif()
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${nlohmann_json_fetch_SOURCE_DIR}/single_include
)

# Dear ImGui (static library with SDL3 + Vulkan backends)
FetchContent_Declare(imgui_fetch
    URL https://github.com/ocornut/imgui/archive/396b33d0d011290f11915b6a2a2bf7737fb42dd7.tar.gz
)
FetchContent_GetProperties(imgui_fetch)
if(NOT imgui_fetch_POPULATED)
    FetchContent_Populate(imgui_fetch)
endif()
add_library(imgui STATIC
    ${imgui_fetch_SOURCE_DIR}/imgui.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_fetch_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_fetch_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_fetch_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_fetch_SOURCE_DIR}
    ${imgui_fetch_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)

# ImGui Node Editor (static library)
FetchContent_Declare(imgui_node_editor_fetch
    URL https://github.com/thedmd/imgui-node-editor/archive/e78e447900909a051817a760efe13fe83e6e1afc.tar.gz
)
FetchContent_GetProperties(imgui_node_editor_fetch)
if(NOT imgui_node_editor_fetch_POPULATED)
    FetchContent_Populate(imgui_node_editor_fetch)
endif()
add_library(imgui_node_editor STATIC
    ${imgui_node_editor_fetch_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_fetch_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imgui_node_editor_fetch_SOURCE_DIR}/crude_json.cpp
    ${imgui_node_editor_fetch_SOURCE_DIR}/imgui_canvas.cpp
)
target_include_directories(imgui_node_editor SYSTEM PUBLIC
    ${imgui_node_editor_fetch_SOURCE_DIR}
)
target_link_libraries(imgui_node_editor PUBLIC imgui)

# Shader Slang (pre-built binaries, downloaded per platform)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SLANG_ARCHIVE_URL  "https://github.com/shader-slang/slang/releases/download/v2025.24.1/slang-2025.24.1-windows-x86_64.zip")
    set(SLANG_ARCHIVE_HASH "fdf3709f6be625ee42ba84fae9cb632ebcec279be505923fba61a0249610b854")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(SLANG_ARCHIVE_URL  "https://github.com/shader-slang/slang/releases/download/v2025.24.1/slang-2025.24.1-linux-x86_64.tar.gz")
    set(SLANG_ARCHIVE_HASH "affaa1b70ccd1f02baae7c30214a4ca2372bc4eb8cc39cbc470dc32a2873e0dd")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(SLANG_ARCHIVE_URL  "https://github.com/shader-slang/slang/releases/download/v2025.24.1/slang-2025.24.1-macos-aarch64.tar.gz")
    set(SLANG_ARCHIVE_HASH "91140d664ef3c98d467a739f492edce774853f7145e6e631120171074296bdbe")
else()
    message(FATAL_ERROR
        "Unsupported platform for shader-slang.\n"
        "Supported: Windows x64, Linux x64, macOS ARM64.\n"
        "Download the correct release from https://github.com/shader-slang/slang/releases")
endif()

FetchContent_Declare(shader_slang_fetch
    URL      ${SLANG_ARCHIVE_URL}
    URL_HASH SHA256=${SLANG_ARCHIVE_HASH}
)
FetchContent_GetProperties(shader_slang_fetch)
if(NOT shader_slang_fetch_POPULATED)
    FetchContent_Populate(shader_slang_fetch)
endif()
set(SLANG_DIR ${shader_slang_fetch_SOURCE_DIR})

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
