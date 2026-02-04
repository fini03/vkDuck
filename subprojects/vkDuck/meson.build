# vkDuck - shared library for generated Vulkan projects
# Used as a subproject by generated projects

project('vkDuck', 'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=debugoptimized'
  ]
)

# Dependencies
vulkan_dep = dependency('vulkan', required: true)
sdl3_dep = dependency('sdl3', required: true)
glm_dep = dependency('glm', required: true)
vma_dep = dependency('vulkan-memory-allocator', include_type: 'system')

# Library sources
vkDuck_sources = files(
  'src/vulkan_base.cpp',
  'src/library.cpp',
  'src/camera_controller.cpp',
  'src/model_loader.cpp',
  'src/image_loader.cpp'
)

# Include directories
vkDuck_inc = include_directories('include', 'external')

# Platform-specific compiler arguments
cpp_args = []
if host_machine.system() == 'darwin'
  cpp_args += ['-DVK_USE_PLATFORM_MACOS_MVK']
elif host_machine.system() == 'linux'
  cpp_args += ['-DVK_USE_PLATFORM_XCB_KHR']
elif host_machine.system() == 'windows'
  cpp_args += ['-DVK_USE_PLATFORM_WIN32_KHR']
endif

# Build as static library
vkDuck_lib = static_library('vkDuck',
  vkDuck_sources,
  include_directories: vkDuck_inc,
  dependencies: [vulkan_dep, sdl3_dep, glm_dep, vma_dep],
  cpp_args: cpp_args,
  install: true
)

# Declare dependency for consumers
vkDuck_dep = declare_dependency(
  include_directories: vkDuck_inc,
  link_with: vkDuck_lib,
  dependencies: [vulkan_dep, sdl3_dep, glm_dep, vma_dep]
)
