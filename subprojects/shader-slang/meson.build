project('shader-slang', 'cpp')

if host_machine.system() == 'linux' and host_machine.cpu_family() == 'x86_64'
  slang_dep = dependency('shader-slang-linux-x86_64')
elif host_machine.system() == 'darwin' and host_machine.cpu_family() == 'aarch64'
  slang_dep = dependency('shader-slang-macos-aarch64')
else
  error('Current architecture is not supported')
endif

meson.override_dependency('shader-slang', slang_dep)
