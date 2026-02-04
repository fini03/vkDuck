project('VulkanMemoryAllocator', ['c', 'cpp'], version: '3.3.0')

vma_dep = declare_dependency(
  include_directories: 'include'
)

meson.override_dependency('vulkan-memory-allocator', vma_dep)
