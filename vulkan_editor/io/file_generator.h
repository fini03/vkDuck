#pragma once
#include "primitive_generator.h"
#include <string>

class NodeGraph;

namespace primitives {
struct Store;
}

/**
 * @class FileGenerator
 * @brief Generates complete standalone C++ projects from the visual pipeline.
 *
 * Exports all GPU primitives, shaders, and configuration to a compilable
 * Meson project using Inja templates. Generated projects use vkDuck
 * for shared Vulkan initialization code.
 */
class FileGenerator {
public:
    FileGenerator() = default;

    void generateProject(
        NodeGraph& graph,
        primitives::Store& store,
        const std::filesystem::path& outputDirectory
    );

private:
    PrimitiveGenerator primitiveGenerator;

    void generateSDKLinks(const std::filesystem::path& projectRoot);
    void generateCameraInstances(primitives::Store& store, const std::filesystem::path& outputDir);
    void generatePrimitives(NodeGraph& graph, primitives::Store& store, const std::filesystem::path& outputDir);
    void generateRenderer(const primitives::Store& store, const std::filesystem::path& outputDir);
    void generateMesonBuild(const primitives::Store& store, const std::filesystem::path& outputDir);
    void generateCMakeBuild(const primitives::Store& store, const std::filesystem::path& outputDir);
    void generateMain(const std::filesystem::path& outputDir);
    void generateShaders(const primitives::Store& store, const std::filesystem::path& outputDir);

    bool hasMovableCameras(const primitives::Store& store) const;
    bool hasAnyCameras(const primitives::Store& store) const;
    bool hasModelFiles(const primitives::Store& store) const;
    bool hasAnyLights(const primitives::Store& store) const;
    bool hasImageFiles(const primitives::Store& store) const;
};
