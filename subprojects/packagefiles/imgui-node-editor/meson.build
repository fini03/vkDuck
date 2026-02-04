project('imgui-node-editor', 'cpp')

nodeedit_dep = declare_dependency(
  sources: [
    'imgui_node_editor.cpp',
    'imgui_node_editor_api.cpp',
    'crude_json.cpp',
    'imgui_canvas.cpp'
  ],
  include_directories: ['.']
)

meson.override_dependency('imgui-node-editor', nodeedit_dep)
