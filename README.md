# vkDuck

A visual node-based Vulkan graphics pipeline editor that generates standalone C++ applications.

![vkDuck Editor Screenshot](docs/screenshots/editor-overview.png)
*Visual pipeline editor with live GPU preview*

## About

vkDuck is a visual development environment for creating Vulkan rendering pipelines without writing low-level GPU code. Design your graphics pipeline by connecting nodes in a visual graph, preview the results in real-time, and export a complete, standalone C++ Vulkan application.

This project was developed as part of a Master's thesis.

## Features

### Visual Pipeline Editor
- **Node-based workflow** - Connect pipeline stages visually using an intuitive graph interface
- **Live GPU preview** - See your rendering output in real-time as you build
- **Shader reflection** - Write slang shaders and see the node pins getting generated
- **Interactive cameras** - FPS, Orbital, and Fixed camera modes with mouse/keyboard controls

![Node Graph Example](docs/screenshots/node-graph.png)
*Example node graph connecting model, camera, and pipeline nodes*

### Asset Management
- **glTF/GLB model loading** - Import 3D models with automatic texture resolution
- **Embedded camera extraction** - Use cameras defined in glTF files
- **File watching** - Models and shaders auto-reload when modified
- **Parallel image loading** - Fast texture loading with optimized decoders

### Code Generation
- **Complete project export** - Generate standalone C++ applications with Meson build files
- **Readable output** - Generated code is clean, well-structured, and easy to understand
- **All assets included** - Shaders, models, and textures are bundled with the project

## Architecture

```
vkDuck/
├── vulkan_editor/          # Visual pipeline editor (ImGui-based)
│   ├── graph/              # Node system (Model, Pipeline, Camera, Light, Present)
│   ├── gpu/                # GPU resource management
│   ├── shader/             # Shader compilation and reflection
│   ├── ui/                 # Editor UI components
│   └── io/                 # File I/O and code generation
│
└── subprojects/vkDuck/     # Shared library for editor and generated projects
    └── include/vkDuck/
        ├── vulkan_base.h       # Vulkan initialization framework
        ├── camera_controller.h # Camera system
        ├── model_loader.h      # glTF model loading
        └── image_loader.h      # Image loading
```

## Building

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 16+, MSVC 2022+)
- [Meson](https://mesonbuild.com/) build system
- [Vulkan SDK](https://vulkan.lunarg.com/)
- [Slang](https://shader-slang.com/) shader compiler

### Dependencies

| Library | Purpose |
|---------|---------|
| SDL3 | Window creation and input |
| Vulkan | Graphics API |
| GLM | Math library |
| ImGui | UI framework |
| imgui-node-editor | Node graph visualization |
| VMA | GPU memory allocation |

Most dependencies are fetched automatically via Meson wraps.

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/fini03/vkDuck.git
cd vkDuck

# Configure and build
meson setup build
meson compile -C build

# Run the editor
./build/main
```

## Usage

### Creating a Pipeline

1. **Start the editor** and select a project folder
2. **Add a Pipeline Node** - Load your vertex and fragment shaders
3. **Add a Model Node** - Import a glTF/GLB model
4. **Add a Camera Node** - Choose from Fixed, FPS, or Orbital camera types
5. **Connect the nodes** - Link model outputs to pipeline inputs
6. **Preview** - Use the Live View to see your rendering in real-time
7. **Generate** - Export as a standalone C++ application

### Node Types

| Node | Description |
|------|-------------|
| **Model** | Loads 3D models (glTF/GLB) and manages materials/textures |
| **Pipeline** | Graphics pipeline with shader configuration |
| **Camera** | View/projection matrices (Fixed, FPS, Orbital modes) |
| **Light** | Light sources with configurable parameters |
| **Present** | Final output to the screen |

### Building Generated Projects

After generating a project:

```bash
cd generated_project
meson setup build
meson compile -C build
./build/main
```

## Camera Controls

### FPS Camera
- **WASD** - Move forward/backward/strafe
- **Mouse** - Look around

### Orbital Camera
- **Left Mouse Drag** - Rotate around target
- **Scroll** - Zoom in/out
- **Middle Mouse Drag** - Pan

## Screenshots

### Editor Interface
![Editor Interface](docs/screenshots/editor-interface.png)

### Live View
![Live View](docs/screenshots/live-view.png)

## License

This project is open source. You can find all licensing information in the `licenses` folder.

## Acknowledgments

This project was developed as part of a master's thesis. Special thanks to the developers of the open source libraries that made this project possible:

- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI
- [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) - Node graph extension for ImGui
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - GPU memory management
- [TinyGLTF](https://github.com/syoyo/tinygltf) - glTF model loading
- [GLM](https://github.com/g-truc/glm) - OpenGL Mathematics
- [SDL3](https://github.com/libsdl-org/SDL) - Cross-platform windowing
- [Slang](https://shader-slang.com/) - Shader compilation and reflection
