project('imgui', 'cpp')

imgui_dep = declare_dependency(
  sources: [
    'imgui.cpp',
    'imgui_draw.cpp',
    'imgui_widgets.cpp',
    'imgui_tables.cpp',
    'backends/imgui_impl_sdl3.cpp',
    'backends/imgui_impl_vulkan.cpp',
  ],
  include_directories: ['.', 'backends']
)

meson.override_dependency('imgui', imgui_dep)
