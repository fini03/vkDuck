project('slang', 'cpp')

cc = meson.get_compiler('cpp')
slang_dep = declare_dependency(
  dependencies : cc.find_library('slang', dirs : meson.current_source_dir() / 'lib'),
  include_directories : include_directories('include'))
