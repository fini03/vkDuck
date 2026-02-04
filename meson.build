project(
  'vkDuck', ['cpp'],
  version: '0.1',
  default_options: [
    'cpp_std=c++23',
    'buildtype=debugoptimized',
    'debug=true'
  ]
)

# vkDuck shared library (used by both editor and generated projects)
vkDuck_proj = subproject('vkDuck')
vkDuck_dep = vkDuck_proj.get_variable('vkDuck_dep')

# Dependencies
sdl3 = dependency('sdl3')
vulkan = dependency('vulkan')
glm = dependency('glm')
imgui = dependency('imgui')
imnodes = dependency('imgui-node-editor', include_type: 'system')
shader_slang = dependency('shader-slang')
vma = dependency('vulkan-memory-allocator', include_type: 'system')
efsw = dependency('efsw')

vulkan_files = files(
	'vulkan_base/vulkan_device.cpp',
  # helper.cpp removed - now using vkDuck/library.h
)

editor_files = files(
  # Main editor
  'vulkan_editor/editor.cpp',

  # Graph - node system
  'vulkan_editor/graph/node.cpp',
  'vulkan_editor/graph/node_graph.cpp',
  'vulkan_editor/graph/link.cpp',
  'vulkan_editor/graph/pipeline_node.cpp',
  'vulkan_editor/graph/model_node.cpp',
  'vulkan_editor/graph/present_node.cpp',
  'vulkan_editor/graph/light_node.cpp',
  'vulkan_editor/graph/camera_node.cpp',
  'vulkan_editor/graph/fixed_camera_node.cpp',
  'vulkan_editor/graph/fps_camera_node.cpp',
  'vulkan_editor/graph/default_renderer.cpp',

  # Shader management
  'vulkan_editor/shader/shader_manager.cpp',
  'vulkan_editor/shader/shader_reflection.cpp',
  'vulkan_editor/shader/shader_watcher.cpp',

  # I/O and serialization
  'vulkan_editor/io/file_generator.cpp',
  'vulkan_editor/io/primitive_generator.cpp',
  'vulkan_editor/io/graph_serializer.cpp',
  'vulkan_editor/io/image_loader.cpp',
  'vulkan_editor/io/directory_watcher.cpp',
  'vulkan_editor/io/model_watcher.cpp',

  # GPU resources and memory
  'vulkan_editor/gpu/primitives.cpp',
  'vulkan_editor/gpu/batched_stager.cpp',
  'vulkan_editor/gpu/staging_buffer_pool.cpp',

  # Utilities
  'vulkan_editor/util/logger.cpp',

  # UI
  'vulkan_editor/ui/pipeline_editor_ui.cpp',
  'vulkan_editor/ui/pipeline_settings_ui.cpp',
  'vulkan_editor/ui/model_settings_ui.cpp',
  'vulkan_editor/ui/camera_editor_ui.cpp',
  'vulkan_editor/ui/light_editor_ui.cpp',
  'vulkan_editor/ui/attachment_editor_ui.cpp',
  'vulkan_editor/ui/live_view.cpp',
  'vulkan_editor/ui/debug_console_ui.cpp',
  'vulkan_editor/ui/user_messages_ui.cpp',
)

external_files = files(
  'external/tinyfiledialogs.cpp',
  'external/utilities/builders.cpp',
  'external/utilities/drawing.cpp',
  'external/utilities/widgets.cpp',
  'external/SimpleFileDialog.cpp',
)

executable(
  'main', [
    'main.cpp',
    vulkan_files,
    editor_files,
    external_files
  ],
  dependencies: [
    sdl3,
    vulkan,
    glm,
    shader_slang,
    imgui,
    imnodes,
    vma,
    efsw,
    vkDuck_dep,
  ],
  cpp_args: ['-DVKDUCK_NO_VMA_IMPLEMENTATION'],
  include_directories: ['external'],
)